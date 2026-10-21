import QtQuick
import QtTest
import com.kysecurity.mail 1.0
import "qrc:/qml/components" as Components

// Every tap target reachable without a pointer: Tab focuses it, Return and
// Space press it, and nothing fires while the app is locked.
TestCase {
    name: "Keyboard"
    when: windowShown
    visible: true
    width: 500
    height: 300

    Components.NavBar {
        id: bar
        width: 412
        height: 80
    }
    Components.Chip {
        id: chip
        y: 100
        text: "Primary"
    }
    Components.IconButton {
        id: button
        y: 150
        icon: "refresh"
        tooltip: "Refresh"
    }

    SignalSpy { id: navSpy; target: bar; signalName: "activated" }
    SignalSpy { id: chipSpy; target: chip; signalName: "clicked" }
    SignalSpy { id: buttonSpy; target: button; signalName: "clicked" }

    function init() {
        AppLock.locked = false
        navSpy.clear(); chipSpy.clear(); buttonSpy.clear()
    }
    function cleanup() { AppLock.locked = false }

    function test_nav_destination_by_keyboard() {
        const contacts = findChild(bar, "nav-contacts")
        verify(contacts.activeFocusOnTab)
        contacts.forceActiveFocus()
        keyClick(Qt.Key_Return)
        compare(navSpy.count, 1)
        compare(navSpy.signalArguments[0][0], "contacts")
        keyClick(Qt.Key_Space)
        compare(navSpy.count, 2)
    }

    function test_chip_and_icon_button_by_keyboard() {
        verify(chip.activeFocusOnTab)
        chip.forceActiveFocus()
        keyClick(Qt.Key_Space)
        compare(chipSpy.count, 1)
        verify(button.activeFocusOnTab)
        button.forceActiveFocus()
        keyClick(Qt.Key_Return)
        compare(buttonSpy.count, 1)
    }

    function test_nothing_fires_while_locked() {
        AppLock.locked = true
        findChild(bar, "nav-inbox").forceActiveFocus()
        keyClick(Qt.Key_Return)
        chip.forceActiveFocus()
        keyClick(Qt.Key_Return)
        button.forceActiveFocus()
        keyClick(Qt.Key_Return)
        compare(navSpy.count + chipSpy.count + buttonSpy.count, 0)
    }
}
