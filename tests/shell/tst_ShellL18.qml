import QtQuick
import QtTest
import com.kysecurity.mail 1.0

// Plan L18 at the root: the vCard import preview (Android #186) and the
// one-time app-lock offer after pairing (Android #181). Both are questions
// that must not survive the lock or a change of account.
TestCase {
    name: "ShellL18"
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
        AppLock.lockEnabled = true
        AppLock.lockOffered = false
        Pairing.pairingState = "idle"
        ContactsApp.importsConfirmed = 0
        ContactsApp.importsDiscarded = 0
        shell.navigate("inbox")
    }

    function cleanup() {
        AppLock.locked = false
        AppLock.lockEnabled = true
    }

    function importDialog() {
        shell.navigate("contacts")
        const contacts = findChild(shell.contentItem, "contactsList")
        verify(contacts)
        const dialog = findChild(contacts, "vcardImportDialog")
        verify(dialog)
        return [contacts, dialog]
    }

    function test_import_preview_names_the_contacts_and_queues_on_import() {
        const [contacts, dialog] = importDialog()
        contacts.previewImport("file:///tmp/good.vcf")
        tryVerify(() => dialog.visible)
        verify(dialog.message.indexOf("12 contacts") !== -1, dialog.message)
        verify(dialog.message.indexOf("• Ada") !== -1)
        verify(dialog.message.indexOf("10 more") !== -1)
        verify(dialog.message.indexOf("1 card has no name") !== -1)
        mouseClick(findChild(dialog, "confirmDialogAccept"))
        compare(ContactsApp.importsConfirmed, 1)
        verify(!dialog.visible)
    }

    function test_a_refused_file_says_why_and_asks_nothing() {
        const [contacts, dialog] = importDialog()
        contacts.previewImport("file:///tmp/bad.vcf")
        verify(!dialog.visible)
        compare(contacts.importError, "That file isn't a valid vCard.")
    }

    function test_the_lock_discards_an_open_preview() {
        const [contacts, dialog] = importDialog()
        contacts.previewImport("file:///tmp/good.vcf")
        tryVerify(() => dialog.visible)
        AppLock.locked = true
        tryVerify(() => !dialog.visible)
        compare(ContactsApp.importsConfirmed, 0)
        verify(ContactsApp.importsDiscarded >= 1)
    }

    function test_a_pairing_change_discards_the_preview() {
        const [contacts, dialog] = importDialog()
        contacts.previewImport("file:///tmp/good.vcf")
        tryVerify(() => dialog.visible)
        Pairing.pairingChanged()
        tryVerify(() => !dialog.visible)
        compare(ContactsApp.importsConfirmed, 0)
        verify(ContactsApp.importsDiscarded >= 1)
    }

    function test_the_lock_offer_follows_a_pairing_once() {
        AppLock.lockEnabled = false
        const offer = findChild(shell.contentItem, "appLockOfferDialog")
        verify(offer)
        shell.openPairing()
        const settings = findChild(shell.contentItem, "settingsPane")
        tryVerify(() => settings.popupOpen)
        Pairing.pairingState = "paired"
        tryVerify(() => offer.visible)
        // The full-window pairing page is a Popup and would cover the offer.
        tryVerify(() => !settings.popupOpen)
        mouseClick(findChild(offer, "confirmDialogReject")) // "Not now"
        verify(!offer.visible)

        Pairing.pairingState = "idle"
        Pairing.pairingState = "paired"
        wait(50)
        verify(!offer.visible, "the offer was made again after Not now")
    }

    function test_no_offer_while_a_lock_is_set() {
        const offer = findChild(shell.contentItem, "appLockOfferDialog")
        Pairing.pairingState = "paired"
        wait(50)
        verify(!offer.visible)
    }

    function test_accepting_the_offer_opens_security_settings() {
        AppLock.lockEnabled = false
        const offer = findChild(shell.contentItem, "appLockOfferDialog")
        Pairing.pairingState = "paired"
        tryVerify(() => offer.visible)
        mouseClick(findChild(offer, "confirmDialogAccept"))
        verify(!offer.visible)
        const settings = findChild(shell.contentItem, "settingsPane")
        tryCompare(settings, "currentPane", 6)
    }
}
