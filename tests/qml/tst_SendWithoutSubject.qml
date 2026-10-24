import QtQuick
import QtTest
import com.kysecurity.mail 1.0
import "qrc:/qml/components" as Components
import "../../app/qml/utils/format.js" as Format

// Android #178: a blank subject is asked about, not refused. Recipient and
// body stay required. The question is a ConfirmDialog, which the lock closes
// as a refusal, so a send can never be confirmed from under the PIN screen.
TestCase {
    id: testCase
    name: "SendWithoutSubject"
    when: windowShown
    visible: true
    width: 480
    height: 360

    Components.ConfirmDialog {
        id: dialog
        anchors.fill: parent
        title: "Send without a subject?"
        acceptText: "Send"
        rejectText: "Add subject"
    }

    SignalSpy { id: acceptedSpy; target: dialog; signalName: "accepted" }
    SignalSpy { id: rejectedSpy; target: dialog; signalName: "rejected" }

    function init() {
        AppLock.locked = false
        dialog.isOpen = false
        acceptedSpy.clear()
        rejectedSpy.clear()
    }

    function test_readiness() {
        compare(Format.sendReadiness("a@example.com", "Hi", false, false), "ready")
        compare(Format.sendReadiness("a@example.com", "", false, false), "confirmSubject")
        compare(Format.sendReadiness("a@example.com", "   ", false, false), "confirmSubject")
        compare(Format.sendReadiness("a@example.com", "", false, true), "ready")
        compare(Format.sendReadiness("", "Hi", false, true), "incomplete")
        compare(Format.sendReadiness(" ", "Hi", false, false), "incomplete")
        compare(Format.sendReadiness("a@example.com", "Hi", true, true), "incomplete")
        // Incomplete wins: there is nothing to confirm for a message that cannot go.
        compare(Format.sendReadiness("", "", true, false), "incomplete")
    }

    // Compose cannot load here (its editor is a WebEngineView), so its wiring
    // is read from the shipped source: the refusal is gone, the question is asked,
    // and confirming re-enters trySend with the answer and the scheduled time.
    function test_composeAsksInsteadOfRefusing() {
        const source = new XMLHttpRequest()
        source.open("GET", "qrc:/qml/pages/Compose.qml", false)
        source.send()
        compare(source.status, 200)
        const text = source.responseText
        verify(text.indexOf("Please fill in all fields") === -1)
        verify(text.indexOf("Format.sendReadiness(") !== -1)
        verify(text.indexOf("onAccepted: root.trySend(emptySubjectDialog.sendAt, true)") !== -1)
    }

    function test_sendIsTheExplicitButtonOnly() {
        dialog.open()
        verify(dialog.visible)
        keyClick(Qt.Key_Return)
        keyClick(Qt.Key_Space)
        compare(acceptedSpy.count, 0, "a keystroke confirmed a send without a subject")
        mouseClick(findChild(dialog, "confirmDialogAccept"))
        compare(acceptedSpy.count, 1)
        verify(!dialog.visible)
    }

    function test_addSubjectEscapeAndScrimRefuse() {
        dialog.open()
        mouseClick(findChild(dialog, "confirmDialogReject"))
        compare(rejectedSpy.count, 1)
        dialog.open()
        keyClick(Qt.Key_Escape)
        compare(rejectedSpy.count, 2)
        dialog.open()
        mouseClick(dialog, 5, 5)
        compare(rejectedSpy.count, 3)
        compare(acceptedSpy.count, 0)
    }

    function test_lockingRefusesAndNothingIsAcceptedWhileLocked() {
        dialog.open()
        AppLock.locked = true
        verify(!dialog.visible)
        compare(rejectedSpy.count, 1)
        dialog.isOpen = true
        dialog.accept()
        compare(acceptedSpy.count, 0, "a send was confirmed under the lock")
    }
}
