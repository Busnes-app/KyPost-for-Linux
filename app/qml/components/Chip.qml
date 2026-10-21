import QtQuick
import com.kysecurity.mail 1.0

// A stadium filter chip with a leading dot: accent fill when checked, panel
// with a line stroke otherwise (Android's keyword chips, 120ms fade).
Rectangle {
    id: root

    property string text: ""
    property bool checked: false
    signal clicked()

    implicitWidth: row.implicitWidth + 28
    implicitHeight: Math.max(36, label.implicitHeight + 14)
    radius: height / 2
    color: checked ? Theme.accent : Theme.panel
    border.width: checked ? 0 : 1
    border.color: Theme.line

    Behavior on color { ColorAnimation { duration: 120 } }

    Accessible.role: Accessible.RadioButton
    Accessible.name: root.text
    Accessible.checked: root.checked
    Accessible.onPressAction: if (!AppLock.locked) root.clicked()

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 8

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: 8
            height: 8
            radius: 4
            color: root.checked ? Theme.readableOnAccent : Theme.accent
        }
        Text {
            id: label
            anchors.verticalCenter: parent.verticalCenter
            textFormat: Text.PlainText
            text: root.text
            color: root.checked ? Theme.readableOnAccent : Theme.inkStrong
            font.family: Theme.fontUi
            font.pixelSize: Theme.fontSender
            font.weight: Font.Medium
            Behavior on color { ColorAnimation { duration: 120 } }
        }
    }

    activeFocusOnTab: true
    Keys.onReturnPressed: if (!AppLock.locked) root.clicked()
    Keys.onSpacePressed: if (!AppLock.locked) root.clicked()
    FocusRing { cornerRadius: root.radius }

    TapHandler { onTapped: root.clicked() }
}
