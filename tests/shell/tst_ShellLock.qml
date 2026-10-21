import QtQuick
import QtTest
import com.kysecurity.mail 1.0

// QQC2 popups, menus and drawers paint above the lock overlay. Every one the
// shell can show while unlocked must be closed by the lock.
TestCase {
    name: "ShellLock"
    when: windowShown

    property var shell: null

    function initTestCase() {
        const component = Qt.createComponent("qrc:/qml/AppShell.qml")
        compare(component.status, Component.Ready, component.errorString())
        shell = component.createObject(null, {width: 1024, height: 768})
        verify(shell)
        shell.show()
        waitForRendering(shell.contentItem)
    }

    function cleanupTestCase() { shell.destroy() }

    function init() {
        AppLock.locked = false
        Pairing.pairingState = "idle"
        shell.navigate("inbox")
    }

    function cleanup() { AppLock.locked = false }

    function test_folder_drawer_closes_on_lock() {
        shell.openFolders()
        tryVerify(() => shell.foldersOpen)
        AppLock.locked = true
        tryVerify(() => !shell.foldersOpen)
    }

    function test_pairing_confirmation_closes_on_lock() {
        Pairing.pairingState = "confirm"
        tryVerify(() => shell.pairingConfirmOpen)
        AppLock.locked = true
        tryVerify(() => !shell.pairingConfirmOpen)
    }

    function test_settings_popups_close_on_lock() {
        shell.openPairing()
        const settings = findChild(shell.contentItem, "settingsPane")
        verify(settings)
        tryVerify(() => settings.popupOpen)
        AppLock.locked = true
        tryVerify(() => !settings.popupOpen)
    }

    function test_mail_row_menu_closes_on_lock() {
        const list = findChild(shell.contentItem, "emailList")
        verify(list)
        tryVerify(() => list.count > 0)
        const card = list.itemAtIndex(0)
        verify(card)
        card.openMenu()
        tryVerify(() => card.menuOpen)
        AppLock.locked = true
        tryVerify(() => !card.menuOpen)
    }

    function test_contacts_menu_closes_on_lock() {
        shell.navigate("contacts")
        const contacts = findChild(shell.contentItem, "contactsList")
        verify(contacts)
        contacts.openActions(contacts)
        tryVerify(() => contacts.actionsOpen)
        AppLock.locked = true
        tryVerify(() => !contacts.actionsOpen)
    }
}
