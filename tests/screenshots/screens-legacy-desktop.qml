import QtQuick
import com.kysecurity.mail 1.0

// "Before" shots of the pre-round-3 DesktopRoot: what its root functions reach.
QtObject {
    function reset(root) {
        AppLock.locked = false; Mfa.open = false; MailApp.empty = false; MailApp.isBusy = false
        MailApp.lastError = ""; MailApp.clearSearch(); MailApp.selectFolder("INBOX"); root.closeDetail()
    }
    property var screens: [
        {name: "inbox-all", setup: function (root) {}},
        {name: "inbox-primary", setup: function (root) { MailApp.selectKeyword("Primary") }},
        {name: "email-detail", wait: 1800, setup: function (root) { root.selectEmail("<playshot-1@kypost-demo.local>", "INBOX") }},
        {name: "compose", setup: function (root) { root.openCompose("Maya Chen <maya.chen@example.com>", "Re: Saturday hike", "Count me in!", ({})) }},
        {name: "contacts-list", setup: function (root) { root.selectContactsSection() }},
        {name: "contact-detail", setup: function (root) { root.selectContactsSection(); root.selectContact("c4") }},
        {name: "search", setup: function (root) { MailApp.setSearchQuery("roadmap") }},
        {name: "mfa-approval", setup: function (root) { Mfa.open = true }},
        {name: "unlock", setup: function (root) { AppLock.locked = true }},
        {name: "inbox-empty", setup: function (root) { MailApp.empty = true; MailApp.reload() }},
        {name: "inbox-loading", setup: function (root) { MailApp.isBusy = true }},
        {name: "inbox-error", setup: function (root) { MailApp.lastError = "Could not reach relay.kypost-demo.local" }},
    ]
}
