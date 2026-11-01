import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.kysecurity.mail 1.0
import "../utils/format.js" as Format

// The keyword strip: stadium chips with their unread count, and when the
// folder last synced under it.
ColumnLayout {
    id: root

    spacing: 8

    // On the page surface, no panel behind the strip.
    Flickable {
        Layout.fillWidth: true
        implicitHeight: chips.implicitHeight
        contentWidth: chips.implicitWidth
        contentHeight: chips.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.HorizontalFlick

        Row {
            id: chips
            spacing: 8

            Chip {
                text: Format.withUnread(i18n("All"), MailApp.currentFolderUnread)
                checked: MailApp.selectedKeyword === ""
                onClicked: MailApp.selectKeyword("")
            }
            Repeater {
                model: MailApp.keywordTabs
                delegate: Chip {
                    required property var modelData
                    text: Format.withUnread(modelData.name, modelData.unread)
                    checked: MailApp.selectedKeyword === modelData.name
                    onClicked: MailApp.selectKeyword(modelData.name)
                }
            }
        }
    }

    Text {
        objectName: "lastSyncLine"
        Layout.fillWidth: true
        visible: !isNaN(new Date(MailApp.lastSyncAt).getTime())
        textFormat: Text.PlainText
        readonly property int minutes: Format.minutesSince(MailApp.lastSyncAt, root.now)
        text: minutes < 1 ? i18n("Updated just now")
                          : i18np("Updated %1 minute ago", "Updated %1 minutes ago", minutes)
        color: Theme.ink
        font.family: Theme.fontUi
        font.pixelSize: Theme.fontDate
    }

    // Re-evaluates "N minutes ago" without a model change.
    property date now: new Date()
    Timer {
        interval: 30000
        running: root.visible
        repeat: true
        onTriggered: root.now = new Date()
    }
}
