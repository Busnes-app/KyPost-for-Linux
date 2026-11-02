import QtQuick
import QtTest
import com.kysecurity.mail 1.0
import "qrc:/qml/pages" as Pages

// The new-device approval wait (#516): the page shows the code the web page
// shows, the server it waits on and what to do, and Cancel ends the wait in
// the controller (which stops polling and wipes the pending credential).
TestCase {
    id: testCase
    name: "PairingApproval"
    when: windowShown
    visible: true
    width: 412
    height: 732

    Component {
        id: pageComponent
        Pages.Pairing { width: testCase.width; height: testCase.height }
    }

    function init() {
        Pairing.pairingState = "approval"
        Pairing.pairingError = ""
        Pairing.approvalCode = "048213"
        Pairing.approvalServer = "https://mail.example.com"
        Pairing.cancelApprovalCount = 0
    }

    function find(item, name) {
        if (item.objectName === name)
            return item
        for (let i = 0; i < item.children.length; ++i) {
            const hit = find(item.children[i], name)
            if (hit)
                return hit
        }
        return null
    }

    function createPage() {
        const page = createTemporaryObject(pageComponent, testCase)
        verify(page !== null)
        verify(waitForRendering(page))
        return page
    }

    function test_shows_code_server_and_instructions() {
        const page = createPage()
        const code = find(page, "approvalCode")
        verify(code.visible)
        compare(code.text, "048 213")
        compare(code.textFormat, Text.PlainText)
        verify(code.font.pixelSize >= 32, "the code is the largest thing on the page")
        compare(find(page, "approvalServer").text, "https://mail.example.com")
        compare(find(page, "approvalServer").textFormat, Text.PlainText)
        compare(find(page, "approvalInstructions").text,
                "Approve this device on the KyPost web page that showed the code. Check the code matches.")
    }

    function test_cancel_ends_the_wait() {
        const page = createPage()
        const cancel = find(page, "approvalCancel")
        verify(cancel.visible)
        mouseClick(cancel)
        compare(Pairing.cancelApprovalCount, 1)
        verify(!find(page, "approvalCode").visible)
    }

    function test_hidden_outside_the_wait() {
        Pairing.pairingState = "idle"
        const page = createPage()
        verify(!find(page, "approvalCode").visible)
        verify(!find(page, "approvalCancel").visible)
    }
}
