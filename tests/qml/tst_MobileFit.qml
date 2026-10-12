import QtQuick
import QtTest
import org.kde.kirigami as Kirigami
import "qrc:/qml/components" as Components

// Phone-width layout and theming of the mobile root's building blocks.
TestCase {
    name: "MobileFit"
    when: windowShown
    visible: true
    width: 360
    height: 200

    Components.IconButton { id: iconButton; icon: "view-refresh"; tooltip: "Refresh" }
    Components.GhostButton { id: ghost; width: 120; text: "Folder: A very long archive subfolder name" }
    Components.ThemedPage { id: page; width: 200; height: 100 }

    function read(path) {
        var source = new XMLHttpRequest()
        source.open("GET", path, false)
        source.send()
        compare(source.status, 200, path)
        return source.responseText
    }

    function test_icon_button_is_named_for_screen_readers() {
        compare(iconButton.Accessible.role, Accessible.Button)
        compare(iconButton.Accessible.name, "Refresh")
    }

    function test_ghost_button_label_elides_inside_its_width() {
        var label = null
        for (var i = 0; i < ghost.children.length; ++i)
            if (ghost.children[i].elide !== undefined)
                label = ghost.children[i]
        verify(label !== null)
        verify(ghost.implicitWidth > ghost.width)
        compare(label.width, ghost.width - ghost.horizontalPadding * 2)
        verify(label.truncated)
    }

    // FakeTheme paints every role #202020, which no platform palette uses.
    function test_themed_page_paints_the_app_theme() {
        compare(page.background.color, Qt.color("#202020"))
        compare(page.Kirigami.Theme.backgroundColor, Qt.color("#202020"))
        compare(page.Kirigami.Theme.textColor, Qt.color("#202020"))
    }

    function test_mobile_root_uses_themed_pages_and_a_compact_header() {
        var text = read("qrc:/qml/MobileRoot.qml")
        compare(text.indexOf("Kirigami.Page {"), -1)
        verify(text.indexOf("pageStack.globalToolBar.style: Kirigami.ApplicationHeaderStyle.ToolBar") !== -1)
        verify(text.indexOf('text: i18n("Refresh")') === -1)
        verify(text.indexOf('tooltip: i18n("Refresh")') !== -1)
    }
}
