import QtQuick
import com.kysecurity.mail 1.0

// A rounded panel card: flat, with a 1px line and no drop shadow (the suite
// shell's card rule).
Item {
    id: root

    default property alias content: body.data
    property color color: Theme.panel
    property int radius: Theme.shapePanel
    property alias border: face.border
    readonly property alias face: face

    Rectangle {
        id: face
        anchors.fill: parent
        radius: root.radius
        color: root.color
        border.width: 1
        border.color: Theme.line

        Behavior on color { ColorAnimation { duration: 120 } }
    }
    Item {
        id: body
        anchors.fill: parent
    }
}
