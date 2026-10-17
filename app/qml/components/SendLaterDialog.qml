import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.kysecurity.mail 1.0

// "Send later": Android's presets, or a date and time of the user's own.
// Emits chosen(atMs); the composer sends with it. MailController checks the
// time again when it sends.
//
// An overlay Item, not a Popup, for the same reason as MoveToFolderDialog:
// z-order alone keeps it under the lock gate.
Item {
    id: root

    property bool isOpen: false
    property var presets: []
    property string error: ""

    signal chosen(real atMs)

    function open() {
        root.presets = Scheduled.presets()
        // An hour from now, as a starting point to edit.
        const start = new Date(Date.now() + 3600 * 1000)
        dateField.text = Qt.formatDate(start, "yyyy-MM-dd")
        timeField.text = Qt.formatTime(start, "HH:mm")
        root.error = ""
        root.isOpen = true
        panel.forceActiveFocus()
    }

    function close() {
        root.isOpen = false
        root.presets = []
    }

    function choose(atMs) {
        if (!root.isOpen || AppLock.locked)
            return
        root.close()
        root.chosen(atMs)
    }

    function chooseCustom() {
        const atMs = Scheduled.customTime(dateField.text, timeField.text)
        if (atMs === 0) {
            root.error = i18n("That time is in the past or too far ahead.")
            return
        }
        root.choose(atMs)
    }

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

        // A MouseArea, not a TapHandler: a TapHandler here also saw taps on
        // the panel's buttons and closed the dialog under them.
        MouseArea {
            anchors.fill: parent
            onClicked: root.close()
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
                text: i18n("Send at")
                color: Theme.inkStrong
                font.family: Theme.fontUi
                font.pixelSize: 16
                font.bold: true
            }

            Repeater {
                model: root.presets

                delegate: GhostButton {
                    objectName: "sendLaterPreset"
                    Layout.fillWidth: true
                    text: modelData.label
                    onClicked: root.choose(modelData.atMs)
                }
            }

            Text {
                Layout.fillWidth: true
                textFormat: Text.PlainText
                text: i18n("Or pick a date and time")
                color: Theme.ink
                font.family: Theme.fontUi
                font.pixelSize: 12
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                ThemedTextField {
                    id: dateField
                    objectName: "sendLaterDate"
                    Layout.fillWidth: true
                    implicitWidth: 120
                    placeholderText: "YYYY-MM-DD"
                }
                ThemedTextField {
                    id: timeField
                    objectName: "sendLaterTime"
                    implicitWidth: 80
                    placeholderText: "HH:MM"
                }
            }

            Text {
                Layout.fillWidth: true
                visible: root.error !== ""
                textFormat: Text.PlainText
                text: root.error
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
                    objectName: "sendLaterCustom"
                    text: i18n("Schedule")
                    onClicked: root.chooseCustom()
                }
            }
        }
    }
}
