import QtQuick 2.15
import QtTest 1.15
import com.kysecurity.mail 1.0
import "qrc:/qml/pages" as Pages

// Cancel returns mail to Drafts, so it takes a second, confirming tap, and is
// offered only for a message still waiting.
TestCase {
    id: testCase
    name: "ScheduledList"
    when: windowShown
    visible: true
    width: 480
    height: 640

    Pages.ScheduledList {
        id: page
        anchors.fill: parent
    }

    function init() {
        AppLock.locked = false
        page.confirmingId = ""
        Scheduled.lastCancelled = ""
        Scheduled.items = [
            { id: "w-1", subject: "Waiting", to: "x@example.com", when: "Sun 08:00", status: "Waiting", reason: "", cancellable: true },
            { id: "s-2", subject: "Gone", to: "y@example.com", when: "Sat 08:00", status: "Sent", reason: "", cancellable: false }
        ]
    }

    function cancelButtons() {
        const found = []
        const walk = function (item) {
            for (let i = 0; i < item.children.length; i++) {
                const child = item.children[i]
                if (child.objectName === "cancelScheduled" && child.visible)
                    found.push(child)
                walk(child)
            }
        }
        walk(page)
        return found
    }

    function test_cancel_needs_a_second_tap() {
        tryVerify(function () { return testCase.cancelButtons().length === 1 })
        const button = testCase.cancelButtons()[0]

        mouseClick(button, button.width / 2, button.height / 2)
        compare(Scheduled.lastCancelled, "")
        compare(page.confirmingId, "w-1")

        wait(Qt.styleHints.mouseDoubleClickInterval + 50) // two taps, not a double-click
        mouseClick(button, button.width / 2, button.height / 2)
        compare(Scheduled.lastCancelled, "w-1")
    }

    function test_locking_drops_a_pending_confirmation() {
        tryVerify(function () { return testCase.cancelButtons().length === 1 })
        const button = testCase.cancelButtons()[0]
        mouseClick(button, button.width / 2, button.height / 2)
        AppLock.locked = true
        compare(page.confirmingId, "")
        compare(Scheduled.lastCancelled, "")
    }
}
