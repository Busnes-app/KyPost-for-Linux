#include "mail/ProtectedImageHandler.h"
#include "mail/RemoteContentInterceptor.h"
#include "../../core/net/FakeRelayServer.h"

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

void ProtectedImageHandlerTest::restoredDraftHtmlIsCleanedBeforeEnteringTheEditor()
{
    QFile file(QStringLiteral(KYPOST_SOURCE_DIR "/app/qml/components/RichBodyEditor.qml"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QString source = QString::fromUtf8(file.readAll());
    const QRegularExpression cleanPattern(QStringLiteral("readonly property string cleanScript: \"(.*?)\""),
                                          QRegularExpression::DotMatchesEverythingOption);
    const auto clean = cleanPattern.match(source);
    QVERIFY(clean.hasMatch());
    const int start = source.indexOf(QStringLiteral("function seedScript(html)"));
    const int end = source.indexOf(QStringLiteral("\n    }"), start);
    QVERIFY(start >= 0 && end > start);
    QJSEngine js;
    auto root = js.newObject();
    root.setProperty(QStringLiteral("cleanScript"), clean.captured(1));
    js.globalObject().setProperty(QStringLiteral("root"), root);
    const auto fn = js.evaluate(QLatin1Char('(') + source.mid(start, end + 6 - start) + QLatin1Char(')'));
    QVERIFY2(fn.isCallable(), qPrintable(fn.toString()));
    FakeRelayServer tracker(httpResponse(200, "OK", "tracked"));
    const QString hostile = QStringLiteral("<p onclick='window.pwned=1'>draft <b>bold</b></p>"
        "<img src='http://127.0.0.1:%1/pixel' onerror='window.pwned=2'>"
        "<svg onload='window.pwned=3'></svg><script>window.pwned=4</script>"
        "<a href='javascript:window.pwned=5'>unsafe</a>").arg(tracker.port());
    const auto script = fn.call({hostile});
    QVERIFY(!script.isError());
    QWebEngineProfile profile;
    QWebEnginePage page(&profile);
    QSignalSpy loaded(&page, &QWebEnginePage::loadFinished);
    page.setHtml(QStringLiteral("<html><body contenteditable='true'></body></html>"));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 15000);
    bool done = false;
    page.runJavaScript(script.toString(), [&](const QVariant&) { done = true; });
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

int main(int argc, char** argv)
{
    ProtectedImageHandler::registerScheme();
    QGuiApplication app(argc, argv);
    ProtectedImageHandlerTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "ProtectedImageHandlerTest.moc"
