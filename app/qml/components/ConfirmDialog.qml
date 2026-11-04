import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.kysecurity.mail 1.0

// A yes/no question: a title, an optional plain-text message, two buttons,
// or three with `alternateText` set (Android's neutral button).
// Android's AlertDialog with a positive and a negative button.
//
// An overlay Item, not a Popup, like FolderDialog.qml: z-order alone keeps it
// under the lock gate. Locking closes it as a refusal, so nothing it asks
// about can be accepted from under the PIN screen. The scrim and Escape
// refuse too; accepting needs a press on its own button, and no button takes
// focus, so no keystroke can accept.
Item {
    id: root

    property bool isOpen: false
    property string title: ""
    property string message: ""
    property string acceptText: i18n("OK")
    property string rejectText: i18n("Cancel")
    property string alternateText: ""
    // An optional checkbox under the message (unpair's "Also remove this
    // device's contacts"); off on every open.
    property string checkText: ""
    property bool checked: false

    signal accepted()
    signal rejected()
    signal alternated()

    function open() {
        root.checked = false
        root.isOpen = true
        panel.forceActiveFocus()
    }

    // Closes before emitting, so a handler may open it again.
    function accept() {
        if (!root.isOpen || AppLock.locked)
            return
        root.isOpen = false
        root.accepted()
    }

    function alternate() {
        if (!root.isOpen || AppLock.locked)
            return
        root.isOpen = false
        root.alternated()
    }

    function reject() {
        if (!root.isOpen)
            return
        root.isOpen = false
        root.rejected()
    }

    Connections {
        target: AppLock
        function onLockedChanged() {
            if (AppLock.locked)
                root.reject()
        }
    }

    visible: root.isOpen
    anchors.fill: parent

    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.4)

        TapHandler {
            onTapped: root.reject()
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
            root.reject()
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
                objectName: "confirmDialogTitle"
                Layout.fillWidth: true
                textFormat: Text.PlainText
                text: root.title
                color: Theme.inkStrong
                font.family: Theme.fontUi
                font.pixelSize: Math.round(16 * Theme.fontScale)
                font.weight: Font.Medium
                wrapMode: Text.WordWrap
            }
            Text {
                objectName: "confirmDialogMessage"
                Layout.fillWidth: true
                visible: root.message !== ""
                textFormat: Text.PlainText
                text: root.message
                color: Theme.ink
                font.family: Theme.fontUi
                font.pixelSize: Theme.fontSender
                wrapMode: Text.WordWrap
            }
            CheckBox {
                objectName: "confirmDialogCheck"
                Layout.fillWidth: true
                visible: root.checkText !== ""
                text: root.checkText
                checked: root.checked
                onToggled: root.checked = checked
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Item { Layout.fillWidth: true }
                GhostButton {
                    objectName: "confirmDialogReject"
                    text: root.rejectText
                    onClicked: root.reject()
                }
                GhostButton {
                    objectName: "confirmDialogAlternate"
                    visible: root.alternateText !== ""
                    text: root.alternateText
                    onClicked: root.alternate()
                }
                PrimaryButton {
                    objectName: "confirmDialogAccept"
                    text: root.acceptText
                    onClicked: root.accept()
                }
            }
        }
    }
}
