import QtQuick
import com.kysecurity.mail 1.0

// The same metadata-only selection for before/after. A classification result
// arrives only if the shipped reader requests one; this is a UI fixture,
// not proof that the real controller or GnuPG was exercised.
QtObject {
    function reset(root) {
        MailApp.metadataRow = null
        MailApp.pgpLookups = 0
        MailApp.lastDecryptId = ""
        AppLock.locked = false
        root.navigate("inbox")
    }
    function open(root, state) {
        const id = "<playshot-1@kypost-demo.local>"
        MailApp.metadataRow = Object.assign({}, MailApp.findByMessageId(id), {
            subject: "Older protected message", body: state === 4 ? "Readable signed fallback." : "",
            bodyMode: "plain", searchOnly: true, pgpState: 0, canDecryptHere: false,
            pgpBannerTitle: "", pgpBannerBody: "", bodyPending: false,
            bodyNotice: state === 4 ? "" : "KyPost cannot show this message's text here. It may be encrypted; open it in webmail."})
        root.selectEmail(id, "INBOX")
        Qt.callLater(function () {
            if (MailApp.pgpLookups === 0) return
            const pending = state === -1
            const failed = state === -2
            MailApp.metadataRow = Object.assign({}, MailApp.metadataRow, {
                pgpLookupPending: pending, pgpLookupRetryable: failed,
                pgpState: state > 0 ? state : 0, canDecryptHere: state > 0,
                pgpReadAction: state === 4 ? "Verify signature" : "Decrypt with your key",
                pgpBannerTitle: state === 4 ? "This message has an OpenPGP signature"
                    : state === 1 ? "This message is end-to-end encrypted" : "OpenPGP check",
                pgpBannerBody: state === 4 ? "Verify the signature to check the signed content and its sender. This message is not encrypted."
                    : state === 1 ? "Decrypt with your GnuPG key to read this message, or open it in webmail where you keep your key."
                    : failed ? "Could not reach the server to fetch this message. Try again."
                    : "Checking whether this message can be decrypted or verified here…",
                bodyNotice: ""})
            MailApp.messagePgpChanged("INBOX", id)
        })
    }
    property var screens: [
        {name: "older-encrypted", wait: 1000, setup: root => open(root, 1)},
        {name: "search-signed", wait: 1000, setup: root => open(root, 4)},
        {name: "pgp-check-pending", wait: 1000, setup: root => open(root, -1)},
        {name: "pgp-check-error", wait: 1000, setup: root => open(root, -2)}
    ]
}
