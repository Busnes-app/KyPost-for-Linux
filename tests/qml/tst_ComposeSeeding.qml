import QtQuick 2.15
import QtTest 1.15

// The pop-out compose window is seeded only from the editor's own sanitized
// draft, which may opt into HTML. Keep the trust bit explicit at the window
// boundary.
TestCase {
    name: "ComposeSeeding"

    function test_popout_compose_preserves_body_trust() {
        var source = new XMLHttpRequest()
        source.open("GET", "qrc:/qml/AppShell.qml", false)
        source.send()
        compare(source.status, 200)
        verify(source.responseText.indexOf(
            "initialBodyIsHtml: composeWindow.popInitialBodyIsHtml") !== -1)
        compare(source.responseText.split("root.popOutCompose(").length - 1, 1)
    }
    // A pop-out used to carry only To/Subject/Body: Cc, Bcc and attachments
    // vanished, and Encrypt/Sign came back unchecked, so a message the user
    // had asked to encrypt went out in the clear from the new window.
    function test_popout_carries_the_whole_composition() {
        var source = new XMLHttpRequest()
        source.open("GET", "qrc:/qml/pages/Compose.qml", false)
        source.send()
        compare(source.status, 200)
        var text = source.responseText
        var start = text.indexOf("function currentDraftForPopOut(callback)")
        var end = text.indexOf("\n    }", start)
        verify(start >= 0 && end > start)
        var snapshot = text.slice(start, end)
        verify(snapshot.indexOf("cc: ccField.joinedText, bcc: bccField.joinedText") !== -1)
        verify(snapshot.indexOf("paths: root.attachmentPaths.slice()") !== -1)
        verify(snapshot.indexOf("encrypt: encryptToggle.checked, sign: signToggle.checked") !== -1)
        verify(text.indexOf("encryptToggle.checked = root.restoredDraft.encrypt === true") !== -1)
        verify(text.indexOf("signToggle.checked = root.restoredDraft.sign === true") !== -1)
        source.open("GET", "qrc:/qml/AppShell.qml", false)
        source.send()
        verify(source.responseText.indexOf("popRestoredDraft: draft,") !== -1)
        verify(source.responseText.indexOf("restoredDraft: composeWindow.popRestoredDraft") !== -1)
    }

    // A reader popped out of INBOX outlives a folder switch in the main
    // window, and MailApp's mailbox actions target MailApp.currentFolder, so
    // a reader action on another folder hit a message with the same UID
    // there. Every manage action is gated on the reader's folder.
    function test_reader_actions_only_act_in_the_readers_folder() {
        var source = new XMLHttpRequest()
        source.open("GET", "qrc:/qml/pages/EmailDetail.qml", false)
        source.send()
        compare(source.status, 200)
        var gate = "enabled: !MailApp.isBusy && root.folder === MailApp.currentFolder\n"
        var calls = ["MailApp.archiveEmails([root.messageId])", "MailApp.markSpam([root.messageId])",
                     "MailApp.deleteEmails([root.messageId])"]
        for (var i = 0; i < calls.length; ++i)
            verify(source.responseText.indexOf(gate + "                    onClicked: " + calls[i]) !== -1, calls[i])
        compare(source.responseText.split("enabled: !MailApp.isBusy\n                    onClicked: MailApp.").length - 1, 0)
    }

    function test_draft_recipients_keep_quoted_commas() {
        var source = new XMLHttpRequest()
        source.open("GET", "qrc:/qml/pages/Compose.qml", false)
        source.send()
        compare(source.status, 200)
        var start = source.responseText.indexOf("function seedTokensFromString(field, value)")
        var end = source.responseText.indexOf("\n    }", start)
        verify(start >= 0 && end > start)
        var seed = eval("(" + source.responseText.slice(start, end + 6) + ")")
        var tokens = []
        seed({ addToken: function(value) { tokens.push(value) } },
             '\"Doe, Jane\" <jane@example.com>, copy@example.com')
        compare(tokens.length, 2)
        compare(tokens[0], "jane@example.com")
        compare(tokens[1], "copy@example.com")
    }

    function test_protected_draft_action_is_scoped_and_the_shell_routes_it() {
        var source = new XMLHttpRequest()
        source.open("GET", "qrc:/qml/pages/EmailDetail.qml", false)
        source.send()
        verify(source.responseText.indexOf('root.folder === MailApp.draftsFolder && root.hasProtectedMessage && root.email.pgpState === 1') !== -1)
        source.open("GET", "qrc:/qml/AppShell.qml", false)
        source.send()
        compare(source.status, 200)
        verify(source.responseText.indexOf("onDraftRequested: function (draft)") !== -1)
        verify(source.responseText.indexOf("restoredDraft:") !== -1)
    }

    // A reply's reference must survive every route into Compose, or the
    // reply silently starts a new thread.
    function test_reply_reference_reaches_the_send() {
        var source = new XMLHttpRequest()
        source.open("GET", "qrc:/qml/AppShell.qml", false)
        source.send()
        var desktop = source.responseText
        compare(desktop.split("onComposeRequested: function (to, subject, body, replyTo, notice) { root.openCompose(to, subject, body, replyTo, notice) }").length, 3)
        verify(desktop.indexOf("replyTo: root.composeSeed.replyTo || ({})") !== -1)
        verify(desktop.indexOf("replyTo: composeWindow.popReplyTo") !== -1)
        verify(desktop.indexOf("root.popOutCompose(draft, replyTo)") !== -1)
        source.open("GET", "qrc:/qml/pages/Compose.qml", false)
        source.send()
        verify(source.responseText.indexOf('root.replyTo.messageId || "", root.replyTo.mailbox || "",') !== -1)
        // Another account's row id must never thread this account's mail.
        verify(source.responseText.indexOf("function onPairingChanged() { root.replyTo = {} }") !== -1)
        source.open("GET", "qrc:/qml/pages/EmailDetail.qml", false)
        source.send()
        compare(source.responseText.split("root.composeRequested(").length - 1, 3)
        compare(source.responseText.split("root.replyReference(), root.quoteNotice(false))").length - 1, 2)
    }
}
