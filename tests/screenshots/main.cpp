// Renders the shipped QML offscreen with fixture data and writes one PNG per
// screen. Driven by scripts/screenshots.sh; not a ctest.
//
//   KyPostScreens --root qrc:/qml/AppShell.qml --size 412x732 --mobile
//                 --theme "Busnes Light" --script <screens.qml> --out <dir>
//
// The screen script is a QML file whose root has `property var screens:
// [{name, setup: function (root) {...}, wait: ms}]` and an optional
// `function reset(root)`. It imports com.kysecurity.mail, so it sees the same
// fixture singletons (tests/screenshots/fakes/*.qml) the root does. Theme is
// the real ThemeController so the shots carry the real palette.
#include "models/StandardFolder.h"
#include "stores/SettingsStore.h"
#include "theme/ThemeController.h"

#include <KLocalizedQmlContext>
#include <KLocalizedString>

#include <QApplication>
#include <QBuffer>
#include <QCommandLineParser>
#include <QDir>
#include <QEventLoop>
#include <QFontDatabase>
#include <QImage>
#include <QPainter>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTimer>
#include <QtQml>

#include <unistd.h>

class ScreenRun : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool desktop MEMBER desktop CONSTANT)
    Q_PROPERTY(QString qrDataUrl MEMBER qrDataUrl CONSTANT)

public:
    bool desktop = true;
    QString qrDataUrl;

    // The real core helper, so a fix there shows up in the shots.
    Q_INVOKABLE QString folderDisplayName(const QString& wireName) const
    {
        return standardFolderDisplayName(wireName);
    }
};

namespace {

int g_warnings = 0;
QString g_screen = QStringLiteral("(load)");

void collectWarnings(QtMsgType type, const QMessageLogContext&, const QString& message)
{
    if (type == QtDebugMsg || type == QtInfoMsg)
        return;
    ++g_warnings;
    fprintf(stderr, "%s: %s\n", qPrintable(g_screen), qPrintable(message));
}

void settle(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

QString fakeQr()
{
    // Any valid PNG stands in for the QR the real controller renders.
    QImage image(29, 29, QImage::Format_RGB32);
    image.fill(Qt::white);
    QPainter p(&image);
    for (int y = 0; y < 29; ++y)
        for (int x = 0; x < 29; ++x)
            if (((x * 7 + y * 13) ^ (x * y)) % 3 == 0)
                p.fillRect(x, y, 1, 1, Qt::black);
    p.end();
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.scaled(232, 232).save(&buffer, "PNG");
    return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(bytes.toBase64());
}

void loadFonts()
{
    const QDir fonts(QStringLiteral(":/fonts"));
    for (const QString& file : fonts.entryList({QStringLiteral("*.ttf")}))
        QFontDatabase::addApplicationFont(fonts.filePath(file));
}

bool isBlank(const QImage& image)
{
    const QRgb first = image.pixel(0, 0);
    for (int y = 0; y < image.height(); y += 7)
        for (int x = 0; x < image.width(); x += 7)
            if (image.pixel(x, y) != first)
                return false;
    return true;
}

} // namespace

int main(int argc, char* argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_SCALE_FACTOR", "1");
    qputenv("QT_ENABLE_HIGHDPI_SCALING", "0");
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("kypost-screens"));
    KLocalizedString::setApplicationDomain("kypost");

    QCommandLineParser parser;
    parser.addOption({QStringLiteral("root"), QStringLiteral("Root QML url"), QStringLiteral("url"),
                      QStringLiteral("qrc:/qml/AppShell.qml")});
    parser.addOption({QStringLiteral("size"), QStringLiteral("WxH logical px"), QStringLiteral("size"),
                      QStringLiteral("1920x1080")});
    parser.addOption({QStringLiteral("theme"), QStringLiteral("Theme name"), QStringLiteral("name"),
                      QStringLiteral("Busnes Light")});
    parser.addOption({QStringLiteral("script"), QStringLiteral("Screen script (.qml)"), QStringLiteral("file")});
    parser.addOption({QStringLiteral("out"), QStringLiteral("Output dir"), QStringLiteral("dir")});
    parser.addOption({QStringLiteral("only"), QStringLiteral("Render one screen"), QStringLiteral("name")});
    parser.addOption({QStringLiteral("mobile"), QStringLiteral("General.isDesktopMode = false")});
    parser.process(app);

    const QStringList size = parser.value(QStringLiteral("size")).split(QLatin1Char('x'));
    if (size.size() != 2 || !parser.isSet(QStringLiteral("script")) || !parser.isSet(QStringLiteral("out")))
        qFatal("need --size WxH --script and --out");
    const int width = size[0].toInt();
    const int height = size[1].toInt();
    const QString outDir = parser.value(QStringLiteral("out"));
    QDir().mkpath(outDir);

    loadFonts();
    qInstallMessageHandler(collectWarnings);

    QTemporaryDir settingsDir;
    SettingsStore settings(settingsDir.filePath(QStringLiteral("settings.ini")));
    settings.setThemeId(parser.value(QStringLiteral("theme")));
    ThemeController theme(settings);

    ScreenRun run;
    run.desktop = !parser.isSet(QStringLiteral("mobile"));
    run.qrDataUrl = fakeQr();

    QQmlApplicationEngine engine;
    KLocalization::setupLocalizedContext(&engine);
    qmlRegisterSingletonInstance("KyPostScreens", 1, 0, "ScreenRun", &run);
    qmlRegisterSingletonInstance<ThemeController>("com.kysecurity.mail", 1, 0, "Theme", &theme);
    for (const char* name : {"General", "MailApp", "ProtectedImages", "AppLock", "ContactsApp", "PgpQr",
                             "PgpEnrollment", "Pairing", "UpdateCheck", "Mfa", "Scheduled"}) {
        // Created from QML but registered as a QObject singleton, the way the
        // real controllers are: a composite singleton named "Pairing" would
        // clash with pages/Pairing.qml in the pages directory import.
        const QUrl url(QStringLiteral("qrc:/screens/fakes/Fake%1.qml").arg(QLatin1String(name)));
        qmlRegisterSingletonType<QObject>("com.kysecurity.mail", 1, 0, name,
                                          [url](QQmlEngine* qmlEngine, QJSEngine*) -> QObject* {
                                              QQmlComponent component(qmlEngine, url);
                                              QObject* fake = component.create();
                                              if (!fake)
                                                  qFatal("%s", qPrintable(component.errorString()));
                                              return fake;
                                          });
    }
    qmlRegisterType(QUrl(QStringLiteral("qrc:/screens/fakes/FakePgpQrScanner.qml")), "com.kysecurity.mail", 1, 0,
                    "PgpQrScanner");
    qmlRegisterType(QUrl(QStringLiteral("qrc:/screens/fakes/FakeRemoteContentInterceptor.qml")),
                    "com.kysecurity.mail", 1, 0, "RemoteContentInterceptor");

    engine.load(QUrl(parser.value(QStringLiteral("root"))));
    if (engine.rootObjects().isEmpty())
        qFatal("root failed to load");
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
    if (!window)
        qFatal("root is not a window");
    window->setMinimumSize(QSize(0, 0));
    window->resize(width, height);
    window->show();
    settle(800);

    QQmlComponent scriptComponent(&engine, QUrl::fromLocalFile(parser.value(QStringLiteral("script"))));
    QObject* script = scriptComponent.create();
    if (!script)
        qFatal("screen script failed: %s", qPrintable(scriptComponent.errorString()));

    QQmlEngine::setObjectOwnership(window, QQmlEngine::CppOwnership);
    const QJSValue rootValue = engine.newQObject(window);
    const QJSValue scriptValue = engine.newQObject(script);
    const QJSValue screens = scriptValue.property(QStringLiteral("screens"));
    const int count = screens.property(QStringLiteral("length")).toInt();
    const QString only = parser.value(QStringLiteral("only"));
    int failures = 0;
    for (int i = 0; i < count; ++i) {
        const QJSValue screen = screens.property(i);
        const QString name = screen.property(QStringLiteral("name")).toString();
        if (!only.isEmpty() && name != only)
            continue;
        g_screen = name;
        const QJSValue reset = scriptValue.property(QStringLiteral("reset"));
        if (reset.isCallable())
            reset.callWithInstance(scriptValue, {rootValue});
        settle(150);
        const QJSValue result = screen.property(QStringLiteral("setup")).call({rootValue});
        if (result.isError()) {
            qWarning("setup threw: %s", qPrintable(result.toString()));
            ++failures;
        }
        const QJSValue wait = screen.property(QStringLiteral("wait"));
        settle(wait.isNumber() ? wait.toInt() : 700);
        const QImage image = window->grabWindow();
        const QString file = QDir(outDir).filePath(name + QStringLiteral(".png"));
        if (image.isNull() || isBlank(image) || !image.save(file)) {
            qWarning("could not grab %s (null or blank)", qPrintable(name));
            ++failures;
        } else {
            fprintf(stdout, "%s\n", qPrintable(file));
        }
    }
    g_screen = QStringLiteral("(end)");
    fprintf(stdout, "screens: %d, warnings: %d, failures: %d\n", count, g_warnings, failures);
    fflush(stdout);
    fflush(stderr);
    // Skip engine/WebEngine teardown, which can abort offscreen; the PNGs are written.
    _exit(failures ? 1 : 0);
}

#include "main.moc"
