import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.kysecurity.mail 1.0

// "Snooze until": Android's presets plus a typed date and time. The host
// passes MailApp.snoozePresets() and MailApp.snoozeCustomTime, and snoozes on
// chosen(); this file never calls MailApp.
//
// An overlay Item, not a Popup, like MoveToFolderDialog: z-order alone keeps
// it under the lock gate.
Item {
    id: root

    property bool isOpen: false
    property var messageIds: []
    // [{kind, at}] from MailApp.snoozePresets().
    property var presets: []
    // (date, time) -> Date, or null when refused.
    property var parseCustom: function (date, time) { return null }
    property string customError: ""

    signal chosen(var messageIds, var at)

    function open(ids, presetList) {
        root.messageIds = ids
        root.presets = presetList
        const soon = new Date(Date.now() + 3600 * 1000)
        dateField.text = Qt.formatDate(soon, "yyyy-MM-dd")
        timeField.text = Qt.formatTime(soon, "HH:mm")
        root.customError = ""
        root.isOpen = true
        panel.forceActiveFocus()
    }

    function close() {
        root.isOpen = false
        root.messageIds = []
        root.presets = []
    }

    function choose(at) {
        if (!root.isOpen || AppLock.locked)
            return
        const ids = root.messageIds
        root.close()
        root.chosen(ids, at)
    }

    function chooseCustom() {
        const at = root.parseCustom(dateField.text, timeField.text)
        if (!at) {
            root.customError = i18n("That time is in the past or too far ahead")
            return
        }
        root.choose(at)
    }

    function presetLabel(preset) {
        const at = preset.at
        let when = at.toLocaleTimeString(Qt.locale(), Locale.ShortFormat)
        if (at.toDateString() !== new Date().toDateString())
            when = Qt.locale().toString(at, "ddd") + " " + when
        switch (preset.kind) {
        case "laterToday": return i18n("Later today, %1", when)
        case "thisEvening": return i18n("This evening, %1", when)
        case "tomorrow": return i18n("Tomorrow, %1", when)
        case "nextWeek": return i18n("Next week, %1", when)
        }
        return when
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

        // A MouseArea, not a TapHandler: handlers also see presses inside
        // the panel, so a refused custom time would close the picker.
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
                text: i18n("Snooze until")
                color: Theme.inkStrong
                font.family: Theme.fontUi
                font.pixelSize: Math.round(16 * Theme.fontScale)
                font.bold: true
            }

            Repeater {
                model: root.presets

                delegate: Rectangle {
                    objectName: "snoozePreset"
                    Layout.fillWidth: true
                    implicitHeight: presetText.implicitHeight + 16
                    radius: Theme.shapeButton
                    color: presetTap.pressed ? Theme.bg : "transparent"

                    Text {
                        id: presetText
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.leftMargin: 10
                        textFormat: Text.PlainText
                        text: root.presetLabel(modelData)
                        color: Theme.inkStrong
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fontSender
                        elide: Text.ElideRight
                    }

                    TapHandler {
                        id: presetTap
                        onTapped: root.choose(modelData.at)
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                ThemedTextField {
                    id: dateField
                    objectName: "snoozeDate"
                    Layout.fillWidth: true
                    implicitWidth: 120
                    placeholderText: i18n("YYYY-MM-DD")
                }
                ThemedTextField {
                    id: timeField
                    objectName: "snoozeTime"
                    implicitWidth: 80
                    placeholderText: i18n("HH:MM")
                }
            }

            Text {
                Layout.fillWidth: true
                visible: root.customError !== ""
                textFormat: Text.PlainText
                text: root.customError
                color: Theme.dangerColor
                font.family: Theme.fontUi
                font.pixelSize: Theme.fontPreview
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 8

                GhostButton {
                    text: i18n("Cancel")
                    onClicked: root.close()
                }
                PrimaryButton {
                    objectName: "snoozeCustom"
                    text: i18n("Snooze")
                    onClicked: root.chooseCustom()
                }
            }
        }
    }
}
