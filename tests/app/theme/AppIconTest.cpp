#include "theme/AppIcon.h"

#include <QGuiApplication>
#include <QTest>

// The window icon comes from the binary, so a stale icon installed under the
// app's theme name can never replace it.
class AppIconTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // No icon theme at all: whatever the icon is, it is not from one.
        QIcon::setThemeSearchPaths({});
        QIcon::setFallbackSearchPaths({});
        QIcon::setThemeName(QString());
        QIcon::setFallbackThemeName(QString());
        QVERIFY(QIcon::fromTheme(QStringLiteral("com.kysecurity.mail")).isNull());
    }

    void bundledIconHasEverySize()
    {
        const QIcon icon = bundledAppIcon();
        QVERIFY(!icon.isNull());
        const QList<QSize> sizes = icon.availableSizes();
        for (int size : {16, 22, 24, 32, 48, 64, 128, 256})
            QVERIFY2(sizes.contains(QSize(size, size)), qPrintable(QString::number(size)));
        QCOMPARE(icon.pixmap(QSize(256, 256)).size(), QSize(256, 256));
        QCOMPARE(icon.pixmap(QSize(32, 32)).size(), QSize(32, 32));
    }

    // The app installs it as the application's window icon.
    void applicationWindowIconIsTheBundledOne()
    {
        QGuiApplication::setWindowIcon(bundledAppIcon());
        const QIcon icon = QGuiApplication::windowIcon();
        QVERIFY(!icon.isNull());
        QVERIFY(icon.availableSizes().contains(QSize(32, 32)));
        QVERIFY(icon.availableSizes().contains(QSize(256, 256)));
    }
};

QTEST_MAIN(AppIconTest)
#include "AppIconTest.moc"
