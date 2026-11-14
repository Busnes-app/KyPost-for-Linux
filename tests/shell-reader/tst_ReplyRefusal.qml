import QtQuick
import QtTest
import com.kysecurity.mail 1.0
import KyPostScreens

// Android's mayReplyOrForward in the real reader: a client-protected or
// unloaded message seeds no compose and says why next to the buttons.
TestCase {
    name: "ReplyRefusal"
    when: windowShown
    visible: true
    width: 800
    height: 900
    property var reader: null
    readonly property string encrypted: "Replying to encrypted mail isn't available in the app yet — use webmail."

    SignalSpy { id: composeSpy; signalName: "composeRequested" }

    function open(pgpState, id) {
        MailApp.metadataRow = {messageId: "reply-refusal", folder: "INBOX", subject: "Outer subject",
            sender: "sender@example.com", sentTo: "me@example.com", cc: "", body: "Hello", bodyMode: "plain",
            status: "read", searchOnly: false, bodyPending: false, pgpState: pgpState, canDecryptHere: false,
            pgpBannerTitle: "", pgpBannerBody: ""}
        MailApp.forwardCalls = 0
        const component = Qt.createComponent("qrc:/qml/pages/EmailDetail.qml")
        compare(component.status, Component.Ready, component.errorString())
        reader = component.createObject(this, {width: 800, height: 900, folder: "INBOX", messageId: id || "reply-refusal"})
        verify(reader)
        reader.attachments = [{name: "a.txt", size: 1, index: 0}]
        composeSpy.target = reader
        composeSpy.clear()
    }
    function cleanup() {
        reader.destroy()
        reader = null
        MailApp.metadataRow = null
    }
    function button(name) { const b = findChild(reader, name); verify(b, name); return b }
    function banner() { return findChild(reader, "replyRefusalBanner") }

    function test_protected_mail_is_refused_with_the_reason_by_the_buttons_data() {
        return [{tag: "reply", button: "replyButton", action: "Reply"},
                {tag: "reply all", button: "replyAllButton", action: "Reply all"},
                {tag: "forward", button: "forwardButton", action: "Forward"}]
    }
    function test_protected_mail_is_refused_with_the_reason_by_the_buttons(data) {
        open(1)
        const b = button(data.button)
        verify(b.enabled)
        verify(b.muted)
        // The action's name comes first, then why it is unavailable.
        compare(b.tooltip, data.action + " (unavailable): " + encrypted)
        verify(!banner().visible)
        b.clicked()
        compare(composeSpy.count, 0)
        compare(MailApp.forwardCalls, 0)
        verify(banner().visible)
        compare(reader.replyRefusal, encrypted)
        // Directly under the button row, where the click happened.
        const gap = banner().mapToItem(reader, 0, 0).y - b.mapToItem(reader, 0, b.height).y
        verify(gap >= 0 && gap <= 24, gap)
    }

    function test_an_unloaded_message_is_refused_but_not_called_encrypted() {
        open(0, "not-in-the-cache")
        compare(reader.email.pgpState, undefined)
        button("replyButton").clicked()
        compare(composeSpy.count, 0)
        verify(banner().visible)
        compare(reader.replyRefusal, "This message hasn't loaded, so it can't be replied to or forwarded yet.")
    }

    function test_ordinary_mail_replies_and_forwards() {
        open(0)
        for (const name of ["replyButton", "replyAllButton", "forwardButton"]) {
            verify(!button(name).muted, name)
            button(name).clicked()
        }
        compare(composeSpy.count, 3)
        compare(composeSpy.signalArguments[0][1], "Re: Outer subject")
        compare(composeSpy.signalArguments[2][1], "Fwd: Outer subject")
        compare(MailApp.forwardCalls, 1)
        verify(!banner().visible)
        compare(button("replyButton").tooltip, "Reply")
    }

    function test_the_reason_goes_when_the_reader_moves_on() {
        open(1)
        button("replyButton").clicked()
        verify(banner().visible)
        reader.messageId = "<playshot-1@kypost-demo.local>"
        verify(!reader.replyRefused)
        verify(!banner().visible)
    }

    function test_the_reason_is_announced() {
        open(1)
        button("replyButton").clicked()
        const text = banner().children[0]
        compare(text.text, encrypted)
        compare(text.Accessible.role, Accessible.AlertMessage)
        compare(text.Accessible.name, encrypted)
    }
}
