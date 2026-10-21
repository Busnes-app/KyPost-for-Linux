import QtQuick
import QtQuick.Layouts
import com.kysecurity.mail 1.0

// The five destinations, as Android: a bottom bar on narrow windows, a left
// rail from 600px (see utils/layout.js). Lock is an action, not a place.
Rectangle {
    id: root

    property bool rail: false
    property string current: "inbox"
    signal activated(string destination)

    readonly property var destinations: [
        {key: "inbox", label: i18n("Inbox"), icon: "inbox"},
        {key: "compose", label: i18n("Compose"), icon: "edit"},
        {key: "lock", label: i18n("Lock"), icon: "lock"},
        {key: "contacts", label: i18n("Contacts"), icon: "person"},
        {key: "settings", label: i18n("Settings"), icon: "settings"},
    ]

    implicitWidth: rail ? 88 : 360
    implicitHeight: rail ? 400 : 80
    color: Theme.panel

    Rectangle {
        // hairline toward the content
        color: Theme.line
        width: root.rail ? 1 : parent.width
        height: root.rail ? parent.height : 1
        anchors.right: root.rail ? parent.right : undefined
        anchors.top: parent.top
    }

    // The rail stacks from the top, under the header; the bar shares its width.
    GridLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: root.rail ? 104 : 0
        height: root.rail ? implicitHeight : parent.height
        flow: root.rail ? GridLayout.TopToBottom : GridLayout.LeftToRight
        rows: root.rail ? root.destinations.length : 1
        columns: root.rail ? 1 : root.destinations.length
        rowSpacing: 12
        columnSpacing: 0

        Repeater {
            model: root.destinations

            delegate: Item {
                id: cell
                required property var modelData
                readonly property bool active: root.current === modelData.key
                readonly property bool available: modelData.key !== "lock" || AppLock.lockEnabled
                objectName: "nav-" + modelData.key

                Layout.fillWidth: true
                Layout.fillHeight: !root.rail
                Layout.preferredHeight: root.rail ? 64 : -1
                Layout.alignment: Qt.AlignTop
                opacity: available ? 1 : 0.4

                Accessible.role: Accessible.PageTab
                Accessible.name: modelData.label
                Accessible.checked: active
                Accessible.onPressAction: if (available && !AppLock.locked) root.activated(modelData.key)

                Column {
                    anchors.centerIn: parent
                    spacing: 4

                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 56
                        height: 30
                        radius: 15
                        color: "transparent"

                        MaterialIcon {
                            anchors.centerIn: parent
                            name: cell.modelData.icon
                            tint: cell.active ? Theme.inkStrong : Theme.ink
                        }
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        textFormat: Text.PlainText
                        text: cell.modelData.label
                        color: cell.active ? Theme.inkStrong : Theme.ink
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fontDate
                        font.weight: cell.active ? Font.Medium : Font.Normal
                    }
                }

                TapHandler {
                    enabled: cell.available
                    onTapped: root.activated(cell.modelData.key)
                }
            }
        }
    }
}
