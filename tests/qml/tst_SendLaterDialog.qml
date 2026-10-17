import QtQuick 2.15
import QtTest 1.15
import com.kysecurity.mail 1.0
import "qrc:/qml/components" as Components

// "Send later": a preset or a valid custom time is chosen once; a bad custom
// time, a cancel or a lock chooses nothing.
TestCase {
    id: testCase
    name: "SendLaterDialog"
    when: windowShown
    visible: true
    width: 480
    height: 640

    Components.SendLaterDialog {
        id: dialog
        anchors.fill: parent
    }

    SignalSpy {
        id: chosenSpy
        target: dialog
        signalName: "chosen"
    }

    function init() {
        AppLock.locked = false
        dialog.close()
        chosenSpy.clear()
    }

    function cleanup() {
        AppLock.locked = false
    }

    function find(item, name) {
        for (let i = 0; i < item.children.length; i++) {
            const child = item.children[i]
            if (child.objectName === name)
                return child
            const found = find(child, name)
            if (found)
                return found
        }
        return null
    }

    function click(name) {
        const target = find(dialog, name)
        verify(target, name)
        mouseClick(target, target.width / 2, target.height / 2)
    }

    function test_a_preset_is_chosen() {
        dialog.open()
        tryVerify(function () { return find(dialog, "sendLaterPreset") !== null })
        click("sendLaterPreset")

        compare(chosenSpy.count, 1)
        compare(chosenSpy.signalArguments[0][0], 1893484800000)
        verify(!dialog.isOpen)
    }

    function test_a_custom_time_is_chosen() {
        dialog.open()
        find(dialog, "sendLaterDate").text = "2030-01-02"
        find(dialog, "sendLaterTime").text = "10:00"
        click("sendLaterCustom")

        compare(chosenSpy.count, 1)
        compare(chosenSpy.signalArguments[0][0], 1893578400000)
    }

    function test_a_time_outside_the_window_chooses_nothing() {
        dialog.open()
        find(dialog, "sendLaterDate").text = "2020-01-01"
        find(dialog, "sendLaterTime").text = "10:00"
        click("sendLaterCustom")

        compare(chosenSpy.count, 0)
        verify(dialog.isOpen)
        verify(dialog.error !== "")
    }

    function test_cancel_and_lock_choose_nothing() {
        dialog.open()
        keyClick(Qt.Key_Escape)
        verify(!dialog.isOpen)

        dialog.open()
        mouseClick(dialog, 4, 4)
        verify(!dialog.isOpen)

        dialog.open()
        AppLock.locked = true
        verify(!dialog.isOpen)
        dialog.choose(1893484800000)
        compare(chosenSpy.count, 0)
    }
}
