import QtQuick 2.15
import QtTest 1.15

// DesktopRoot has two compose seed callers: only the editor's own sanitized
// draft may opt into HTML. Keep the trust bit explicit at the window boundary.
TestCase {
    name: "ComposeSeeding"

    function test_popout_compose_preserves_body_trust() {
        var source = new XMLHttpRequest()
        source.open("GET", "qrc:/qml/DesktopRoot.qml", false)
        source.send()
        compare(source.status, 200)
        verify(source.responseText.indexOf(
            "initialBodyIsHtml: composeWindow.popInitialBodyIsHtml") !== -1)
        verify(source.responseText.indexOf(
            "root.popOutCompose(to || \"\", subject || \"\", body || \"\", false)") !== -1)
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

    function test_protected_draft_action_is_scoped_and_both_roots_route_it() {
        var source = new XMLHttpRequest()
        source.open("GET", "qrc:/qml/pages/EmailDetail.qml", false)
        source.send()
        verify(source.responseText.indexOf('root.folder === "Drafts" && root.hasProtectedMessage && root.email.pgpState === 1') !== -1)
        for (var root of ["DesktopRoot", "MobileRoot"]) {
            source.open("GET", "qrc:/qml/" + root + ".qml", false)
            source.send()
            compare(source.status, 200)
            verify(source.responseText.indexOf("onDraftRequested: function (draft)") !== -1)
            verify(source.responseText.indexOf("restoredDraft:") !== -1)
        }
    }
}
