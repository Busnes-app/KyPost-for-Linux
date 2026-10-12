import QtQuick 2.15
import QtTest 1.15
import com.kysecurity.mail 1.0
import "qrc:/qml/components" as Components

// The "Move to…" picker: never offers the mailbox the message is already in,
// keeps nesting, and moves nothing unless a folder is tapped.
TestCase {
    id: testCase
    name: "MoveToFolderDialog"
    when: windowShown
    visible: true
    width: 480
    height: 480

    readonly property var folders: [
        { wireName: "INBOX", displayName: "Inbox", depth: 0 },
        { wireName: "Archive", displayName: "Archive", depth: 0 },
        { wireName: "Archive/Projects", displayName: "Projects", depth: 1 },
        { wireName: "Archive/Projects/2026", displayName: "2026", depth: 2 }
    ]

    Components.MoveToFolderDialog {
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

    function targets() {
        const found = []
        const walk = function (item) {
            for (let i = 0; i < item.children.length; i++) {
                const child = item.children[i]
                if (child.objectName === "moveTarget")
                    found.push(child)
                walk(child)
            }
        }
        walk(dialog)
        return found
    }

    function test_the_current_folder_is_not_offered() {
        dialog.open(["m-1"], "INBOX", testCase.folders)

        verify(dialog.isOpen)
        compare(dialog.folders.length, 3)
        for (let i = 0; i < dialog.folders.length; i++)
            verify(dialog.folders[i].wireName !== "INBOX")
        compare(chosenSpy.count, 0)
    }

    function test_nested_folders_are_offered_with_their_depth() {
        dialog.open(["m-1"], "Archive/Projects", testCase.folders)

        compare(dialog.folders.map(f => f.wireName), ["INBOX", "Archive", "Archive/Projects/2026"])
        tryVerify(function () { return testCase.targets().length === 3 })
    }

    function test_tapping_a_folder_moves_the_messages_there() {
        dialog.open(["m-1", "m-2"], "INBOX", testCase.folders)
        tryVerify(function () { return testCase.targets().length === 3 })

        const nested = testCase.targets()[2]
        mouseClick(nested, nested.width / 2, nested.height / 2)

        compare(chosenSpy.count, 1)
        compare(chosenSpy.signalArguments[0][0], ["m-1", "m-2"])
        compare(chosenSpy.signalArguments[0][1], "Archive/Projects/2026")
        verify(!dialog.isOpen)
    }

    function test_cancel_and_backdrop_move_nothing() {
        dialog.open(["m-1"], "INBOX", testCase.folders)
        mouseClick(dialog, 4, 4)
        verify(!dialog.isOpen)

        dialog.open(["m-1"], "INBOX", testCase.folders)
        keyClick(Qt.Key_Escape)
        verify(!dialog.isOpen)

        compare(chosenSpy.count, 0)
    }

    // A picker left open when the app locks sits under the PIN overlay. Locking
    // closes it, and one opened under the lock anyway moves nothing.
    function test_locking_closes_the_picker_and_moves_nothing() {
        dialog.open(["m-1"], "INBOX", testCase.folders)
        AppLock.locked = true
        verify(!dialog.isOpen)

        dialog.open(["m-1"], "INBOX", testCase.folders)
        tryVerify(function () { return testCase.targets().length === 3 })
        const target = testCase.targets()[0]
        mouseClick(target, target.width / 2, target.height / 2)
        dialog.choose("Archive")
        compare(chosenSpy.count, 0)

        // After unlock, a freshly opened picker moves again.
        AppLock.locked = false
        dialog.open(["m-1"], "INBOX", testCase.folders)
        dialog.choose("Archive")
        compare(chosenSpy.count, 1)
    }
}
