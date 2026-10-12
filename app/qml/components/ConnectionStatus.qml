import QtQuick
import QtQuick.Layouts
import com.kysecurity.mail 1.0

// Push and sync status for Settings -> Connection, plus the notification
// history. Takes values, not singletons, so tests/qml can drive it.
ColumnLayout {
    id: root

    property bool distributorActive: false
    property string pushServerHost: ""
    property bool pushEncrypted: false
    property date lastSyncAt: new Date(NaN)
    property string lastError: ""
    property var history: [] // MailController::notificationHistory()

    signal openRequested(string messageId)

    spacing: 6

    function formatTime(d) {
        return d && !isNaN(d.getTime()) ? Qt.formatDateTime(d, "MMM d, hh:mm") : ""
    }

    readonly property string pushText: !distributorActive
        ? i18n("Checking the relay every 90 seconds")
        : pushServerHost !== "" ? i18n("UnifiedPush via %1", pushServerHost) : i18n("UnifiedPush")
    readonly property string syncText: formatTime(lastSyncAt) || i18n("Not yet this session")

    Repeater {
        model: [
            { "label": i18n("Push"), "value": root.pushText, "name": "pushRow" },
            { "label": i18n("Encrypted"), "value": root.pushEncrypted ? i18n("Yes") : i18n("No"), "name": "encryptedRow" },
            { "label": i18n("Last sync"), "value": root.syncText, "name": "syncRow" },
            { "label": i18n("Last error"), "value": root.lastError, "name": "errorRow" }
        ]
        delegate: RowLayout {
            objectName: modelData.name
            Layout.fillWidth: true
            visible: modelData.value !== ""
            spacing: 12
            SectionLabel { Layout.preferredWidth: 70; text: modelData.label }
            Text {
                objectName: "value"
                Layout.fillWidth: true
                textFormat: Text.PlainText
                text: modelData.value
                color: modelData.name === "errorRow" ? Theme.dangerColor : Theme.inkStrong
                font.family: Theme.fontUi
                font.pixelSize: 14
                wrapMode: Text.WordWrap
            }
        }
    }

    SectionLabel {
        Layout.topMargin: 8
        text: i18n("Recent notifications")
    }
    MutedHint {
        objectName: "historyEmpty"
        visible: root.history.length === 0
        text: i18n("None yet.")
    }
    Repeater {
        objectName: "historyList"
        model: root.history
        delegate: Text {
            Layout.fillWidth: true
            textFormat: Text.PlainText
            text: root.formatTime(modelData.receivedAt) + "  "
                + (modelData.subject !== "" ? modelData.subject : i18n("(message not synced yet)"))
            color: Theme.ink
            font.family: Theme.fontUi
            font.pixelSize: 13
            elide: Text.ElideRight
            TapHandler { onTapped: root.openRequested(modelData.messageId) }
        }
    }
}
