import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import com.kysecurity.mail 1.0

// Create / rename / delete prompt shared by both roots. The host makes the
// MailApp call on the *Requested signals and reports back with finished():
// the dialog stays open on failure so the typed name and the reason are kept.
//
// The host offers rename/delete only for folders with deletable == true (the
// server refuses built-in and top-level mailboxes); this file does not
// second-guess that, it just never deletes without the explicit button.
//
// An overlay Item, not a Popup: z-order alone keeps it under the lock gate.
Item {
    id: root

    property bool isOpen: false
    property string mode: "create" // "create" | "rename" | "delete"
    property string targetFolder: ""
    property string displayName: ""
    property string errorText: ""
    property bool busy: false
    property alias nameText: nameField.text

    signal createRequested(string parent, string name)
    signal renameRequested(string folder, string name)
    signal deleteRequested(string folder)

    function openCreate(parentWireName, parentDisplayName) {
        show("create", parentWireName, parentDisplayName, "")
    }

    function openRename(wireName, folderDisplayName) {
        show("rename", wireName, folderDisplayName, folderDisplayName)
    }

    function openDelete(wireName, folderDisplayName) {
        show("delete", wireName, folderDisplayName, "")
    }

    function show(newMode, folder, name, initialText) {
        root.mode = newMode
        root.targetFolder = folder
        root.displayName = name
        root.nameText = initialText
        root.errorText = ""
        root.isOpen = true
        if (newMode === "delete")
            panel.forceActiveFocus()
        else
            nameField.inputField.forceActiveFocus()
    }

    function close() {
        root.isOpen = false
        root.targetFolder = ""
        root.errorText = ""
    }

    function submit() {
        if (!root.isOpen || root.busy)
            return
        if (root.mode === "delete") {
            root.deleteRequested(root.targetFolder)
            return
        }
        const name = root.nameText.trim()
        if (name.length === 0)
            return
        if (root.mode === "create")
            root.createRequested(root.targetFolder, name)
        else
            root.renameRequested(root.targetFolder, name)
    }

    function finished(ok, reason) {
        if (!root.isOpen)
            return
        if (ok)
            root.close()
        else
            root.errorText = reason
    }

    visible: root.isOpen
    anchors.fill: parent

    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.4)

        TapHandler {
            onTapped: root.close()
        }
    }

    Rectangle {
        id: panel
        anchors.centerIn: parent
        width: Math.min(380, root.width - 32)
        implicitHeight: content.implicitHeight + 40
        radius: Theme.shapeSheet
        color: Theme.panel
        border.width: 1
        border.color: Theme.line

        Keys.onEscapePressed: function (event) {
            root.close()
            event.accepted = true
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            onWheel: function (wheel) { wheel.accepted = true }
        }

        ColumnLayout {
            id: content
            anchors.fill: parent
            anchors.margins: 20
            spacing: 12

            Text {
                objectName: "folderDialogHeading"
                Layout.fillWidth: true
                textFormat: Text.PlainText
                text: root.mode === "create" ? i18n("New folder in %1", root.displayName)
                    : root.mode === "rename" ? i18n("Rename folder")
                    : i18n("Delete “%1”?", root.displayName)
                color: Theme.inkStrong
                font.family: Theme.fontUi
                font.pixelSize: 14
                font.bold: true
                wrapMode: Text.WordWrap
            }

            Text {
                Layout.fillWidth: true
                visible: root.mode === "delete"
                textFormat: Text.PlainText
                text: i18n("The folder and any messages in it are removed from the server. This cannot be undone.")
                color: Theme.ink
                font.family: Theme.fontUi
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }

            ThemedTextField {
                id: nameField
                objectName: "folderNameField"
                Layout.fillWidth: true
                visible: root.mode !== "delete"
                placeholderText: i18n("Folder name")
            }

            Connections {
                target: nameField.inputField
                function onAccepted() { root.submit() }
            }

            Text {
                objectName: "folderDialogError"
                Layout.fillWidth: true
                visible: root.errorText.length > 0
                textFormat: Text.PlainText
                text: root.errorText
                color: Theme.dangerColor
                font.family: Theme.fontUi
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Item { Layout.fillWidth: true }
                GhostButton {
                    text: i18n("Cancel")
                    onClicked: root.close()
                }
                PrimaryButton {
                    objectName: "folderDialogSubmit"
                    visible: root.mode !== "delete"
                    text: root.mode === "create" ? i18n("Create") : i18n("Rename")
                    enabled: root.nameText.trim().length > 0 && !root.busy
                    onClicked: root.submit()
                }
                DangerButton {
                    objectName: "folderDialogDelete"
                    visible: root.mode === "delete"
                    text: i18n("Delete")
                    enabled: !root.busy
                    onClicked: root.submit()
                }
            }
        }
    }
}
