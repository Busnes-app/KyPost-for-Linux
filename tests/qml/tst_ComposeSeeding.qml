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
        compare(desktop.split("onComposeRequested: function (to, subject, body, replyTo) { root.openCompose(to, subject, body, replyTo) }").length, 3)
        verify(desktop.indexOf("replyTo: root.composeSeed.replyTo || ({})") !== -1)
        verify(desktop.indexOf("replyTo: composeWindow.popReplyTo") !== -1)
        verify(desktop.indexOf("root.popOutCompose(to, subject, body, bodyIsHtml, replyTo)") !== -1)
        source.open("GET", "qrc:/qml/pages/Compose.qml", false)
        source.send()
        verify(source.responseText.indexOf('root.replyTo.messageId || "", root.replyTo.mailbox || "",') !== -1)
        // Another account's row id must never thread this account's mail.
        verify(source.responseText.indexOf("function onPairingChanged() { root.replyTo = {} }") !== -1)
        source.open("GET", "qrc:/qml/pages/EmailDetail.qml", false)
        source.send()
        compare(source.responseText.split("root.composeRequested(").length - 1, 3)
        compare(source.responseText.split("root.replyReference())").length - 1, 2)
    }
}
