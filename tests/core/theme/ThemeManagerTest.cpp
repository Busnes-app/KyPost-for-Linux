#include "theme/ThemeManager.h"

#include "stores/SettingsStore.h"

#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

namespace {
const QString kFollow = QStringLiteral("Follow system");

ThemePalette named(const char* name)
{
    return AppTheme::palette(QString::fromLatin1(name), false);
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
    void freshStoreFollowsSystem();
    void setSystemDarkSwitchesFollowSystem();
    void savedNameIgnoresSystemDark_data();
    void savedNameIgnoresSystemDark();
    void unknownSavedNameFollowsSystemWithoutWriting();
    void setThemeFollowSystemPersists();
    void setThemeIgnoresUnknownName();
    void persistenceRoundTripsAcrossManagers();
};

void ThemeManagerTest::freshStoreFollowsSystem()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("settings.ini"));
    SettingsStore store(path);

    ThemeManager dark(store, true);
    QCOMPARE(dark.themeName(), kFollow);
    QCOMPARE(dark.palette(), named("Busnes Dark"));

    ThemeManager light(store, false);
    QCOMPARE(light.themeName(), kFollow);
    QCOMPARE(light.palette(), named("Busnes Light"));

    QVERIFY(!storedThemeId(path).isValid());
}

void ThemeManagerTest::setSystemDarkSwitchesFollowSystem()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    SettingsStore store(dir.filePath(QStringLiteral("settings.ini")));

    ThemeManager manager(store, false);
    manager.setSystemDark(true);
    QCOMPARE(manager.palette(), named("Busnes Dark"));
    manager.setSystemDark(false);
    QCOMPARE(manager.palette(), named("Busnes Light"));
    QCOMPARE(manager.themeName(), kFollow);
}

void ThemeManagerTest::savedNameIgnoresSystemDark_data()
{
    QTest::addColumn<QString>("name");
    for (const QString& name : AppTheme::themeNames().mid(1))
        QTest::newRow(qPrintable(name)) << name;
}

void ThemeManagerTest::savedNameIgnoresSystemDark()
{
    QFETCH(QString, name);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("settings.ini"));
    SettingsStore store(path);
    store.setThemeId(name);

    ThemeManager manager(store, true);
    QCOMPARE(manager.themeName(), name);
    QCOMPARE(manager.palette(), AppTheme::palette(name, false));
    manager.setSystemDark(false);
    QCOMPARE(manager.palette(), AppTheme::palette(name, false));
    manager.setSystemDark(true);
    QCOMPARE(manager.palette(), AppTheme::palette(name, false));
    QCOMPARE(storedThemeId(path).toString(), name);
}

void ThemeManagerTest::unknownSavedNameFollowsSystemWithoutWriting()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("settings.ini"));
    SettingsStore store(path);
    store.setThemeId(QStringLiteral("Bogus"));

    ThemeManager manager(store, true);
    QCOMPARE(manager.themeName(), kFollow);
    QCOMPARE(manager.palette(), named("Busnes Dark"));
    QCOMPARE(storedThemeId(path).toString(), QStringLiteral("Bogus"));
}

void ThemeManagerTest::setThemeFollowSystemPersists()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("settings.ini"));
    SettingsStore store(path);
    store.setThemeId(QStringLiteral("Ocean"));

    ThemeManager manager(store, false);
    manager.setTheme(kFollow);
    QCOMPARE(manager.themeName(), kFollow);
    QCOMPARE(manager.palette(), named("Busnes Light"));
    QCOMPARE(storedThemeId(path).toString(), kFollow);
}

void ThemeManagerTest::setThemeIgnoresUnknownName()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    SettingsStore store(dir.filePath(QStringLiteral("settings.ini")));

    ThemeManager manager(store, false);
    manager.setTheme(QStringLiteral("Ocean"));
    manager.setTheme(QStringLiteral("Not A Theme"));

    QCOMPARE(manager.themeName(), QStringLiteral("Ocean"));
    QCOMPARE(manager.palette(), named("Ocean"));
}

void ThemeManagerTest::persistenceRoundTripsAcrossManagers()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("settings.ini"));
    SettingsStore store(path);

    ThemeManager first(store, false);
    first.setTheme(QStringLiteral("Ocean"));

    SettingsStore secondStore(path);
    ThemeManager second(secondStore, true);
    QCOMPARE(second.themeName(), QStringLiteral("Ocean"));
}

QTEST_GUILESS_MAIN(ThemeManagerTest)
#include "ThemeManagerTest.moc"
