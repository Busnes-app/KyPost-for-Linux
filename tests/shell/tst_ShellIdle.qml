import QtQuick
import QtTest
import com.kysecurity.mail 1.0

// The shell left on screen, as a user opening the app leaves it. With the
// KYPOST_RSS_LIMIT_MB watchdog (tests/CMakeLists.txt) this is the memory
// regression test: a layout or image feedback loop shows up here as a
// steadily growing process that the watchdog aborts.
TestCase {
    name: "ShellIdle"
    when: windowShown

    function idle(width, height) {
        const component = Qt.createComponent("qrc:/qml/AppShell.qml")
        compare(component.status, Component.Ready, component.errorString())
        const shell = component.createObject(null, {width: width, height: height})
        shell.show()
        waitForRendering(shell.contentItem)
        wait(5000)
        verify(shell.visible)
        shell.destroy()
    }

    function test_phone_layout_stays_put() { idle(412, 732) }
    function test_split_layout_stays_put() { idle(1280, 800) }
}
