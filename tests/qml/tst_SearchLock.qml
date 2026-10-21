import QtQuick
import QtTest
import com.kysecurity.mail 1.0
import "qrc:/qml/components" as Components

// A window shortcut fires whatever has focus, so the PIN overlay did not stop
// Find: it put the keyboard into the search field underneath, where the
// retained query could be edited, selected and copied, and searched for,
// before unlock. Locking must leave focus in the PIN field, and nothing typed
// may reach the field or MailApp until unlock.
TestCase {
    id: testCase
    name: "SearchLock"
    when: windowShown
    visible: true
    width: 420
    height: 320

    Components.MailSearchField {
        id: search
        width: testCase.width
        findShortcutEnabled: true
    }

    // Stacked over the field the way the shell stacks it.
    Components.LockOverlay {
        id: overlay
    }

    function init() {
        AppLock.locked = false
        MailApp.resetSearchCounts()
        search.inputField.focus = false
        // Window shortcuts only fire in the focus window.
        const win = testCase.Window.window
        win.requestActivate()
        tryVerify(function () { return win.active }, 5000, "the test window must be active for Find")
    }

    function cleanup() {
        AppLock.locked = false
    }

    function pinField() {
        const walk = function (item) {
            if (item.objectName === "pinField")
                return item
            for (let i = 0; i < item.children.length; i++) {
                const found = walk(item.children[i])
                if (found)
                    return found
            }
            return null
        }
        return overlay.item ? walk(overlay.item) : null
    }

    function typeIn() {
        keyClick("i")
        keyClick("n")
    }

    // The control: without it, every assertion below could pass on a
    // shortcut or key path that never worked at all.
    function test_unlocked_find_focuses_the_field_and_typing_searches() {
        keySequence(StandardKey.Find)
        verify(search.inputField.activeFocus, "Find must reach the field while unlocked")
        typeIn()
        compare(search.text, "in")
        compare(MailApp.searchEdits, 2)
        compare(MailApp.searchQuery, "in")
    }

    function test_locked_search_takes_no_focus_edit_or_request() {
        keySequence(StandardKey.Find)
        typeIn()
        compare(MailApp.searchEdits, 2)

        AppLock.locked = true
        const pin = pinField()
        verify(pin !== null, "the PIN gate must be loaded")
        tryVerify(function () { return pin.inputField.activeFocus }, 2000, "locking must put focus in the PIN field")
        verify(!search.inputField.activeFocus)

        verify(!search.findShortcut.enabled, "Find must be off while locked")
        keySequence(StandardKey.Find)
        verify(pin.inputField.activeFocus, "Find must leave focus in the PIN field")
        verify(!search.inputField.activeFocus)

        // Whatever route Find took, taken directly.
        search.inputField.forceActiveFocus()
        verify(!search.inputField.activeFocus, "a locked search field must not take focus")
        keyClick("x")
        keySequence(StandardKey.SelectAll)
        keySequence(StandardKey.Copy)
        keyClick(Qt.Key_Backspace)
        keyClick(Qt.Key_Escape)

        compare(search.text, "in")
        compare(search.inputField.selectedText, "")
        compare(MailApp.searchEdits, 2, "no query edit may reach MailApp while locked")
        compare(MailApp.searchClears, 0, "Escape must not clear the search under the PIN gate")
        compare(MailApp.searchQuery, "in")

        AppLock.locked = false
        keySequence(StandardKey.Find)
        verify(search.inputField.activeFocus, "unlocking gives Find back")
    }
}
