import QtQuick
import com.kysecurity.mail 1.0

// "Before" shots of the pre-round-3 MobileRoot: what its root functions reach.
QtObject {
    function reset(root) {
        AppLock.locked = false; Mfa.open = false; MailApp.empty = false; MailApp.isBusy = false
        MailApp.lastError = ""; MailApp.clearSearch(); MailApp.selectFolder("INBOX"); root.goInbox()
        while (root.pageStack.depth > 1) root.pageStack.pop()
    }
    property var screens: [
        {name: "inbox-all", setup: function (root) {}},
        {name: "inbox-primary", setup: function (root) { MailApp.selectKeyword("Primary") }},
        {name: "email-detail", wait: 1800, setup: function (root) { root.openEmailDetail("<playshot-1@kypost-demo.local>", "INBOX") }},
        {name: "contacts-list", setup: function (root) { root.goContacts() }},
        {name: "settings-connection", setup: function (root) { root.openSettingsPane(0) }},
        {name: "settings-appearance", setup: function (root) { root.openSettingsPane(1) }},
        {name: "settings-keywords", setup: function (root) { root.openSettingsPane(2) }},
        {name: "settings-contacts", setup: function (root) { root.openSettingsPane(3) }},
        {name: "settings-notifications", setup: function (root) { root.openSettingsPane(4) }},
        {name: "settings-general", setup: function (root) { root.openSettingsPane(5) }},
        {name: "settings-security", setup: function (root) { root.openSettingsPane(6) }},
        {name: "search", setup: function (root) { MailApp.setSearchQuery("roadmap") }},
        {name: "mfa-approval", setup: function (root) { Mfa.open = true }},
        {name: "unlock", setup: function (root) { AppLock.locked = true }},
        {name: "inbox-empty", setup: function (root) { MailApp.empty = true; MailApp.reload() }},
        {name: "inbox-loading", setup: function (root) { MailApp.isBusy = true }},
        {name: "inbox-error", setup: function (root) { MailApp.lastError = "Could not reach relay.kypost-demo.local" }},
    ]
}
