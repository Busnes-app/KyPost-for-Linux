import QtQuick 2.15
import QtTest 1.15
import com.kysecurity.mail 1.0
import "qrc:/qml/components" as Components

// The "Snooze until" picker: a preset or an accepted typed time snoozes; a
// refused typed time, cancel and the lock snooze nothing.
TestCase {
    id: testCase
    name: "SnoozeDialog"
    when: windowShown
    visible: true
    width: 480
    height: 600

    readonly property date tomorrow: new Date(2030, 0, 2, 8, 0)
    readonly property var presets: [
        { kind: "tomorrow", at: testCase.tomorrow },
        { kind: "nextWeek", at: new Date(2030, 0, 7, 8, 0) }
    ]
    property var parsed: []

    Components.SnoozeDialog {
        id: dialog
        anchors.fill: parent
        // Accepts one fixed input, like MailApp.snoozeCustomTime's window.
        parseCustom: function (date, time) {
            testCase.parsed.push(date + " " + time)
            return date === "2030-01-03" && time === "09:30" ? new Date(2030, 0, 3, 9, 30) : null
        }
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
        testCase.parsed = []
    }

    function cleanup() {
        AppLock.locked = false
    }

    // Click only once the panel and its preset rows are laid out.
    function openSettled(ids) {
        dialog.open(ids, testCase.presets)
        verify(waitForRendering(dialog))
    }

    function find(name) {
        const found = []
        const walk = function (item) {
            for (let i = 0; i < item.children.length; i++) {
                const child = item.children[i]
                if (child.objectName === name)
                    found.push(child)
                walk(child)
            }
        }
        walk(dialog)
        return found
    }

    function test_presets_are_listed_with_their_label() {
        dialog.open(["m-1"], testCase.presets)

        tryVerify(function () { return testCase.find("snoozePreset").length === 2 })
        verify(dialog.presetLabel(testCase.presets[0]).startsWith("Tomorrow, "))
        verify(dialog.presetLabel(testCase.presets[1]).startsWith("Next week, "))
    }

    function test_tapping_a_preset_snoozes_until_its_time() {
        testCase.openSettled(["m-1", "m-2"])

        const first = testCase.find("snoozePreset")[0]
        mouseClick(first, first.width / 2, first.height / 2)

        compare(chosenSpy.count, 1)
        compare(chosenSpy.signalArguments[0][0], ["m-1", "m-2"])
        compare(chosenSpy.signalArguments[0][1].getTime(), testCase.tomorrow.getTime())
        verify(!dialog.isOpen)
    }

    function test_a_refused_typed_time_snoozes_nothing_and_says_why() {
        testCase.openSettled(["m-1"])
        testCase.find("snoozeDate")[0].text = "2020-01-01"
        testCase.find("snoozeTime")[0].text = "09:30"
        const button = testCase.find("snoozeCustom")[0]
        mouseClick(button, button.width / 2, button.height / 2)

        compare(testCase.parsed, ["2020-01-01 09:30"])
        compare(chosenSpy.count, 0)
        verify(dialog.isOpen)
        verify(dialog.customError !== "")
    }

    function test_an_accepted_typed_time_snoozes_until_it() {
        testCase.openSettled(["m-1"])
        testCase.find("snoozeDate")[0].text = "2030-01-03"
        testCase.find("snoozeTime")[0].text = "09:30"
        const button = testCase.find("snoozeCustom")[0]
        mouseClick(button, button.width / 2, button.height / 2)

        compare(chosenSpy.count, 1)
        compare(chosenSpy.signalArguments[0][1].getTime(), new Date(2030, 0, 3, 9, 30).getTime())
        verify(!dialog.isOpen)
    }

    function test_cancel_and_backdrop_snooze_nothing() {
        dialog.open(["m-1"], testCase.presets)
        mouseClick(dialog, 4, 4)
        verify(!dialog.isOpen)

        dialog.open(["m-1"], testCase.presets)
        keyClick(Qt.Key_Escape)
        verify(!dialog.isOpen)

        compare(chosenSpy.count, 0)
    }

    function test_locking_closes_the_picker_and_snoozes_nothing() {
        dialog.open(["m-1"], testCase.presets)
        AppLock.locked = true
        verify(!dialog.isOpen)

        dialog.open(["m-1"], testCase.presets)
        dialog.choose(testCase.tomorrow)
        compare(chosenSpy.count, 0)

        AppLock.locked = false
        dialog.open(["m-1"], testCase.presets)
        dialog.choose(testCase.tomorrow)
        compare(chosenSpy.count, 1)
    }
}
