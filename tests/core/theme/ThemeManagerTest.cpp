#include "theme/ThemeManager.h"

#include "stores/SettingsStore.h"

#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

using AppTheme::Mode;
using AppTheme::Selection;

namespace {
ThemePalette half(const char* id)
{
    return AppTheme::findHalf(QString::fromLatin1(id))->palette;
}

// The raw stored value, bypassing SettingsStore's fallback.
QVariant storedThemeId(const QString& path)
{
    return QSettings(path, QSettings::IniFormat).value(QStringLiteral("appearance/themeId"));
}
} // namespace

class ThemeManagerTest : public QObject
{
    Q_OBJECT

private slots:
    void freshStoreIsBusnesFollowingSystem();
    void setSystemDarkSwitchesSystemMode();
    void migratesSavedValueWithoutWriting_data();
    void migratesSavedValueWithoutWriting();
    void forcedHalfIgnoresSystemDark();
    void setPairKeepsModeAndPersists();
    void setModePersistsHalfId();
    void setPairIgnoresUnknownPair();
    void persistenceRoundTripsAcrossManagers();
};

void ThemeManagerTest::freshStoreIsBusnesFollowingSystem()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("settings.ini"));
    SettingsStore store(path);

    ThemeManager dark(store, true);
    QCOMPARE(dark.selection(), (Selection{"busnes-pair", Mode::System}));
    QCOMPARE(dark.palette(), half("busnes-dark"));

    ThemeManager light(store, false);
    QCOMPARE(light.palette(), half("busnes-light"));
    QCOMPARE(light.half().name, QStringLiteral("Busnes Light"));

    QVERIFY(!storedThemeId(path).isValid());
}

void ThemeManagerTest::setSystemDarkSwitchesSystemMode()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    SettingsStore store(dir.filePath(QStringLiteral("settings.ini")));
    store.setThemeId(QStringLiteral("forest-pair"));

    ThemeManager manager(store, false);
    QCOMPARE(manager.palette(), half("forest-light"));
    manager.setSystemDark(true);
    QCOMPARE(manager.palette(), half("forest"));
    manager.setSystemDark(false);
    QCOMPARE(manager.palette(), half("forest-light"));
}

// What an older build saved under appearance/themeId, what it paints now
// under a light and a dark desktop. The stored value is left as it was.
void ThemeManagerTest::migratesSavedValueWithoutWriting_data()
{
    QTest::addColumn<QString>("saved");
    QTest::addColumn<QString>("onLight");
    QTest::addColumn<QString>("onDark");

    QTest::newRow("Busnes Light forced") << "Busnes Light" << "busnes-light" << "busnes-light";
    QTest::newRow("Busnes Dark forced") << "Busnes Dark" << "busnes-dark" << "busnes-dark";
    QTest::newRow("Follow system") << "Follow system" << "busnes-light" << "busnes-dark";
    QTest::newRow("Ocean keeps its half") << "Ocean" << "ocean" << "ocean";
    QTest::newRow("Polished Ky keeps its half") << "Polished Ky" << "polished-ky" << "polished-ky";
    QTest::newRow("removed Sky") << "Sky" << "busnes-light" << "busnes-dark";
    QTest::newRow("removed Cyber Punk") << "Cyber Punk" << "busnes-light" << "busnes-dark";
    QTest::newRow("garbage") << "Bogus" << "busnes-light" << "busnes-dark";
}

void ThemeManagerTest::migratesSavedValueWithoutWriting()
{
    QFETCH(QString, saved);
    QFETCH(QString, onLight);
    QFETCH(QString, onDark);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("settings.ini"));
    SettingsStore store(path);
    store.setThemeId(saved);

    ThemeManager manager(store, false);
    QCOMPARE(manager.palette(), AppTheme::findHalf(onLight)->palette);
    manager.setSystemDark(true);
    QCOMPARE(manager.palette(), AppTheme::findHalf(onDark)->palette);
    QCOMPARE(storedThemeId(path).toString(), saved);
}

void ThemeManagerTest::forcedHalfIgnoresSystemDark()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    SettingsStore store(dir.filePath(QStringLiteral("settings.ini")));
    store.setThemeId(QStringLiteral("paper"));

    ThemeManager manager(store, true);
    QCOMPARE(manager.selection(), (Selection{"nord-pair", Mode::Light}));
    QCOMPARE(manager.palette(), half("paper"));
    manager.setSystemDark(false);
    QCOMPARE(manager.palette(), half("paper"));
}

void ThemeManagerTest::setPairKeepsModeAndPersists()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("settings.ini"));
    SettingsStore store(path);
    store.setThemeId(QStringLiteral("Busnes Dark"));

    ThemeManager manager(store, false);
    manager.setPair(QStringLiteral("ocean-pair"));
    QCOMPARE(manager.selection(), (Selection{"ocean-pair", Mode::Dark}));
    QCOMPARE(manager.palette(), half("ocean"));
    QCOMPARE(storedThemeId(path).toString(), QStringLiteral("ocean"));
}

void ThemeManagerTest::setModePersistsHalfId()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("settings.ini"));
    SettingsStore store(path);

    ThemeManager manager(store, true);
    manager.setMode(Mode::Light);
    QCOMPARE(manager.palette(), half("busnes-light"));
    QCOMPARE(storedThemeId(path).toString(), QStringLiteral("busnes-light"));
    manager.setMode(Mode::System);
    QCOMPARE(manager.palette(), half("busnes-dark"));
    QCOMPARE(storedThemeId(path).toString(), QStringLiteral("busnes-pair"));
}

void ThemeManagerTest::setPairIgnoresUnknownPair()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("settings.ini"));
    SettingsStore store(path);
    store.setThemeId(QStringLiteral("ky-pair"));

    ThemeManager manager(store, false);
    for (const QString& bogus : {QStringLiteral("Sky"), QStringLiteral("ocean"), QStringLiteral("Ocean"), QString()})
        manager.setPair(bogus);
    QCOMPARE(manager.selection(), (Selection{"ky-pair", Mode::System}));
    QCOMPARE(storedThemeId(path).toString(), QStringLiteral("ky-pair"));
}

void ThemeManagerTest::persistenceRoundTripsAcrossManagers()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("settings.ini"));
    {
        SettingsStore store(path);
        ThemeManager manager(store, false);
        manager.setPair(QStringLiteral("matter-pair"));
        manager.setMode(Mode::Dark);
    }
    SettingsStore store(path);
    ThemeManager manager(store, false);
    QCOMPARE(manager.selection(), (Selection{"matter-pair", Mode::Dark}));
    QCOMPARE(manager.palette(), half("dark-matter"));
}

QTEST_APPLESS_MAIN(ThemeManagerTest)
#include "ThemeManagerTest.moc"
