import QtQuick
import QtTest
import com.kysecurity.mail 1.0
import "qrc:/qml/components" as Components

// Android's field stroke and avatar stops reach the shipped components.
TestCase {
    name: "ThemeTokens"
    when: windowShown
    visible: true
    width: 400
    height: 200

    Components.ThemedTextField {
        id: field
    }

    Components.Avatar {
        id: avatar
        initials: "KP"
    }

    // On white, away from the other items, so a grab sees only the glyph.
    Rectangle {
        id: lockBox
        x: 320
        width: 48
        height: 48
        color: "white"
        Components.MaterialIcon {
            id: lockIcon
            size: 48
            name: "lock"
            tint: "#ff0000"
        }
    }
    Rectangle {
        id: unknownBox
        x: 320
        y: 60
        width: 24
        height: 24
        color: "white"
        Components.MaterialIcon {
            id: unknownIcon
            name: "no-such-icon"
        }
    }

    Components.SectionLabel {
        id: eyebrow
        text: "Accounts"
    }

    function test_field_stroke() {
        compare(Theme.fieldStroke, 2)
        compare(field.border.width, Theme.fieldStroke)
    }

    function test_avatar_stops_and_border() {
        compare(avatar.gradient.stops.length, 2)
        verify(Qt.colorEqual(avatar.gradient.stops[0].color, Theme.avatarStart))
        verify(Qt.colorEqual(avatar.gradient.stops[1].color, Theme.avatarEnd))
        verify(Qt.colorEqual(avatar.border.color, Theme.avatarBorder))
        verify(!Qt.colorEqual(Theme.avatarStart, Theme.avatarEnd))
    }

    function test_eyebrow_type() {
        compare(eyebrow.font.pixelSize, Theme.fontEyebrow)
        compare(eyebrow.font.capitalization, Font.AllUppercase)
        // QFont stores spacing in 1/64 px.
        fuzzyCompare(eyebrow.font.letterSpacing, Theme.eyebrowLetterSpacing, 1 / 64)
    }

    // The glyph carries the tint, never the SVG's own black: a black glyph is
    // invisible on a dark theme.
    function test_material_icon_is_tinted() {
        tryCompare(lockIcon, "status", Image.Ready)
        const image = grabImage(lockBox)
        let tinted = 0
        let black = 0
        for (let y = 0; y < image.height; y += 3) {
            for (let x = 0; x < image.width; x += 3) {
                const c = image.pixel(x, y)
                if (c.r > 0.9 && c.g < 0.1 && c.b < 0.1) ++tinted
                else if (c.r < 0.2 && c.g < 0.2 && c.b < 0.2) ++black
            }
        }
        verify(tinted > 20, "tinted pixels: " + tinted)
        compare(black, 0)
    }

    function test_unknown_icon_draws_nothing() {
        tryCompare(unknownIcon, "status", Image.Ready)
        const image = grabImage(unknownBox)
        const background = image.pixel(0, 0)
        for (let y = 0; y < image.height; y += 2)
            for (let x = 0; x < image.width; x += 2)
                verify(Qt.colorEqual(image.pixel(x, y), background), x + "," + y)
    }
}
