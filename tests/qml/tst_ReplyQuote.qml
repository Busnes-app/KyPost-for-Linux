import QtQuick
import QtTest

import "../../app/qml/utils/format.js" as Format

// Reply, Reply All and Forward quote the real body (Android's quoteForReply /
// quoteForForward), never the 200-character preview and never plaintext this
// view only holds as local decryption.
TestCase {
    name: "ReplyQuote"

    function source(url) {
        const request = new XMLHttpRequest()
        request.open("GET", url, false)
        request.send()
        compare(request.status, 200)
        return request.responseText
    }

    function test_replyQuotesAnEscapedPlainBodyUnderTheAttribution() {
        const quoted = Format.quotedBodyHtml({ body: "a < b\nline2", bodyMode: "plain" }, false)
        const html = Format.replyQuoteHtml("Al <x@y> wrote:", quoted)
        const attribution = html.indexOf("<div>Al &lt;x@y&gt; wrote:</div>")
        const quote = html.indexOf("<blockquote")
        verify(attribution >= 0, html)
        verify(quote > attribution, html)
        verify(html.indexOf("a &lt; b<br>line2", quote) > quote, html)
    }

    function test_anHtmlBodyIsPassedOnForTheEditorToClean() {
        compare(Format.quotedBodyHtml({ body: "<p>Hi <b>there</b></p>", bodyMode: "html" }, false),
                "<p>Hi <b>there</b></p>")
    }

    function test_forwardHeaderLinesAreEscaped() {
        const html = Format.forwardQuoteHtml("---------- Forwarded message ----------", "From: <x@y>",
                                             "Subject: 1 < 2 & more", "<p>body</p>")
        verify(html.indexOf("<div>---------- Forwarded message ----------</div>") >= 0, html)
        verify(html.indexOf("<div>From: &lt;x@y&gt;</div>") >= 0, html)
        verify(html.indexOf("<div>Subject: 1 &lt; 2 &amp; more</div><br><p>body</p>") >= 0, html)
    }

    // Attribution only: nothing to quote, too long to clean, or only a preview.
    function test_noQuotedContentWithoutAQuotableBody() {
        const attributionOnly = "<br><br><div>me wrote:</div><blockquote></blockquote>"
        compare(Format.replyQuoteHtml("me wrote:", Format.quotedBodyHtml({ body: "", bodyMode: "plain" }, false)),
                attributionOnly)
        compare(Format.replyQuoteHtml("me wrote:", Format.quotedBodyHtml({}, false)), attributionOnly)
        const atCap = "x".repeat(524288)
        compare(Format.quotedBodyHtml({ body: atCap, bodyMode: "plain" }, false), atCap)
        compare(Format.replyQuoteHtml("me wrote:", Format.quotedBodyHtml({ body: atCap + "x", bodyMode: "plain" }, false)),
                attributionOnly)
        compare(Format.replyQuoteHtml("me wrote:", Format.quotedBodyHtml({ preview: "a summary" }, false)),
                attributionOnly)
    }

    // Quoting locally decrypted content would put end-to-end-protected text
    // into a send that may go out in the clear.
    function test_protectedMessagesQuoteNothing() {
        compare(Format.quotedBodyHtml({ body: "cached", bodyMode: "plain", pgpState: 1 }, false), "")
        compare(Format.quotedBodyHtml({ body: "cached", bodyMode: "plain", pgpState: 0 }, true), "")
        compare(Format.quotedBodyHtml({ body: "cached", bodyMode: "plain", pgpState: 0 }, false), "cached")
    }

    function test_theReaderNeverRendersOrQuotesThePreview() {
        const detail = source("qrc:/qml/pages/EmailDetail.qml")
        verify(detail.indexOf("email.preview") === -1)
        compare(detail.split("Format.quotedBodyHtml(root.email, root.hasDecryptedBody)").length - 1, 2)
        verify(detail.indexOf("root.composeRequested(to, subject, root.replyQuote(), root.replyReference())") !== -1)
        verify(detail.indexOf("root.composeRequested(to.join(\", \"), subject, root.replyQuote(), root.replyReference())") !== -1)
        verify(detail.indexOf("const body = Format.forwardQuoteHtml(") !== -1)
        // Forward attachments are refused for protected messages before any download.
        verify(detail.indexOf("root.attachments.length > 0 && !root.hasDecryptedBody && root.email.pgpState !== 1") !== -1)
    }

    // Every seed is HTML into seedScript, never an escaping plain-text path.
    function test_quotesEnterTheEditorThroughSeedScript() {
        const shell = source("qrc:/qml/AppShell.qml")
        verify(shell.indexOf("initialBody: root.composeSeed.body\n            initialBodyIsHtml: true") !== -1)
        verify(shell.indexOf("function onForwardComposeRequested(draft) { root.openRestoredDraft(draft) }") !== -1)
        const compose = source("qrc:/qml/pages/Compose.qml")
        verify(compose.indexOf("quotedInitialBodyHtml") === -1)
        verify(compose.indexOf("bodyEditor.loadInitialHtml(root.restoredDraft.token ? root.restoredDraft.body\n"
            + "            : (root.initialBodyIsHtml ? root.initialBody :") !== -1)
        const editor = source("qrc:/qml/components/RichBodyEditor.qml")
        verify(editor.indexOf("webView.runJavaScript(root.seedScript(root.pendingInitialHtml))") !== -1)
    }
}
