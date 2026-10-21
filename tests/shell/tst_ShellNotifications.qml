import QtQuick
import QtTest
import com.kysecurity.mail 1.0

// The root's notification handlers, run for real. They once assigned the
// read-only currentSection, threw, and a tapped notification did nothing.
TestCase {
    name: "ShellNotifications"

    property var shell: null

    function initTestCase() {
        const component = Qt.createComponent("qrc:/qml/AppShell.qml")
        compare(component.status, Component.Ready, component.errorString())
        shell = component.createObject(null, {width: 412, height: 732})
        verify(shell)
    }

    function cleanupTestCase() { shell.destroy() }

    function init() {
        MailApp.openedFromNotification = ""
        shell.navigate("contacts")
    }

    function test_open_requested_switches_to_mail_and_asks_the_controller() {
        compare(shell.destination, "contacts")
        MailApp.openEmailRequested("<playshot-1@kypost-demo.local>")
        compare(MailApp.openedFromNotification, "<playshot-1@kypost-demo.local>")
        compare(shell.destination, "inbox")
        compare(shell.currentSection, "mail")
    }

    function test_ambiguous_notification_asks_which_folder() {
        const dialog = findChild(shell.contentItem, "notificationFolderDialog")
        verify(dialog)
        verify(!dialog.visible)
        MailApp.notificationEmailAmbiguous("<playshot-1@kypost-demo.local>", ["INBOX", "Archive"])
        compare(shell.destination, "inbox")
        tryVerify(() => dialog.visible)
    }

    function test_resolved_notification_opens_the_mail() {
        MailApp.notificationEmailResolved("<playshot-1@kypost-demo.local>", "INBOX")
        compare(shell.detailMode, "email")
        compare(shell.selectedMessageId, "<playshot-1@kypost-demo.local>")
    }
}
