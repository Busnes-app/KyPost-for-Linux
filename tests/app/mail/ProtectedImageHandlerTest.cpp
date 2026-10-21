#include "mail/ProtectedImageHandler.h"
#include "mail/RemoteContentInterceptor.h"
#include "../../core/net/FakeRelayServer.h"
#include "domain/LinkTarget.h"

#include <QBuffer>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJSEngine>
#include <QSignalSpy>
#include <QRegularExpression>
#include <QTest>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineScript>

class ProtectedImageHandlerTest : public QObject
{
    Q_OBJECT
private slots:
    void rendersLocalImagesWithoutFetchingRemoteContent();
    void restoredDraftHtmlIsCleanedBeforeEnteringTheEditor();
    void quotedHtmlDropsNonBodyElementsWithTheirContent();
    void linkAnchorScriptReadsLinksWithPageJavaScriptOff();
};

void ProtectedImageHandlerTest::rendersLocalImagesWithoutFetchingRemoteContent()
{
    QByteArray png;
    QBuffer output(&png);
    QVERIFY(output.open(QIODevice::WriteOnly));
    QImage image(3, 2, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(&output, "PNG"));
    const QUrl base(QStringLiteral("kypost-cid://abc-123/"));
    bool active = true;
    int requests = 0;
    ProtectedImageHandler handler([&](const QUrl& url) -> ProtectedImageHandler::Image {
        ++requests;
        if (active && url == QUrl(base.toString() + QStringLiteral("logo")))
            return {"image/png", png};
        return {};
    });
    RemoteContentInterceptor interceptor;
    interceptor.setLocalImageBase(base);
    QWebEngineProfile profile;
    QVERIFY(profile.isOffTheRecord());
    profile.setHttpCacheType(QWebEngineProfile::NoCache);
    profile.installUrlSchemeHandler("kypost-cid", &handler);
    profile.setUrlRequestInterceptor(&interceptor);
    QWebEnginePage page(&profile);
    page.settings()->setAttribute(QWebEngineSettings::JavascriptEnabled, false);
    page.settings()->setAttribute(QWebEngineSettings::AutoLoadImages, true);
    FakeRelayServer remote(httpResponse(200, "OK", png, "image/png"));

    QFile script(QStringLiteral(KYPOST_SOURCE_DIR "/app/qml/utils/format.js"));
    QVERIFY(script.open(QIODevice::ReadOnly));
    QString source = QString::fromUtf8(script.readAll());
    source.remove(0, source.indexOf(QLatin1Char('\n')) + 1); // .pragma library
    QJSEngine js;
    QVERIFY(!js.evaluate(source).isError());
    const QString content = QStringLiteral("<img src='cid:logo'><img src='http://127.0.0.1:%1/pixel'>").arg(remote.port());
    const auto rendered = js.globalObject().property(QStringLiteral("renderedEmailHtml"))
        .call({content, false, QString(), false, base.toString()}).toString();
    QSignalSpy loaded(&page, &QWebEnginePage::loadFinished);
    page.setHtml(rendered);
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 15000);
    QVERIFY(loaded[0][0].toBool());
    QCOMPARE(requests, 1);
    QCOMPARE(remote.receivedRequests().size(), 0);
    int width = -1;
    page.runJavaScript(QStringLiteral("document.images[0].naturalWidth"), QWebEngineScript::ApplicationWorld,
        [&](const QVariant& value) { width = value.toInt(); });
    QTRY_COMPARE_WITH_TIMEOUT(width, 3, 5000);

    // The controller revokes its token on lock/account change; a stale URL
    // must go through the resolver again, not survive in a profile cache.
    active = false;
    loaded.clear();
    page.setHtml(rendered);
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 15000);
    QCOMPARE(requests, 2);
    width = -1;
    page.runJavaScript(QStringLiteral("document.images[0].naturalWidth"), QWebEngineScript::ApplicationWorld,
        [&](const QVariant& value) { width = value.toInt(); });
    QTRY_COMPARE_WITH_TIMEOUT(width, 0, 5000);
}

// RichBodyEditor.seedScript(html), evaluated from the shipped source: the
// exact script the composer runs on its live page. Empty if the source moved.
static QString editorSeedScript(const QString& html)
{
    QFile file(QStringLiteral(KYPOST_SOURCE_DIR "/app/qml/components/RichBodyEditor.qml"));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QString source = QString::fromUtf8(file.readAll());
    const QRegularExpression cleanPattern(QStringLiteral("readonly property string cleanScript: \"(.*?)\""),
                                          QRegularExpression::DotMatchesEverythingOption);
    const auto clean = cleanPattern.match(source);
    const int start = source.indexOf(QStringLiteral("function seedScript(html)"));
    const int end = source.indexOf(QStringLiteral("\n    }"), start);
    if (!clean.hasMatch() || start < 0 || end <= start)
        return {};
    QJSEngine js;
    auto root = js.newObject();
    root.setProperty(QStringLiteral("cleanScript"), clean.captured(1));
    js.globalObject().setProperty(QStringLiteral("root"), root);
    const auto fn = js.evaluate(QLatin1Char('(') + source.mid(start, end + 6 - start) + QLatin1Char(')'));
    const auto script = fn.isCallable() ? fn.call({html}) : QJSValue();
    return script.isString() ? script.toString() : QString();
}

void ProtectedImageHandlerTest::restoredDraftHtmlIsCleanedBeforeEnteringTheEditor()
{
    FakeRelayServer tracker(httpResponse(200, "OK", "tracked"));
    const QString hostile = QStringLiteral("<p onclick='window.pwned=1'>draft <b>bold</b></p>"
        "<img src='http://127.0.0.1:%1/pixel' onerror='window.pwned=2'>"
        "<svg onload='window.pwned=3'></svg><script>window.pwned=4</script>"
        "<a href='javascript:window.pwned=5'>unsafe</a>").arg(tracker.port());
    const QString script = editorSeedScript(hostile);
    QVERIFY(!script.isEmpty());
    QWebEngineProfile profile;
    QWebEnginePage page(&profile);
    QSignalSpy loaded(&page, &QWebEnginePage::loadFinished);
    page.setHtml(QStringLiteral("<html><body contenteditable='true'></body></html>"));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 15000);
    bool done = false;
    page.runJavaScript(script, [&](const QVariant&) { done = true; });
    QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
    QString html;
    page.runJavaScript(QStringLiteral("document.body.innerHTML"), [&](const QVariant& result) { html = result.toString(); });
    QTRY_VERIFY_WITH_TIMEOUT(!html.isEmpty(), 5000);
    QVERIFY(html.contains(QStringLiteral("<b>bold</b>")));
    QVERIFY(!html.contains(QStringLiteral("<img")));
    QVERIFY(!html.contains(QStringLiteral("<script")));
    QVERIFY(!html.contains(QStringLiteral("onclick")));
    QVERIFY(!html.contains(QStringLiteral("href=")));
    QString executed;
    page.runJavaScript(QStringLiteral("typeof window.pwned"), [&](const QVariant& result) { executed = result.toString(); });
    QTRY_COMPARE_WITH_TIMEOUT(executed, QStringLiteral("undefined"), 5000);
    QCOMPARE(tracker.receivedRequests().size(), 0);
}

// A quoted message's <style>, <script> and <title> are not body text. Unwrapped
// rather than removed, their content showed up in the reply as quoted text.
void ProtectedImageHandlerTest::quotedHtmlDropsNonBodyElementsWithTheirContent()
{
    FakeRelayServer tracker(httpResponse(200, "OK", "tracked"));
    const QString quoted = QStringLiteral("<style>p{color:red}</style><script>alert(1)</script><title>T</title>"
        "<p>Hi <img src=\"http://127.0.0.1:%1/t.png\"> <a href=\"javascript:alert(1)\">l</a> "
        "<a href=\"https://ok.example/\">o</a></p>").arg(tracker.port());
    const QString script = editorSeedScript(quoted);
    QVERIFY(!script.isEmpty());
    QWebEngineProfile profile;
    QWebEnginePage page(&profile);
    QSignalSpy loaded(&page, &QWebEnginePage::loadFinished);
    page.setHtml(QStringLiteral("<html><body contenteditable='true'></body></html>"));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 15000);
    bool done = false;
    page.runJavaScript(script, [&](const QVariant&) { done = true; });
    QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
    QVariantMap dom;
    page.runJavaScript(QStringLiteral("(function() { var a = document.body.querySelectorAll('a'); return {"
        "text: document.body.textContent,"
        "banned: document.body.querySelectorAll('img, style, script, title').length,"
        "links: a.length,"
        "firstHref: a.length > 0 && a[0].hasAttribute('href'),"
        "secondHref: a.length > 1 ? a[1].getAttribute('href') : '' }; })()"),
        [&](const QVariant& result) { dom = result.toMap(); });
    QTRY_VERIFY_WITH_TIMEOUT(!dom.isEmpty(), 5000);
    const QString text = dom.value(QStringLiteral("text")).toString();
    QVERIFY2(text.contains(QStringLiteral("Hi")), qPrintable(text));
    QVERIFY2(!text.contains(QStringLiteral("color:red")), qPrintable(text));
    QVERIFY2(!text.contains(QStringLiteral("alert")), qPrintable(text));
    QVERIFY2(!text.contains(QLatin1Char('T')), qPrintable(text));
    QCOMPARE(dom.value(QStringLiteral("banned")).toInt(), 0);
    QCOMPARE(dom.value(QStringLiteral("links")).toInt(), 2);
    QCOMPARE(dom.value(QStringLiteral("firstHref")).toBool(), false);
    QCOMPARE(dom.value(QStringLiteral("secondHref")).toString(), QStringLiteral("https://ok.example/"));
    QCOMPARE(tracker.receivedRequests().size(), 0);
}

// EmailDetail reads link text with kLinkAnchorScript in the isolated world,
// because the message's own JavaScript is off. Proves that works, that the
// sender's inline script cannot rewrite what it reads, and that its hrefs
// match the URL the click hands over.
void ProtectedImageHandlerTest::linkAnchorScriptReadsLinksWithPageJavaScriptOff()
{
    QWebEngineProfile profile;
    QWebEnginePage page(&profile);
    page.settings()->setAttribute(QWebEngineSettings::JavascriptEnabled, false);
    QSignalSpy loaded(&page, &QWebEnginePage::loadFinished);
    page.setHtml(QStringLiteral("<a href='https://evil.example/'>bank.example</a>"
                                "<a href='https://p\u0430yp\u0430l.com/x'>paypal.com</a>"
                                "<a href='https://bank.example/'>Read more</a>"
                                "<script>document.links[0].textContent = 'evil.example'</script>"));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 15000);
    QVERIFY(loaded[0][0].toBool());

    QVariant result;
    bool done = false;
    page.runJavaScript(QString::fromLatin1(kLinkAnchorScript), QWebEngineScript::ApplicationWorld,
        [&](const QVariant& value) { result = value; done = true; });
    QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
    const QVariantList anchors = result.toList();
    QCOMPARE(anchors.size(), 3);
    QCOMPARE(anchors[0].toList(), (QVariantList{QStringLiteral("https://evil.example/"), QStringLiteral("bank.example")}));
    QCOMPARE(anchors[1].toList().value(0).toString(), QStringLiteral("https://xn--pypl-53dc.com/x"));

    QCOMPARE(linkTextMismatch(anchors, QUrl(QStringLiteral("https://evil.example/"))), QStringLiteral("evil.example"));
    QCOMPARE(linkTextMismatch(anchors, QUrl(QStringLiteral("https://p\u0430yp\u0430l.com/x"))),
             QStringLiteral("xn--pypl-53dc.com"));
    QCOMPARE(linkTextMismatch(anchors, QUrl(QStringLiteral("https://bank.example/"))), QString());
}

int main(int argc, char** argv)
{
    ProtectedImageHandler::registerScheme();
    QGuiApplication app(argc, argv);
    ProtectedImageHandlerTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "ProtectedImageHandlerTest.moc"
