import QtQuick
import org.kde.kirigami as Kirigami
import com.kysecurity.mail 1.0

// A bundled Material Icons (filled) glyph from qrc:/qml/icons, tinted.
Kirigami.Icon {
    property string name: ""
    property color tint: Theme.inkStrong

    implicitWidth: 24
    implicitHeight: 24
    source: name ? "qrc:/qml/icons/" + name + ".svg" : ""
    isMask: true
    // Static glyphs: no crossfade. With it on, some instances painted the
    // untinted black glyph in offscreen renders.
    animated: false
    color: tint
}
