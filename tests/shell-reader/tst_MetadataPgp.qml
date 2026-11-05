import QtQuick
import QtTest
import com.kysecurity.mail 1.0
import KyPostScreens

TestCase {
    name: "MetadataPgp"
    when: windowShown
    visible: true
    width: 800
    height: 900
    property var reader: null

    function init() {
        AppLock.locked = false
        MailApp.pgpLookups = 0
        MailApp.lastDecryptId = ""
        MailApp.metadataRow = {messageId: "metadata-pgp", folder: "INBOX", subject: "Older mail",
            sender: "sender@example.com", body: "", bodyMode: "plain", status: "read",
            searchOnly: true, bodyPending: false, pgpState: 0, canDecryptHere: false,
            pgpBannerTitle: "", pgpBannerBody: ""}
        const component = Qt.createComponent("qrc:/qml/pages/EmailDetail.qml")
        compare(component.status, Component.Ready, component.errorString())
        reader = component.createObject(this, {width: 800, height: 900, folder: "INBOX", messageId: "metadata-pgp"})
        verify(reader)
    }
    function cleanup() {
        reader.destroy()
        reader = null
        MailApp.metadataRow = null
        AppLock.locked = false
    }
    function finish(state) {
        MailApp.metadataRow = Object.assign({}, MailApp.metadataRow, {pgpLookupPending: false,
            pgpState: state, canDecryptHere: true,
            pgpReadAction: state === 4 ? "Verify signature" : "Decrypt with your key",
            pgpBannerTitle: state === 4 ? "This message has an OpenPGP signature" : "This message is end-to-end encrypted"})
        MailApp.messagePgpChanged("INBOX", "metadata-pgp")
    }
    function test_open_checks_without_decrypting() {
        verify(MailApp.pgpLookups > 0)
        verify(reader.email.pgpLookupPending)
        compare(MailApp.lastDecryptId, "")
        verify(!findChild(reader, "decryptButton").visible)
    }
    function test_signed_action_is_explicit() {
        finish(4)
        const button = findChild(reader, "decryptButton")
        tryVerify(() => button.visible)
        compare(button.text, "Verify signature")
        compare(MailApp.lastDecryptId, "")
        button.clicked()
        compare(MailApp.lastDecryptId, "metadata-pgp")
        compare(MailApp.lastDecryptFolder, "INBOX")
    }
    function test_other_folder_completion_does_not_replace_this_reader() {
        MailApp.metadataRow = Object.assign({}, MailApp.metadataRow, {pgpState: 1, canDecryptHere: true})
        MailApp.messagePgpChanged("Archive", "metadata-pgp")
        compare(reader.email.pgpState, 0)
        verify(!findChild(reader, "decryptButton").visible)
    }
    function test_unlock_rechecks_even_with_cached_body() {
        finish(1)
        const before = MailApp.pgpLookups
        AppLock.locked = true
        AppLock.locked = false
        tryVerify(() => MailApp.pgpLookups > before)
        verify(reader.email.pgpLookupPending)
        compare(MailApp.lastDecryptId, "")
    }
    function test_invalidated_lookup_restarts_only_the_matching_visible_reader() {
        const before = MailApp.pgpLookups
        MailApp.metadataRow = Object.assign({}, MailApp.metadataRow, {pgpLookupNeeded: true, pgpLookupPending: false})
        MailApp.messagePgpChanged("Archive", "metadata-pgp")
        compare(MailApp.pgpLookups, before)
        MailApp.messagePgpChanged("INBOX", "metadata-pgp")
        verify(MailApp.pgpLookups > before)
        verify(reader.email.pgpLookupPending)
        compare(MailApp.lastDecryptId, "")
    }
    function test_pending_lookup_recovers_after_the_actual_popout_transition() {
        const component = Qt.createComponent("qrc:/qml/AppShell.qml")
        compare(component.status, Component.Ready, component.errorString())
        const shell = component.createObject(null, {width: 1200, height: 900})
        verify(shell)
        shell.show()
        shell.selectEmail("metadata-pgp", "INBOX")
        shell.popOutEmail("metadata-pgp", "INBOX")
        const popup = ScreenRun.emailWindow("metadata-pgp")
        verify(popup)
        const before = MailApp.pgpLookups
        // Closing the embedded reader invalidates the new window's pending
        // lookup. Controller coverage uses a held real HTTP response; this
        // fake exercises the actual AppShell/EmailDetail continuation.
        MailApp.finishMetadataLookup(1)
        verify(MailApp.pgpLookups > before)
        MailApp.finishMetadataLookup(1)
        const button = findChild(popup.contentItem, "decryptButton")
        tryVerify(() => button.visible)
        compare(MailApp.lastDecryptId, "")
        popup.close()
        shell.destroy()
    }
    function test_failure_keeps_body_and_retry_uses_same_selection() {
        MailApp.metadataRow = Object.assign({}, MailApp.metadataRow, {pgpLookupPending: false,
            pgpLookupRetryable: true, body: "Readable signed fallback", pgpBannerTitle: "OpenPGP check",
            pgpBannerBody: "Could not reach the server to fetch this message. Try again."})
        MailApp.messagePgpChanged("INBOX", "metadata-pgp")
        compare(reader.email.body, "Readable signed fallback")
        verify(!findChild(reader, "decryptButton").visible)
        const retry = findChild(reader, "pgpLookupRetryButton")
        verify(retry.visible)
        const before = MailApp.pgpLookups
        retry.clicked()
        verify(MailApp.pgpLookups > before)
        compare(MailApp.lastDecryptId, "")
    }
}
