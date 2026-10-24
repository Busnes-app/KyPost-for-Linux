#pragma once
// Fixture singletons shared by the screenshot harness (KyPostScreens) and the
// shell tests (ShellTests): the QML fakes in fakes/, Android's demo mailbox,
// and the real ThemeController for the palette.
#include "models/StandardFolder.h"
#include "theme/MaterialIconProvider.h"
#include "theme/ThemeController.h"

#include <QBuffer>
#include <QFont>
#include <QGuiApplication>
#include <QDir>
#include <QFontDatabase>
#include <QImage>
#include <QPainter>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QtQml>

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

namespace Fixtures {

// Stock Plasma's 10 pt, so Theme.fontScale is 1.0 and sizes read as Android sp.
inline void pinFont()
{
    QFont font = QGuiApplication::font();
    font.setPointSizeF(10);
    QGuiApplication::setFont(font);
}

inline QString fakeQr()
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

inline void loadFonts()
{
    const QDir fonts(QStringLiteral(":/fonts"));
    for (const QString& file : fonts.entryList({QStringLiteral("*.ttf")}))
        QFontDatabase::addApplicationFont(fonts.filePath(file));
}

// Registers Theme (real), the fixture singletons and the icon provider.
inline void registerAll(QQmlEngine& engine, ScreenRun& run, ThemeController& theme)
{
    engine.addImageProvider(QStringLiteral("icon"), new MaterialIconProvider);
    qmlRegisterSingletonInstance("KyPostScreens", 1, 0, "ScreenRun", &run);
    qmlRegisterSingletonInstance<ThemeController>("com.kysecurity.mail", 1, 0, "Theme", &theme);
    for (const char* name : {"General", "MailApp", "ProtectedImages", "AppLock", "ContactsApp", "PgpQr",
                             "PgpEnrollment", "Pairing", "UpdateCheck", "Mfa", "Scheduled", "Invites"}) {
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

}

} // namespace Fixtures
