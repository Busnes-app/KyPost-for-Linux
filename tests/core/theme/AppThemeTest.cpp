#include "theme/AppTheme.h"

#include <QSet>
#include <iterator>
#include <QTest>

namespace {

const QString kFollow = QStringLiteral("Follow system");
const QString kBusnesLight = QStringLiteral("Busnes Light");
const QString kBusnesDark = QStringLiteral("Busnes Dark");

// Copied from kypost-android origin/main AppTheme.kt themePalettes:
// bg, panel, ink, inkStrong, accent, line, avatarStart, avatarEnd, avatarBorder.
struct AndroidRow
{
    const char* name;
    quint32 v[9];
};

const AndroidRow kAndroid[] = {
    {"Busnes Light", {0xF8F6F0, 0xFFFFFF, 0x566461, 0x182326, 0xBF3F18, 0xCCCFCF, 0xBF3F18, 0xE65124, 0xBF3F18}},
    {"Busnes Dark", {0x182326, 0x1F2B2E, 0xB3BCB8, 0xF2EFE8, 0xF5865F, 0x525A5B, 0xF5865F, 0xF26A3D, 0xF5865F}},
    {"Dark Matter", {0x1A1A1E, 0x252530, 0xD4C5E2, 0xE8DDF5, 0xC29A72, 0x404050, 0xC29A72, 0x9A7450, 0x8F6B4A}},
    {"Light Matter", {0xF5EFE5, 0xFFF8EE, 0x4C3D32, 0x2D1F15, 0xC29A72, 0xC5B29D, 0xC29A72, 0x9A7450, 0x8F6B4A}},
    {"Tropics", {0xF4F1EB, 0xFFFAF0, 0x43362D, 0x241A14, 0x9BC400, 0xC4B7A3, 0x9BC400, 0x7EA100, 0x78A100}},
    {"Tropic Night", {0x15131A, 0x221F2B, 0xCDBDE0, 0xE8DDF5, 0x9BC400, 0x3C3650, 0x9BC400, 0x7EA100, 0x78A100}},
    {"Ocean", {0x0F1B24, 0x152A36, 0xB8D8E8, 0xE0F2FB, 0x5EA9BE, 0x2F5567, 0x74BACD, 0x4F91A6, 0x4F91A6}},
    {"Coffee", {0x1D1714, 0x2A211D, 0xD6C0B3, 0xF0DED2, 0xB47F5C, 0x4A3830, 0xB47F5C, 0x8F5F42, 0x8F5F42}},
    {"White Cliffs", {0xF7F9FB, 0xFFFFFF, 0x2E4C63, 0x163246, 0x5EA8D8, 0x8FC3DF, 0x4F9BC8, 0x58B65A, 0x2F7FB0}},
    {"Cyber Punk", {0x120918, 0x1E1028, 0xF5D0FF, 0xFFE9FF, 0x00F5D4, 0x5C2D84, 0x00F5D4, 0x00C9AD, 0x00C9AD}},
    {"Neon Purple", {0x130B1D, 0x231233, 0xE4CCFF, 0xF2E6FF, 0xC86CFF, 0x63358A, 0xC86CFF, 0x9D45D3, 0x9D45D3}},
    {"Space", {0x0B0F1A, 0x151C2D, 0xC8D5F0, 0xE7EFFF, 0x86A8FF, 0x34496F, 0x86A8FF, 0x6788DD, 0x6788DD}},
    {"Sky", {0xDFF1FF, 0xF4FBFF, 0x2F4F64, 0x183142, 0x6DB3D6, 0x93BDD2, 0x6DB3D6, 0x4F93B8, 0x4F93B8}},
    {"Forest", {0x142018, 0x1F2F24, 0xC7DBC7, 0xE3F0DF, 0x8FAA74, 0x4F694F, 0x8FAA74, 0x6F8D5A, 0x6F8D5A}},
    {"Sun", {0xFFF3DC, 0xFFF9EC, 0x5A4024, 0x392611, 0xE0AB4F, 0xD4B27A, 0xE0AB4F, 0xBB8631, 0xBB8631}},
    {"Patina Ky", {0x0D0F14, 0x161A22, 0x64748B, 0xE2E8F0, 0x4DEEEA, 0x1E293B, 0x4DEEEA, 0x10B981, 0x0E9668}},
    {"Polished Ky", {0xEEF2F6, 0xFFFFFF, 0x475569, 0x0F172A, 0x0891B2, 0xCBD5E1, 0x0891B2, 0x10B981, 0x059669}},
};

} // namespace

class AppThemeTest : public QObject
{
    Q_OBJECT

private slots:
    void themeNamesMatchAndroidList();
    void busnesPalettes();
    void allPalettesMatchAndroid();
    void existingAccentSoftUnchanged();
    void followSystemResolvesBusnesPair();
    void systemPrefersDark();
    void lightThemesGetLightColorScheme();
};

void AppThemeTest::themeNamesMatchAndroidList()
{
    const QStringList expected = {
        kFollow,
        kBusnesLight,
        kBusnesDark,
        QStringLiteral("Dark Matter"),
        QStringLiteral("Light Matter"),
        QStringLiteral("Tropics"),
        QStringLiteral("Tropic Night"),
        QStringLiteral("Ocean"),
        QStringLiteral("Coffee"),
        QStringLiteral("White Cliffs"),
        QStringLiteral("Cyber Punk"),
        QStringLiteral("Neon Purple"),
        QStringLiteral("Space"),
        QStringLiteral("Sky"),
        QStringLiteral("Forest"),
        QStringLiteral("Sun"),
        QStringLiteral("Patina Ky"),
        QStringLiteral("Polished Ky"),
    };
    QCOMPARE(AppTheme::themeNames().size(), 18);
    QCOMPARE(AppTheme::themeNames(), expected);
    QCOMPARE(AppTheme::defaultThemeName(), kFollow);
}

void AppThemeTest::busnesPalettes()
{
    const ThemePalette light = AppTheme::palette(kBusnesLight, true);
    QCOMPARE(light, (ThemePalette{0xF8F6F0, 0xFFFFFF, 0x566461, 0x182326, 0xBF3F18, 0xEBC5B9, 0xCCCFCF,
                                  0xBF3F18, 0xE65124, 0xBF3F18}));
    const ThemePalette dark = AppTheme::palette(kBusnesDark, false);
    QCOMPARE(dark, (ThemePalette{0x182326, 0x1F2B2E, 0xB3BCB8, 0xF2EFE8, 0xF5865F, 0x5F463C, 0x525A5B,
                                 0xF5865F, 0xF26A3D, 0xF5865F}));
}

void AppThemeTest::allPalettesMatchAndroid()
{
    QCOMPARE(std::size(kAndroid), size_t(17));
    for (const AndroidRow& row : kAndroid) {
        const QString name = QString::fromLatin1(row.name);
        QVERIFY2(AppTheme::themeNames().contains(name), row.name);
        const ThemePalette p = AppTheme::palette(name, false);
        const quint32 got[9] = {p.bg, p.panel, p.ink, p.inkStrong, p.accent, p.line,
                                p.avatarStart, p.avatarEnd, p.avatarBorder};
        for (int i = 0; i < 9; ++i)
            QVERIFY2(got[i] == row.v[i], qPrintable(QStringLiteral("%1 field %2").arg(name).arg(i)));
    }
}

void AppThemeTest::existingAccentSoftUnchanged()
{
    const QList<QPair<QString, quint32>> expected = {
        {QStringLiteral("Dark Matter"), 0x5A3F31}, {QStringLiteral("Light Matter"), 0xE6D2BE},
        {QStringLiteral("Tropics"), 0xD4E3A0},     {QStringLiteral("Tropic Night"), 0x6B4A42},
        {QStringLiteral("Ocean"), 0x214657},       {QStringLiteral("Coffee"), 0x5F3F2F},
        {QStringLiteral("White Cliffs"), 0xDFF1FB}, {QStringLiteral("Cyber Punk"), 0x3B1760},
        {QStringLiteral("Neon Purple"), 0x47206C}, {QStringLiteral("Space"), 0x263E74},
        {QStringLiteral("Sky"), 0xB6DCED},         {QStringLiteral("Forest"), 0x3A5837},
        {QStringLiteral("Sun"), 0xF1D9A2},         {QStringLiteral("Patina Ky"), 0x0E4A48},
        {QStringLiteral("Polished Ky"), 0xCFFAFE},
    };
    for (const auto& [name, accentSoft] : expected)
        QVERIFY2(AppTheme::palette(name, true).accentSoft == accentSoft, qPrintable(name));
}

void AppThemeTest::followSystemResolvesBusnesPair()
{
    const ThemePalette light = AppTheme::palette(kBusnesLight, false);
    const ThemePalette dark = AppTheme::palette(kBusnesDark, false);
    QVERIFY(light != dark);
    QCOMPARE(AppTheme::palette(kFollow, true), dark);
    QCOMPARE(AppTheme::palette(kFollow, false), light);
    QCOMPARE(AppTheme::palette(QStringLiteral("Nope"), true), dark);
    QCOMPARE(AppTheme::palette(QStringLiteral("Nope"), false), light);
    QCOMPARE(AppTheme::palette(QString(), false), light);
    // A named palette ignores the system scheme.
    for (const QString& name : AppTheme::themeNames().mid(1))
        QVERIFY2(AppTheme::palette(name, true) == AppTheme::palette(name, false), qPrintable(name));
    QCOMPARE(AppTheme::palette(kBusnesLight, true), light);
}

void AppThemeTest::systemPrefersDark()
{
    using AppTheme::SystemScheme;
    QVERIFY(AppTheme::systemPrefersDark(SystemScheme::Dark, 0xFFFFFF));
    QVERIFY(!AppTheme::systemPrefersDark(SystemScheme::Light, 0x000000));
    QVERIFY(AppTheme::systemPrefersDark(SystemScheme::Unknown, 0x202020));
    QVERIFY(!AppTheme::systemPrefersDark(SystemScheme::Unknown, 0xEFF0F1));
}

void AppThemeTest::lightThemesGetLightColorScheme()
{
    const QSet<QString> lightThemes = {
        kBusnesLight,
        QStringLiteral("Light Matter"),
        QStringLiteral("Tropics"),
        QStringLiteral("White Cliffs"),
        QStringLiteral("Sky"),
        QStringLiteral("Sun"),
        QStringLiteral("Polished Ky"),
    };

    for (const QString& name : AppTheme::themeNames().mid(1)) {
        const ThemePalette p = AppTheme::palette(name, false);
        const bool expectedLight = lightThemes.contains(name);
        QVERIFY2(p.isLight() == expectedLight, qPrintable(name));
        QVERIFY2(p.preferredColorScheme() ==
                     (expectedLight ? ThemePalette::ColorScheme::Light : ThemePalette::ColorScheme::Dark),
                 qPrintable(name));
    }
}

QTEST_APPLESS_MAIN(AppThemeTest)
#include "AppThemeTest.moc"
