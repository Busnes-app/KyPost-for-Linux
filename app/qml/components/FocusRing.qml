import QtQuick
import com.kysecurity.mail 1.0

// The keyboard focus outline: 2px accent just outside the parent, shown only
// while the parent has active focus. Pass the parent's corner radius.
Rectangle {
    property real cornerRadius: 0

    anchors.fill: parent
    anchors.margins: -3
    radius: cornerRadius + 3
    color: "transparent"
    border.width: 2
    border.color: Theme.accent
    visible: parent && parent.activeFocus
}
