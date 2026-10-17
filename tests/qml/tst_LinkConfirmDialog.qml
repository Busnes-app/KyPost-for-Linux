import QtQuick 2.15
import QtTest 1.15
import com.kysecurity.mail 1.0
import "qrc:/qml/components" as Components

// A web link in a message leaves the app only through this dialog. It must
// show the host it was given, open exactly the URL it was given, and open
// nothing on any other exit.
TestCase {
    id: testCase
    name: "LinkConfirmDialog"
    when: windowShown
    visible: true
    width: 600
    height: 400

    Component {
        id: dialogComponent
        Components.LinkConfirmDialog {}
    }

    SignalSpy {
        id: openSpy
        signalName: "openRequested"
    }

    // Shaped like MailApp.linkTarget()'s result.
    readonly property var target: ({
        host: "xn--pypl-53dc.com",
        shownUrl: "https://xn--pypl-53dc.com/login",
        mismatchHost: ""
    })

    function createDialog() {
        const dialog = createTemporaryObject(dialogComponent, testCase)
        verify(dialog !== null, "dialog must instantiate")
        openSpy.target = dialog
        openSpy.clear()
        return dialog
    }

    function child(item, name) {
        if (item.objectName === name)
            return item
        for (let i = 0; i < item.children.length; ++i) {
            const found = child(item.children[i], name)
            if (found)
                return found
        }
        return null
    }

    function test_showsTheParsedHostAndAddress() {
        const dialog = createDialog()
        dialog.show(target, false)
        compare(child(dialog, "linkHost").text, "xn--pypl-53dc.com")
        compare(child(dialog, "linkUrl").text, "https://xn--pypl-53dc.com/login")
        compare(child(dialog, "linkHost").textFormat, Text.PlainText)
        verify(!child(dialog, "linkMismatch").visible)
        verify(!child(dialog, "linkPhishing").visible)
    }

    function test_mismatchAndPhishingAreFlagged() {
        const dialog = createDialog()
        const lying = Object.assign({}, target, { mismatchHost: "xn--pypl-53dc.com" })
        dialog.show(lying, true)
        verify(child(dialog, "linkMismatch").visible)
        verify(child(dialog, "linkPhishing").visible)
    }

    function test_openSendsExactlyTheShownUrl() {
        const dialog = createDialog()
        dialog.show(target, false)
        dialog.confirm()
        verify(!dialog.isOpen)
        compare(openSpy.count, 1)
        compare(openSpy.signalArguments[0][0], target.shownUrl)
    }

    function test_cancelEscapeAndScrimOpenNothing() {
        const dialog = createDialog()
        dialog.show(target, false)
        dialog.cancel()
        verify(!dialog.isOpen)

        dialog.show(target, false)
        keyClick(Qt.Key_Escape)
        verify(!dialog.isOpen)

        dialog.show(target, false)
        mouseClick(dialog, 5, 5)
        verify(!dialog.isOpen)
        compare(openSpy.count, 0)
    }

    function test_lockingClosesWithoutOpening() {
        const dialog = createDialog()
        dialog.show(target, false)
        AppLock.locked = true
        verify(!dialog.isOpen)
        compare(openSpy.count, 0)
        AppLock.locked = false
    }
}
