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
    void forcedHalfIgnoresScheme();
    void setPairAndModePersist();
    void pairsListsSixWithBothHalves();
    void tokens();
    void statusColoursFollowHalf();
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
    QCOMPARE(theme.pairId(), QStringLiteral("busnes-pair"));
    QCOMPARE(theme.mode(), QStringLiteral("system"));

    theme.setSystemDark(true);
    QCOMPARE(theme.bg(), QColor(QStringLiteral("#182326")));
    QCOMPARE(theme.halfName(), QStringLiteral("Busnes Dark"));

    QSignalSpy spy(&theme, &ThemeController::themeChanged);
    theme.setSystemDark(false);
    QCOMPARE(theme.bg(), QColor(QStringLiteral("#f8f6f0")));
    QCOMPARE(spy.count(), 1);
    theme.setSystemDark(false);
    QCOMPARE(spy.count(), 1);
}

void ThemeControllerTest::forcedHalfIgnoresScheme()
{
    SettingsStore store(m_dir.filePath(QStringLiteral("settings.ini")));
    store.setThemeId(QStringLiteral("Ocean")); // an old save: Ocean Dark, forced
    ThemeController theme(store);
    QCOMPARE(theme.pairId(), QStringLiteral("ocean-pair"));
    QCOMPARE(theme.mode(), QStringLiteral("dark"));
    const QColor ocean = theme.bg();
    QCOMPARE(ocean, QColor(QStringLiteral("#0f1b24")));

    QSignalSpy spy(&theme, &ThemeController::themeChanged);
    theme.setSystemDark(true);
    theme.setSystemDark(false);
    theme.setSystemDark(true);
    QCOMPARE(spy.count(), 0);
    QCOMPARE(theme.bg(), ocean);
}

void ThemeControllerTest::setPairAndModePersist()
{
    const QString path = m_dir.filePath(QStringLiteral("settings.ini"));
    SettingsStore store(path);
    ThemeController theme(store);
    theme.setSystemDark(false);
    QSignalSpy spy(&theme, &ThemeController::themeChanged);

    theme.setPair(QStringLiteral("nord-pair"));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(theme.bg(), QColor(QStringLiteral("#eceff4")));
    theme.setMode(QStringLiteral("dark"));
    QCOMPARE(spy.count(), 2);
    QCOMPARE(theme.halfName(), QStringLiteral("Nord Dark"));
    QCOMPARE(store.themeId(), QStringLiteral("nord"));

    theme.setMode(QStringLiteral("sepia"));
    theme.setPair(QStringLiteral("Sky"));
    theme.setMode(QStringLiteral("dark"));
    QCOMPARE(spy.count(), 2);
    QCOMPARE(store.themeId(), QStringLiteral("nord"));
}

void ThemeControllerTest::pairsListsSixWithBothHalves()
{
    SettingsStore store(m_dir.filePath(QStringLiteral("settings.ini")));
    ThemeController theme(store);
    const QVariantList pairs = theme.pairs();
    QCOMPARE(pairs.size(), 6);
    const QVariantMap forest = pairs.at(3).toMap();
    QCOMPARE(forest.value(QStringLiteral("id")).toString(), QStringLiteral("forest-pair"));
    QCOMPARE(forest.value(QStringLiteral("name")).toString(), QStringLiteral("Forest"));
    QCOMPARE(forest.value(QStringLiteral("light")).toMap().value(QStringLiteral("accent")).value<QColor>(),
             QColor(QStringLiteral("#4b6b37")));
    QCOMPARE(forest.value(QStringLiteral("dark")).toMap().value(QStringLiteral("bg")).value<QColor>(),
             QColor(QStringLiteral("#142018")));
}

void ThemeControllerTest::tokens()
{
    SettingsStore store(m_dir.filePath(QStringLiteral("settings.ini")));
    ThemeController theme(store);
    QCOMPARE(theme.fieldStroke(), 2);

    theme.setMode(QStringLiteral("light"));
    QCOMPARE(theme.avatarStart(), QColor(QStringLiteral("#bf3f18")));
    QCOMPARE(theme.avatarEnd(), QColor(QStringLiteral("#e65124")));
    QCOMPARE(theme.avatarBorder(), QColor(QStringLiteral("#bf3f18")));
    QCOMPARE(theme.readableOnAccent(), QColor(Qt::white));
    QCOMPARE(theme.accentSoft(), QColor(QStringLiteral("#fbf0ec")));
    // Selection is accent-soft (the QML adds the accent bar).
    QCOMPARE(theme.selection(), theme.accentSoft());

    // The mobile swipe surfaces: white on the light half's danger and warning.
    QCOMPARE(theme.readableOn(theme.warning()), QColor(Qt::white));
    QCOMPARE(theme.readableOn(theme.danger()), QColor(Qt::white));
    QCOMPARE(theme.readableOn(QColor(QStringLiteral("#182326"))), QColor(Qt::white));
}

void ThemeControllerTest::statusColoursFollowHalf()
{
    SettingsStore store(m_dir.filePath(QStringLiteral("settings.ini")));
    ThemeController theme(store);

    theme.setSystemDark(true);
    QCOMPARE(theme.danger(), QColor(QStringLiteral("#ff8fb1")));
    QCOMPARE(theme.dangerSoft(), QColor(QStringLiteral("#3a373e")));
    QCOMPARE(theme.warning(), QColor(QStringLiteral("#ffd25e")));
    QCOMPARE(theme.success(), QColor(QStringLiteral("#86efac")));
    QCOMPARE(theme.successSoft(), QColor(QStringLiteral("#2b433d")));

    theme.setSystemDark(false);
    QCOMPARE(theme.danger(), QColor(QStringLiteral("#a8174e")));
    QCOMPARE(theme.dangerSoft(), QColor(QStringLiteral("#f5e3ea")));
    QCOMPARE(theme.warning(), QColor(QStringLiteral("#7a5f00")));
    QCOMPARE(theme.success(), QColor(QStringLiteral("#267442")));
    QCOMPARE(theme.dangerBorder(), QColor(QStringLiteral("#d48ba7")));

    theme.setPair(QStringLiteral("forest-pair"));
    QCOMPARE(theme.success(), QColor(QStringLiteral("#00737a")));
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
    QCOMPARE(theme.fontBody(), 16);
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
