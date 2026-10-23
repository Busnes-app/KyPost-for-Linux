import QtQuick
import QtQuick.Layouts
import com.kysecurity.mail 1.0

// "Send RSVP unencrypted?" for the card whose token is `token`. Driven by
// Invites.confirmation, which C++ withdraws under the lock; this file only
// answers it. Only the reader showing that card hosts it, so a pop-out and the
// main window never both ask.
//
// An overlay Item, not a Popup, like MoveToFolderDialog: z-order alone keeps
// it under the lock gate.
Item {
    id: root

    // The host's card token, or -1 for none.
    property int token: -1
    readonly property var confirmation: Invites.confirmation
    readonly property bool isOpen: root.token >= 0 && root.confirmation.token === root.token && !AppLock.locked

    function cancel() {
        if (root.isOpen)
            Invites.cancelRsvp(root.token)
    }

    function send() {
        if (root.isOpen)
            Invites.confirmRsvp(root.token)
    }

    visible: root.isOpen
    anchors.fill: parent
    onIsOpenChanged: if (root.isOpen) panel.forceActiveFocus()

    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.4)

        // A MouseArea, not a TapHandler: handlers also see presses inside
        // the panel, which would cancel on every click there.
        MouseArea {
            anchors.fill: parent
            onClicked: root.cancel()
        }
    }

    Rectangle {
        id: panel
        anchors.centerIn: parent
        width: Math.min(420, root.width - 32)
        height: Math.min(content.implicitHeight + 32, root.height - 32)
        radius: Theme.shapeSheet
        color: Theme.panel
        border.width: 1
        border.color: Theme.line

        Keys.onEscapePressed: function (event) {
            root.cancel()
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
            spacing: 12

            Text {
                Layout.fillWidth: true
                textFormat: Text.PlainText
                text: root.confirmation.title || ""
                color: Theme.inkStrong
                font.family: Theme.fontUi
                font.pixelSize: Math.round(16 * Theme.fontScale)
                font.bold: true
                wrapMode: Text.Wrap
            }
            Text {
                objectName: "rsvpConfirmText"
                Layout.fillWidth: true
                textFormat: Text.PlainText
                text: root.confirmation.text || ""
                color: Theme.ink
                font.family: Theme.fontUi
                font.pixelSize: Theme.fontSender
                wrapMode: Text.Wrap
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 8

                GhostButton {
                    objectName: "rsvpCancel"
                    text: i18n("Cancel")
                    onClicked: root.cancel()
                }
                PrimaryButton {
                    objectName: "rsvpSend"
                    text: root.confirmation.sendLabel || ""
                    onClicked: root.send()
                }
            }
        }
    }
}
