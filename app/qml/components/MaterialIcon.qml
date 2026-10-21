import QtQuick
import QtQuick.Window
import com.kysecurity.mail 1.0

// A bundled Material Icons (filled) glyph, tinted by MaterialIconProvider
// (image://icon/<name>/<colour>). The colour is part of the URL, so a theme
// change fetches a freshly tinted image.
//
// An Item around the Image, not an Image: an Image's implicit size is its
// pixel size, so with sourceSize = width x devicePixelRatio inside a layout
// every frame grew the icon by the screen scale and rendered it again. At
// 1.6x that ate 3 GB in two seconds. Here the size comes only from `size`.
Item {
    property string name: ""
    property color tint: Theme.inkStrong
    property int size: 24
    readonly property alias status: glyph.status

    implicitWidth: size
    implicitHeight: size
    Accessible.ignored: true

    Image {
        id: glyph
        anchors.fill: parent
        source: parent.name ? "image://icon/" + parent.name + "/" + parent.tint.toString().slice(1) : ""
        sourceSize: Qt.size(Math.ceil(parent.size * Screen.devicePixelRatio),
                            Math.ceil(parent.size * Screen.devicePixelRatio))
        fillMode: Image.PreserveAspectFit
        smooth: true
    }
}
