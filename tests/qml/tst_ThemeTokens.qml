import QtQuick
import QtTest
import com.kysecurity.mail 1.0
import "qrc:/qml/components" as Components

// Android's field stroke and avatar stops reach the shipped components.
TestCase {
    name: "ThemeTokens"
    when: windowShown
    visible: true
    width: 300
    height: 200

    Components.ThemedTextField {
        id: field
    }

    Components.Avatar {
        id: avatar
        initials: "KP"
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
}
