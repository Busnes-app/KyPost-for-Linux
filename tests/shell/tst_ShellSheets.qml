import QtQuick
import QtTest
import com.kysecurity.mail 1.0

// The Scheduled and PGP sheets are plain Popups. Without focus a Popup
// ignores Escape and Tab walks the dimmed page behind it.
TestCase {
    name: "ShellSheets"
    when: windowShown

    property var shell: null

    function initTestCase() {
        const component = Qt.createComponent("qrc:/qml/AppShell.qml")
        compare(component.status, Component.Ready, component.errorString())
        shell = component.createObject(null, {width: 412, height: 732})
        shell.show()
        waitForRendering(shell.contentItem)
    }

    function cleanupTestCase() { shell.destroy() }

    function escapeCloses(open, isOpen) {
        open()
        tryVerify(() => shell[isOpen])
        keyClick(Qt.Key_Escape)
        tryVerify(() => !shell[isOpen])
    }

    function test_scheduled_closes_on_escape() { escapeCloses(shell.openScheduled, "scheduledOpen") }
    function test_my_qr_closes_on_escape() { escapeCloses(shell.openPgpMyQrCode, "pgpMyQrCodeOpen") }
    function test_scan_closes_on_escape() { escapeCloses(shell.openPgpScan, "pgpScanOpen") }
}
