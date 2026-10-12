import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.kysecurity.mail 1.0

// Folder picker for "Move to…". The host passes MailApp.mailFolders() and
// performs the move on chosen(); this file never calls MailApp.
//
// An overlay Item, not a Popup, for the same reason as
// NotificationFolderDialog: z-order alone keeps it under the lock gate.
Item {
    id: root

    property bool isOpen: false
    property var messageIds: []
    // mailFolders() entries minus the mailbox the messages are in.
    property var folders: []

    signal chosen(var messageIds, string folder)

    function open(ids, fromFolder, allFolders) {
        root.messageIds = ids
        root.folders = allFolders.filter(f => f.wireName !== fromFolder)
        root.isOpen = true
        panel.forceActiveFocus()
    }

    function close() {
        root.isOpen = false
        root.messageIds = []
        root.folders = []
    }

    function choose(folder) {
        if (!root.isOpen || AppLock.locked)
            return
        const ids = root.messageIds
        root.close()
        root.chosen(ids, folder)
    }

    // Same as FolderDialog: the lock gate hides this picker but leaves it
    // focused underneath, so locking closes it. MailController refuses the
    // move anyway.
    Connections {
        target: AppLock
        function onLockedChanged() {
            if (AppLock.locked)
                root.close()
        }
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
        width: Math.min(360, root.width - 32)
        height: Math.min(content.implicitHeight + 32, root.height - 32)
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
            anchors.margins: 16
            spacing: 10

            Text {
                Layout.fillWidth: true
                textFormat: Text.PlainText
                text: i18n("Move to")
                color: Theme.inkStrong
                font.family: Theme.fontUi
                font.pixelSize: 16
                font.bold: true
            }

            ListView {
                id: folderList
                Layout.fillWidth: true
                Layout.fillHeight: true
                implicitHeight: contentHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: root.folders
                ScrollBar.vertical: ThemedScrollBar {}

                delegate: Rectangle {
                    objectName: "moveTarget"
                    width: folderList.width
                    implicitHeight: targetLabel.implicitHeight + 16
                    radius: Theme.shapeButton
                    color: targetTap.pressed ? Theme.bg : "transparent"

                    Text {
                        id: targetLabel
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.leftMargin: 10 + (modelData.depth || 0) * 14
                        textFormat: Text.PlainText
                        text: modelData.displayName
                        color: Theme.inkStrong
                        font.family: Theme.fontUi
                        font.pixelSize: modelData.depth > 0 ? 13 : 14
                        elide: Text.ElideRight
                    }

                    TapHandler {
                        id: targetTap
                        onTapped: root.choose(modelData.wireName)
                    }
                }
            }

            GhostButton {
                Layout.alignment: Qt.AlignRight
                text: i18n("Cancel")
                onClicked: root.close()
            }
        }
    }
}
