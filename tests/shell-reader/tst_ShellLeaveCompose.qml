import QtQuick
import QtTest
import com.kysecurity.mail 1.0

// Owner decision Q1: every way out of a compose pane that holds something asks
// "Discard this message?" (Save draft / Discard / Keep writing). Here, not in
// tests/shell: the composer's editor starts WebEngine.
TestCase {
    name: "ShellLeaveCompose"
    when: windowShown

    property var shell: null
    property var dialog: null
    readonly property string mail: "<playshot-1@kypost-demo.local>"

    function initTestCase() {
        const component = Qt.createComponent("qrc:/qml/AppShell.qml")
        compare(component.status, Component.Ready, component.errorString())
        shell = component.createObject(null, {width: 1920, height: 1080})
        shell.show()
        waitForRendering(shell.contentItem)
        dialog = findChild(shell.contentItem, "leaveComposeDialog")
        verify(dialog)
    }

    function cleanupTestCase() { shell.destroy() }

    function init() {
        if (dialog.isOpen) dialog.reject()
        shell.detailMode = "empty"
    }

    function writing() {
        shell.openCompose("someone@example.com", "Unsent", "")
        compare(shell.detailMode, "compose")
    }

    function test_clicking_a_message_asks_and_keep_writing_stays() {
        writing()
        shell.selectEmail(mail, "INBOX")
        tryVerify(() => dialog.isOpen)
        compare(shell.detailMode, "compose")
        dialog.reject()
        compare(shell.detailMode, "compose")
        compare(shell.pendingLeave, null)
    }

    function test_discard_leaves_for_where_the_user_was_going() {
        writing()
        shell.selectEmail(mail, "INBOX")
        tryVerify(() => dialog.isOpen)
        dialog.alternate()
        compare(shell.detailMode, "email")
        compare(shell.selectedMessageId, mail)
    }

    function test_every_way_out_asks() {
        const ways = [function () { shell.openCompose("", "", "") },
                      function () { shell.navigate("contacts") },
                      function () { shell.navigate("settings") },
                      function () { shell.selectFolder("Archive") },
                      function () { shell.openRestoredDraft({token: "t", to: "x@example.com"}) },
                      function () { shell.leaveCompose(shell.closeDetail) },
                      function () { MailApp.notificationEmailResolved(mail, "INBOX") },
                      function () { MailApp.forwardComposeRequested({token: "f", subject: "Fwd: x"}) }]
        for (let i = 0; i < ways.length; ++i) {
            shell.detailMode = "empty"
            writing()
            ways[i]()
            tryVerify(() => dialog.isOpen, 5000, "way " + i)
            compare(shell.detailMode, "compose", "way " + i)
            dialog.reject()
        }
    }

    function test_save_draft_leaves_only_once_the_draft_has_landed() {
        writing()
        shell.navigate("contacts")
        tryVerify(() => dialog.isOpen)
        const before = MailApp.draftSaves
        dialog.accept()
        tryVerify(() => MailApp.draftSaves === before + 1)
        compare(shell.detailMode, "compose")
        MailApp.draftSaveCompleted(MailApp.draftSaves, false)
        compare(shell.detailMode, "compose") // a failed save stays
        shell.navigate("contacts")
        tryVerify(() => dialog.isOpen)
        dialog.accept()
        tryVerify(() => MailApp.draftSaves === before + 2)
        MailApp.draftSaveCompleted(MailApp.draftSaves, true)
        tryCompare(shell, "detailMode", "empty")
        compare(shell.destination, "contacts")
    }

    function test_an_empty_composer_leaves_without_asking() {
        shell.openCompose("", "", "")
        shell.selectEmail(mail, "INBOX")
        tryCompare(shell, "detailMode", "email")
        verify(!dialog.isOpen)
    }
}
