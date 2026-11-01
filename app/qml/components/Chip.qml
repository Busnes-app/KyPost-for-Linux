import QtQuick
import com.kysecurity.mail 1.0

// A stadium filter chip: accent-soft with an accent edge and ink text when
// checked (never accent text on accent-soft), page surface with a line
// stroke otherwise. No dot: accent dots mark status and unread only.
Rectangle {
    id: root

    property string text: ""
    property bool checked: false
    signal clicked()

    implicitWidth: label.implicitWidth + 28
    implicitHeight: Math.max(32, label.implicitHeight + 12)
    radius: height / 2
    color: checked ? Theme.accentSoft : (hover.hovered ? Qt.rgba(Theme.ink.r, Theme.ink.g, Theme.ink.b, 0.06) : "transparent")
    border.width: 1
    border.color: checked ? Theme.accent : Theme.line

    Behavior on color { ColorAnimation { duration: 120 } }

    Accessible.role: Accessible.RadioButton
    Accessible.name: root.text
    Accessible.checked: root.checked
    Accessible.onPressAction: if (!AppLock.locked) root.clicked()

    Text {
        id: label
        anchors.centerIn: parent
        textFormat: Text.PlainText
        text: root.text
        color: Theme.inkStrong
        font.family: Theme.fontUi
        font.pixelSize: Theme.fontSender
        font.weight: Font.Medium
    }

    activeFocusOnTab: true
    Keys.onReturnPressed: if (!AppLock.locked) root.clicked()
    Keys.onSpacePressed: if (!AppLock.locked) root.clicked()
    FocusRing { cornerRadius: root.radius }

    HoverHandler { id: hover }
    TapHandler { onTapped: root.clicked() }
}
