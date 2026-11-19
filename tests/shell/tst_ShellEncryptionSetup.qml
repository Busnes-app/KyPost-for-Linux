import QtQuick
import QtTest
import com.kysecurity.mail 1.0

// The relay withholds mail until encryption setup is finished on the web
// (MailController::encryptionSetupRequired): one screen over the mail section,
// with Settings and Contacts still reachable.
TestCase {
    name: "ShellEncryptionSetup"
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
        MailApp.encryptionSetupRequired = false
        MailApp.encryptionSetupMessage = ""
        MailApp.encryptionSetupUrl = ""
        MailApp.encryptionSetupChecks = 0
        shell.navigate("inbox")
    }

    function cleanup() { MailApp.encryptionSetupRequired = false }

    function screen() {
        const found = findChild(shell.contentItem, "encryptionSetupScreen")
        verify(found)
        return found
    }

    function test_hidden_while_mail_is_served() {
        verify(!screen().visible)
    }

    function test_shows_the_default_wording_and_offers_the_web() {
        MailApp.encryptionSetupRequired = true
        MailApp.encryptionSetupUrl = "https://mail.example/setup/encryption"
        const s = screen()
        verify(s.visible)
        compare(findChild(s, "encryptionSetupTitle").text, "Finish encryption setup")
        compare(findChild(s, "encryptionSetupBody").text,
                "Finish email encryption setup in KyPost on the web, then come back.")
        const open = findChild(s, "encryptionSetupOpen")
        verify(open.visible)
        compare(open.text, "Open KyPost on the web")
    }

    function test_prefers_the_relays_message() {
        MailApp.encryptionSetupMessage = "Finish email encryption setup in KyPost on the web before using mail here."
        MailApp.encryptionSetupRequired = true
        compare(findChild(screen(), "encryptionSetupBody").text, MailApp.encryptionSetupMessage)
    }

    // A pairing that is not https gets no link to open, not a broken one.
    function test_no_web_button_without_a_setup_url() {
        MailApp.encryptionSetupRequired = true
        verify(!findChild(screen(), "encryptionSetupOpen").visible)
    }

    function test_check_again_asks_once_per_click() {
        MailApp.encryptionSetupRequired = true
        const check = findChild(screen(), "encryptionSetupCheck")
        mouseClick(check)
        compare(MailApp.encryptionSetupChecks, 1)
    }

    function test_settings_and_contacts_stay_reachable() {
        MailApp.encryptionSetupRequired = true
        shell.navigate("settings")
        verify(!screen().visible)
        shell.navigate("contacts")
        verify(!screen().visible)
        shell.navigate("inbox")
        verify(screen().visible)
    }
}
