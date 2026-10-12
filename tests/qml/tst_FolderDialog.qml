import QtQuick 2.15
import QtTest 1.15
import com.kysecurity.mail 1.0
import "qrc:/qml/components" as Components

// The create/rename/delete prompt both roots share. Delete must take the
// explicit Delete button, and a failure must keep the dialog and say why.
TestCase {
    id: testCase
    name: "FolderDialog"
    when: windowShown
    visible: true
    width: 480
    height: 360

    Components.FolderDialog {
        id: dialog
        anchors.fill: parent
    }

    SignalSpy { id: createSpy; target: dialog; signalName: "createRequested" }
    SignalSpy { id: renameSpy; target: dialog; signalName: "renameRequested" }
    SignalSpy { id: deleteSpy; target: dialog; signalName: "deleteRequested" }

    function init() {
        AppLock.locked = false
        dialog.close()
        dialog.busy = false
        createSpy.clear()
        renameSpy.clear()
        deleteSpy.clear()
    }

    function child(name) {
        return findChild(dialog, name)
    }

    function test_create_sends_the_parent_and_trimmed_name() {
        dialog.openCreate("Archive", "Archive")
        compare(child("folderDialogHeading").text, "New folder in Archive")
        dialog.nameText = "  Projects  "
        dialog.submit()

        compare(createSpy.count, 1)
        compare(createSpy.signalArguments[0][0], "Archive")
        compare(createSpy.signalArguments[0][1], "Projects")
        // Open until the host reports success.
        verify(dialog.isOpen)
        dialog.finished(true, "")
        verify(!dialog.isOpen)
    }

    function test_rename_prefills_and_sends_the_folder() {
        dialog.openRename("Archive/Old", "Old")
        compare(dialog.nameText, "Old")
        dialog.nameText = "New"
        keyClick(Qt.Key_Return)

        compare(renameSpy.count, 1)
        compare(renameSpy.signalArguments[0][0], "Archive/Old")
        compare(renameSpy.signalArguments[0][1], "New")
    }

    function test_an_empty_name_sends_nothing() {
        dialog.openCreate("Archive", "Archive")
        dialog.nameText = "   "
        dialog.submit()
        verify(!child("folderDialogSubmit").enabled)
        compare(createSpy.count, 0)
    }

    function test_delete_needs_the_delete_button() {
        dialog.openDelete("Archive/Old", "Old")
        compare(child("folderDialogHeading").text, "Delete “Old”?")
        verify(!child("folderNameField").visible)

        // Return and a click on the panel do not delete; Escape cancels.
        keyClick(Qt.Key_Return)
        mouseClick(dialog, dialog.width / 2, dialog.height / 2)
        compare(deleteSpy.count, 0)

        const button = child("folderDialogDelete")
        mouseClick(button, button.width / 2, button.height / 2)
        compare(deleteSpy.count, 1)
        compare(deleteSpy.signalArguments[0][0], "Archive/Old")
    }

    function test_escape_and_backdrop_delete_nothing() {
        dialog.openDelete("Archive/Old", "Old")
        keyClick(Qt.Key_Escape)
        verify(!dialog.isOpen)

        dialog.openDelete("Archive/Old", "Old")
        mouseClick(dialog, 4, 4)
        verify(!dialog.isOpen)
        compare(deleteSpy.count, 0)
    }

    function test_busy_blocks_a_second_request() {
        dialog.openDelete("Archive/Old", "Old")
        dialog.busy = true
        verify(!child("folderDialogDelete").enabled)
        dialog.submit()
        compare(deleteSpy.count, 0)
    }

    function test_a_failure_keeps_the_dialog_and_shows_why() {
        dialog.openCreate("Archive", "Archive")
        dialog.nameText = "Projects"
        dialog.submit()
        dialog.finished(false, "folder already exists")

        verify(dialog.isOpen)
        compare(dialog.nameText, "Projects")
        verify(child("folderDialogError").visible)
        compare(child("folderDialogError").text, "folder already exists")

        // A stale error does not follow the dialog into its next opening.
        dialog.close()
        dialog.openRename("Archive/Projects", "Projects")
        verify(!child("folderDialogError").visible)
    }

    // A prompt left open when the app locks sits under the PIN overlay with
    // keyboard focus. Locking must close it, and Return must send nothing.
    function test_locking_closes_the_prompt_and_return_sends_nothing() {
        dialog.openRename("Archive/Old", "Old")
        dialog.nameText = "New"
        AppLock.locked = true
        verify(!dialog.isOpen)
        keyClick(Qt.Key_Return)
        dialog.submit()
        compare(renameSpy.count, 0)

        // Reopened under the lock (a host that ignored isOpen): still nothing.
        dialog.openCreate("Archive", "Archive")
        dialog.nameText = "Projects"
        keyClick(Qt.Key_Return)
        dialog.submit()
        compare(createSpy.count, 0)
        AppLock.locked = false
    }
}
