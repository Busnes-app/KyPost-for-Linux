import QtQuick
import com.kysecurity.mail 1.0

// A rounded panel card. The two offset rings stand in for Android's 4dp
// card elevation without pulling in QtQuick.Effects.
Item {
    id: root

    default property alias content: body.data
    property color color: Theme.panel
    property int radius: Theme.shapePanel
    property bool elevated: true
    property alias border: face.border
    readonly property alias face: face

    Rectangle {
        visible: root.elevated
        anchors.fill: parent
        anchors.topMargin: 2
        anchors.bottomMargin: -3
        radius: root.radius + 1
        color: Qt.rgba(0, 0, 0, Theme.isLight ? 0.05 : 0.22)
    }
    Rectangle {
        visible: root.elevated
        anchors.fill: parent
        anchors.topMargin: 1
        anchors.bottomMargin: -1
        radius: root.radius
        color: Qt.rgba(0, 0, 0, Theme.isLight ? 0.07 : 0.3)
    }
    Rectangle {
        id: face
        anchors.fill: parent
        radius: root.radius
        color: root.color
        border.width: root.elevated ? 0 : 1
        border.color: Theme.line

        Behavior on color { ColorAnimation { duration: 120 } }
    }
    Item {
        id: body
        anchors.fill: parent
    }
}
