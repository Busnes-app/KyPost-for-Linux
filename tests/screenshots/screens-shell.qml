import QtQuick
import com.kysecurity.mail 1.0

// Every screen of AppShell.qml. Names that match an Android store shot are
// paired with it in scripts/screenshots-compare.py.
QtObject {
    function reset(root) {
        AppLock.locked = false; Mfa.open = false; MailApp.empty = false; MailApp.isBusy = false
        MailApp.lastError = ""; MailApp.clearSearch(); root.searchOpen = false
        ContactsApp.empty = false; ContactsApp.reload()
        Pairing.pairingState = "idle"; Pairing.isPaired = true
        Invites.confirmation = ({})
        root.closeOverlays(); root.selectFolder("INBOX"); root.navigate("inbox")
    }
    function inviteCard() {
        return {token: 7, eyebrow: "Calendar invite · REQUEST", title: "Saturday hike: Eagle Peak loop",
                details: ["Sat 31/10/2026 07:30 – 13:00", "Where: Eagle Peak trailhead", "Organizer: maya.chen@example.com"],
                notices: [], offersRsvp: true}
    }
    function findReader(item, id) {
        if (item.replyQuote !== undefined && item.messageId === id)
            return item
        const kids = item.contentItem ? [item.contentItem] : (item.children || [])
        for (let i = 0; i < kids.length; ++i) {
            const found = findReader(kids[i], id)
            if (found) return found
        }
        return null
    }
    property var screens: [
        {name: "inbox-all", setup: function (root) {}},
        {name: "inbox-primary", setup: function (root) { MailApp.selectKeyword("Primary") }},
        {name: "inbox-selected", wait: 1800, setup: function (root) { root.selectEmail("<playshot-0@kypost-demo.local>", "INBOX") }},
        {name: "folders", setup: function (root) { root.openFolders() }},
        {name: "folder-sent", setup: function (root) { root.selectFolder("Sent") }},
        {name: "email-detail", wait: 1800, setup: function (root) { root.selectEmail("<playshot-1@kypost-demo.local>", "INBOX") }},
        // A calendar invite, as Invites.inviteLoaded hands it to the reader.
        {name: "email-invite", wait: 1800, setup: function (root) {
            const id = "<playshot-1@kypost-demo.local>"
            root.selectEmail(id, "INBOX")
            findReader(root, id).invite = inviteCard()
        }},
        {name: "rsvp-confirm", wait: 1800, setup: function (root) {
            const id = "<playshot-1@kypost-demo.local>"
            root.selectEmail(id, "INBOX")
            findReader(root, id).invite = inviteCard()
            Invites.confirmation = {token: 7, title: "Send RSVP unencrypted?", sendLabel: "Send unencrypted",
                text: "Reply \"Accepted\" to maya.chen@example.com as sam@kypost-demo.local.\n\n"
                    + "RSVPs are always sent unencrypted and unsigned, because the organizer's calendar can't read "
                    + "an encrypted reply. The reply carries your answer, your address and the event's ID."}
        }},
        {name: "compose", setup: function (root) { root.openCompose("Maya Chen <maya.chen@example.com>", "Re: Saturday hike", "Count me in! I will bring the trail mix and an extra headlamp.<br><br>See you at 7:30.<br><br>Sam", ({})) }},
        // The reader's own Reply: the composer opens on the quoted body.
        {name: "reply", wait: 1800, setup: function (root) {
            const id = "<playshot-1@kypost-demo.local>"
            root.selectEmail(id, "INBOX")
            const detail = findReader(root, id)
            root.openCompose(detail.extractAddress(detail.email.sender), detail.withPrefix(detail.email.subject, "Re:"),
                             detail.replyQuote(), detail.replyReference())
        }},
        // A forward that could not carry everything says so until dismissed.
        {name: "forward-notice", setup: function (root) {
            root.openRestoredDraft({ token: "fwd", subject: "Fwd: Q4 roadmap draft", forwarded: true,
                body: "<br><br><div>---------- Forwarded message ----------</div><div>From: Jordan Ellis</div><div>Subject: Q4 roadmap draft</div><br>Thanks for pulling this together.",
                paths: ["kypost-draft:0"], names: { "kypost-draft:0": "roadmap.pdf" },
                notice: "1 attachment is too large to forward and is not included." })
        }},
        {name: "compose-empty", setup: function (root) { root.navigate("compose") }},
        {name: "contacts-list", setup: function (root) { root.navigate("contacts") }},
        {name: "contact-detail", setup: function (root) { root.navigate("contacts"); root.selectContact("c4") }},
        {name: "contact-new", setup: function (root) { root.navigate("contacts"); root.selectContact("") }},
        {name: "contacts-empty", setup: function (root) { ContactsApp.empty = true; ContactsApp.reload(); root.navigate("contacts") }},
        {name: "settings", setup: function (root) { root.navigate("settings") }},
        {name: "settings-connection", setup: function (root) { root.openSettingsPane(0) }},
        {name: "settings-appearance", setup: function (root) { root.openSettingsPane(1) }},
        {name: "settings-keywords", setup: function (root) { root.openSettingsPane(2) }},
        {name: "settings-contacts", setup: function (root) { root.openSettingsPane(3) }},
        {name: "settings-notifications", setup: function (root) { root.openSettingsPane(4) }},
        {name: "settings-general", setup: function (root) { root.openSettingsPane(5) }},
        {name: "settings-security", setup: function (root) { root.openSettingsPane(6) }},
        {name: "scheduled", setup: function (root) { root.openScheduled() }},
        {name: "search", setup: function (root) { root.searchOpen = true; MailApp.setSearchQuery("roadmap") }},
        {name: "mfa-approval", setup: function (root) { Mfa.open = true }},
        {name: "pgp-my-qr", setup: function (root) { root.openPgpMyQrCode() }},
        {name: "pgp-scan", setup: function (root) { root.openPgpScan() }},
        {name: "pairing", setup: function (root) { Pairing.isPaired = false; root.openPairing() }},
        {name: "pairing-confirm", setup: function (root) { Pairing.pairingState = "confirm" }},
        {name: "unlock", setup: function (root) { AppLock.locked = true }},
        {name: "inbox-empty", setup: function (root) { MailApp.empty = true; MailApp.reload() }},
        {name: "inbox-loading", setup: function (root) { MailApp.isBusy = true }},
        {name: "inbox-error", setup: function (root) { MailApp.lastError = "Could not reach relay.kypost-demo.local" }},
    ]
}
