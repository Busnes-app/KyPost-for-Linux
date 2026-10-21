import QtQuick
import QtTest
import com.kysecurity.mail 1.0
import "qrc:/qml/components" as Components
import "qrc:/qml/utils/layout.js" as Breakpoints

// Android's window-size classes: a bottom bar below 600px, a rail from 600,
// list | detail side by side from 840 with the list at 0.4.
TestCase {
    id: testCase
    name: "Breakpoints"
    when: windowShown
    visible: true
    width: 900
    height: 600

    Components.NavBar {
        id: bar
        width: 412
        height: implicitHeight
    }

    SignalSpy {
        id: activated
        target: bar
        signalName: "activated"
    }

    function read(path) {
        var source = new XMLHttpRequest()
        source.open("GET", path, false)
        source.send()
        compare(source.status, 200, path)
        return source.responseText
    }

    function init() { bar.rail = false; activated.clear() }

    function test_size_classes_switch_at_600_and_840() {
        compare(Breakpoints.mode(320), "compact")
        compare(Breakpoints.mode(599), "compact")
        compare(Breakpoints.mode(600), "medium")
        compare(Breakpoints.mode(839), "medium")
        compare(Breakpoints.mode(840), "expanded")
        compare(Breakpoints.mode(1920), "expanded")
        compare(Breakpoints.listRatio, 0.4)
    }

    function test_bar_lays_destinations_side_by_side() {
        const inbox = findChild(bar, "nav-inbox")
        const settings = findChild(bar, "nav-settings")
        verify(inbox && settings)
        compare(inbox.y, settings.y)
        verify(settings.x > inbox.x)
        compare(bar.implicitHeight, 80)
    }

    function test_rail_stacks_destinations() {
        bar.rail = true
        waitForRendering(bar)
        const inbox = findChild(bar, "nav-inbox")
        const settings = findChild(bar, "nav-settings")
        compare(inbox.x, settings.x)
        verify(settings.y > inbox.y)
        compare(bar.implicitWidth, 88)
    }

    function test_tapping_a_destination_reports_it() {
        mouseClick(findChild(bar, "nav-contacts"))
        compare(activated.count, 1)
        compare(activated.signalArguments[0][0], "contacts")
    }

    // The root needs every C++ singleton, so its wiring is read from the source.
    function test_shell_follows_the_window_width() {
        const text = read("qrc:/qml/AppShell.qml")
        verify(text.indexOf("readonly property string layoutMode: Breakpoints.mode(width)") !== -1)
        verify(text.indexOf('visible: root.layoutMode !== "compact"\n            rail: true') !== -1)
        verify(text.indexOf('visible: root.layoutMode === "compact" &&') !== -1)
        verify(text.indexOf("? Math.round(parent.width * Breakpoints.listRatio) : parent.width") !== -1)
        compare(text.indexOf("minimumWidth: 900"), -1)
    }
}
