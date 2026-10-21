#include "theme/ThemeController.h"

#include "stores/SettingsStore.h"

#include <QFont>
#include <QGuiApplication>
#include <QPalette>
#include <QSignalSpy>
#include <QStyleHints>
#include <QTemporaryDir>
#include <QTest>

class ThemeControllerTest : public QObject
{
    Q_OBJECT

private slots:
    void cleanup();
    void followSystemSwitchesWithScheme();
    void namedThemeIgnoresScheme();
    void tokens();
    void dangerAffordanceFollowsPalette();
    void fontTokensFollowSystemFont();
    void windowColourDecidesWhenSchemeUnknown();

private:
    QTemporaryDir m_dir;
};

void ThemeControllerTest::cleanup()
{
    QFile::remove(m_dir.filePath(QStringLiteral("settings.ini")));
    QGuiApplication::setFont(QFont());
    QGuiApplication::setPalette(QPalette());
}

void ThemeControllerTest::followSystemSwitchesWithScheme()
{
    SettingsStore store(m_dir.filePath(QStringLiteral("settings.ini")));
    ThemeController theme(store);
    QCOMPARE(theme.themeName(), QStringLiteral("Follow system"));

    theme.setSystemDark(true);
    QCOMPARE(theme.bg(), QColor(QStringLiteral("#182326")));

    QSignalSpy spy(&theme, &ThemeController::themeChanged);
    theme.setSystemDark(false);
    QCOMPARE(theme.bg(), QColor(QStringLiteral("#f8f6f0")));
    QCOMPARE(spy.count(), 1);
    theme.setSystemDark(false);
    QCOMPARE(spy.count(), 1);
}

void ThemeControllerTest::namedThemeIgnoresScheme()
{
    SettingsStore store(m_dir.filePath(QStringLiteral("settings.ini")));
    store.setThemeId(QStringLiteral("Ocean"));
    ThemeController theme(store);
    const QColor ocean = theme.bg();
    QCOMPARE(ocean, QColor(QStringLiteral("#0f1b24")));

    QSignalSpy spy(&theme, &ThemeController::themeChanged);
    theme.setSystemDark(true);
    theme.setSystemDark(false);
    theme.setSystemDark(true);
    QCOMPARE(spy.count(), 0);
    QCOMPARE(theme.bg(), ocean);
}

void ThemeControllerTest::tokens()
{
    SettingsStore store(m_dir.filePath(QStringLiteral("settings.ini")));
    ThemeController theme(store);
    QCOMPARE(theme.themeNames().first(), QStringLiteral("Follow system"));
    QCOMPARE(theme.fieldStroke(), 2);

    theme.setTheme(QStringLiteral("Busnes Light"));
    QCOMPARE(theme.avatarStart(), QColor(QStringLiteral("#bf3f18")));
    QCOMPARE(theme.avatarEnd(), QColor(QStringLiteral("#e65124")));
    QCOMPARE(theme.avatarBorder(), QColor(QStringLiteral("#bf3f18")));
    QCOMPARE(theme.readableOnAccent(), QColor(Qt::white));
    QCOMPARE(theme.successFillColor().rgb() & 0xFFFFFF, 0x7BBF7Bu);
    QCOMPARE(qRound(theme.successFillColor().alphaF() * 100), 12);

    // The mobile swipe surfaces: both are light enough for black text.
    QCOMPARE(theme.readableOn(theme.warningColor()), QColor(Qt::black));
    QCOMPARE(theme.readableOn(theme.dangerColor()), QColor(Qt::black));
    QCOMPARE(theme.readableOn(QColor(QStringLiteral("#182326"))), QColor(Qt::white));
}

void ThemeControllerTest::dangerAffordanceFollowsPalette()
{
    SettingsStore store(m_dir.filePath(QStringLiteral("settings.ini")));
    ThemeController theme(store);
    const auto rgba = [](const QColor& c) {
        return qMakePair(c.rgb() & 0xFFFFFF, qRound(c.alphaF() * 100));
    };

    theme.setSystemDark(true);
    QCOMPARE(rgba(theme.dangerActionFill()), qMakePair(0xFFB4ABu, 12));
    QCOMPARE(rgba(theme.dangerActionBorder()), qMakePair(0xFFB4ABu, 40));
    QCOMPARE(rgba(theme.dangerActionText()), qMakePair(0xFFD8D3u, 100));
    QCOMPARE(rgba(theme.warningActionText()), qMakePair(0xFFF0B8u, 100));

    theme.setSystemDark(false);
    QCOMPARE(rgba(theme.dangerActionFill()), qMakePair(0xFF5F5Fu, 18));
    QCOMPARE(rgba(theme.dangerActionBorder()), qMakePair(0xFF5F5Fu, 65));
    QCOMPARE(rgba(theme.dangerActionText()), qMakePair(0x722A2Au, 100));
    QCOMPARE(rgba(theme.warningActionText()), qMakePair(0x726022u, 100));
}

void ThemeControllerTest::fontTokensFollowSystemFont()
{
    QFont font = QGuiApplication::font();
    font.setPointSize(10);
    QGuiApplication::setFont(font);

    SettingsStore store(m_dir.filePath(QStringLiteral("settings.ini")));
    ThemeController theme(store);
    QCOMPARE(theme.fontScale(), 1.0);
    QCOMPARE(theme.fontWordmark(), 32);
    QCOMPARE(theme.fontSubject(), 18);
    QCOMPARE(theme.fontSender(), 14);
    QCOMPARE(theme.fontPreview(), 13);
    QCOMPARE(theme.fontDate(), 12);
    QCOMPARE(theme.fontEyebrow(), 11);
    QCOMPARE(theme.eyebrowLetterSpacing(), 0.88);

    QSignalSpy spy(&theme, &ThemeController::fontScaleChanged);
    font.setPointSize(12);
    QGuiApplication::setFont(font);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(theme.fontScale(), 1.2);
    QCOMPARE(theme.fontSubject(), 22);
    QCOMPARE(theme.fontDate(), 14);

    // A pixel-sized font scales from 96 dpi.
    font.setPixelSize(16);
    QGuiApplication::setFont(font);
    QCOMPARE(spy.count(), 1); // 16 px is 12 pt: no change
    QCOMPARE(theme.fontSubject(), 22);
}

void ThemeControllerTest::windowColourDecidesWhenSchemeUnknown()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (QGuiApplication::styleHints()->colorScheme() != Qt::ColorScheme::Unknown)
        QSKIP("the platform reports a colour scheme, so the window colour is not consulted");
#endif
    SettingsStore store(m_dir.filePath(QStringLiteral("settings.ini")));
    ThemeController theme(store);

    QPalette palette;
    palette.setColor(QPalette::Window, QColor(0x20, 0x20, 0x20));
    QGuiApplication::setPalette(palette);
    QCOMPARE(theme.bg(), QColor(QStringLiteral("#182326")));

    palette.setColor(QPalette::Window, QColor(0xEF, 0xF0, 0xF1));
    QGuiApplication::setPalette(palette);
    QCOMPARE(theme.bg(), QColor(QStringLiteral("#f8f6f0")));
}

QTEST_MAIN(ThemeControllerTest)
#include "ThemeControllerTest.moc"
