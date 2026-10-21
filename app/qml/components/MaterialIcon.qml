import QtQuick
import QtQuick.Window
import com.kysecurity.mail 1.0

// A bundled Material Icons (filled) glyph, tinted by MaterialIconProvider
// (image://icon/<name>/<colour>). The colour is part of the URL, so a theme
// change fetches a freshly tinted image.
Image {
    property string name: ""
    property color tint: Theme.inkStrong

    width: 24
    height: 24
    source: name ? "image://icon/" + name + "/" + tint.toString().slice(1) : ""
    sourceSize: Qt.size(Math.ceil(width * Screen.devicePixelRatio), Math.ceil(height * Screen.devicePixelRatio))
    fillMode: Image.PreserveAspectFit
    smooth: true
    Accessible.ignored: true
}
