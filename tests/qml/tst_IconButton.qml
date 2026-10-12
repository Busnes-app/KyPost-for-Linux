import QtQuick
import QtTest
import com.kysecurity.mail 1.0
import "qrc:/qml/components" as Components

// The PIN overlay blocks pointer and key input, but an accessibility action
// reaches the button itself. EmailDetail's Archive, Delete and Move are
// IconButtons, so a reader left open under the lock must not act on one.
TestCase {
    id: testCase
    name: "IconButton"
    when: windowShown
    visible: true
    width: 200
    height: 200

    Components.IconButton {
        id: button
        icon: "edit-delete"
        tooltip: "Delete"
    }

    SignalSpy {
        id: clickedSpy
        target: button
        signalName: "clicked"
    }

    function init() {
        AppLock.locked = false
        button.enabled = true
        clickedSpy.clear()
    }

    function cleanup() {
        AppLock.locked = false
    }

    function test_accessibility_press_is_refused_while_locked() {
        AppLock.locked = true
        button.Accessible.pressAction()
        compare(clickedSpy.count, 0)

        AppLock.locked = false
        button.Accessible.pressAction()
        compare(clickedSpy.count, 1)
    }

    function test_accessibility_press_is_refused_while_disabled() {
        button.enabled = false
        button.Accessible.pressAction()
        compare(clickedSpy.count, 0)
    }
}
