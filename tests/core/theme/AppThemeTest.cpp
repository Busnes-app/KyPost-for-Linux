#include "theme/AppTheme.h"

#include <QTest>
#include <iterator>

using AppTheme::Mode;
using AppTheme::Selection;

namespace {

// Copied from ky-ui 0.11.0 src/themes.js THEME_PALETTES and src/tokens.css
// (Busnes): bg, panel, ink, inkStrong, accent, accentSoft, line, buttonText,
// danger, warning, success. rgba accentSoft/line are flattened over panel.
struct KyUiRow
{
    const char* id;
    quint32 v[11];
};

const KyUiRow kKyUi[] = {
    {"busnes-light", {0xF8F6F0, 0xFFFFFF, 0x566461, 0x182326, 0xBF3F18, 0xFBF0EC, 0xCCCFCF, 0xFFFFFF, 0xA8174E, 0x7A5F00, 0x267442}},
    {"busnes-dark", {0x182326, 0x1F2B2E, 0xB3BCB8, 0xF2EFE8, 0xF5865F, 0x2B2622, 0x525A5B, 0x182326, 0xFF8FB1, 0xFFD25E, 0x86EFAC}},
    {"light-matter", {0xF5EFE5, 0xFFF8EE, 0x4C3D32, 0x2D1F15, 0x805A39, 0xE2D0B8, 0xD4C7B5, 0xFFFFFF, 0xA8174E, 0x7A5F00, 0x267442}},
    {"dark-matter", {0x1D1B19, 0x292725, 0xCFC6BB, 0xF0E9DF, 0xC29A72, 0x49392C, 0x45403A, 0x24170F, 0xFF8FB1, 0xFFD25E, 0x86EFAC}},
    {"white-cliffs", {0xF7F9FB, 0xFFFFFF, 0x2E4C63, 0x163246, 0x1D5FA6, 0xDDE9F7, 0xC7D6DF, 0xFFFFFF, 0xA8174E, 0x7A5F00, 0x267442}},
    {"ocean", {0x0F1B24, 0x152A36, 0xB8D8E8, 0xE0F2FB, 0x6AA8E8, 0x1F3A55, 0x2F5567, 0x0A1B22, 0xFF8FB1, 0xFFD25E, 0x86EFAC}},
    {"forest-light", {0xF3F6EF, 0xFCFDF9, 0x435342, 0x203322, 0x4B6B37, 0xE2EBDA, 0xC5D0BC, 0xFFFFFF, 0xA8174E, 0x7A5F00, 0x00737A}},
    {"forest", {0x142018, 0x1F2F24, 0xC7DBC7, 0xE3F0DF, 0x8FAA74, 0x3A5837, 0x4F694F, 0x12200F, 0xFF8FB1, 0xFFD25E, 0x6EE0D0}},
    {"polished-ky", {0xEEF2F6, 0xFFFFFF, 0x475569, 0x0F172A, 0x08758B, 0xDAF2F4, 0xC2CDD8, 0xFFFFFF, 0xA8174E, 0x7A5F00, 0x267442}},
    {"patina-ky", {0x10161E, 0x1B2530, 0xAFBDCA, 0xE2E8F0, 0x6FC9CC, 0x203D41, 0x34414D, 0x10161E, 0xFF8FB1, 0xFFD25E, 0x86EFAC}},
    {"paper", {0xECEFF4, 0xFFFFFF, 0x4C566A, 0x2E3440, 0x4A6A94, 0xDBE4EF, 0xC5CDD9, 0xFFFFFF, 0xA8174E, 0x7A5F00, 0x267442}},
    {"nord", {0x2E3440, 0x3B4252, 0xD8DEE9, 0xECEFF4, 0x88C0D0, 0x4A5B6B, 0x4C566A, 0x0D0F14, 0xECC4C8, 0xFFD25E, 0xBCD4AA}},
};

} // namespace

class AppThemeTest : public QObject
{
    Q_OBJECT

private slots:
    void pairsMatchKyUi();
    void halvesMatchKyUi();
    void palettesMatchKyUi();
    void busnesKeepsAndroidAvatarStops();
    void selectionForSavedValue_data();
    void selectionForSavedValue();
    void savedIdRoundTrips();
    void resolveFollowsMode();
    void systemPrefersDark();
    void halfSchemeMatchesLightness();
};

void AppThemeTest::pairsMatchKyUi()
{
    const QList<AppTheme::Pair>& pairs = AppTheme::pairs();
    QCOMPARE(pairs.size(), 6);
    const QStringList ids = {"busnes-pair", "matter-pair", "ocean-pair", "forest-pair", "ky-pair", "nord-pair"};
    const QStringList names = {"Busnes", "Matter", "Ocean", "Forest", "Ky", "Nord"};
    const QStringList lights = {"busnes-light", "light-matter", "white-cliffs", "forest-light", "polished-ky", "paper"};
    const QStringList darks = {"busnes-dark", "dark-matter", "ocean", "forest", "patina-ky", "nord"};
    for (int i = 0; i < pairs.size(); ++i) {
        QCOMPARE(pairs[i].id, ids[i]);
        QCOMPARE(pairs[i].name, names[i]);
        QCOMPARE(pairs[i].lightId, lights[i]);
        QCOMPARE(pairs[i].darkId, darks[i]);
    }
    QCOMPARE(AppTheme::defaultPairId(), QStringLiteral("busnes-pair"));
}

void AppThemeTest::halvesMatchKyUi()
{
    struct Row
    {
        const char* id;
        const char* name;
        bool dark;
        const char* pair;
        QStringList aliases;
    };
    const Row rows[] = {
        {"busnes-light", "Busnes Light", false, "busnes-pair", {}},
        {"busnes-dark", "Busnes Dark", true, "busnes-pair", {}},
        {"light-matter", "Matter Light", false, "matter-pair", {"Light Matter"}},
        {"dark-matter", "Matter Dark", true, "matter-pair", {"Dark Matter", "Current"}},
        {"white-cliffs", "Ocean Light", false, "ocean-pair", {"White Cliffs", "Cliffs"}},
        {"ocean", "Ocean Dark", true, "ocean-pair", {"Ocean"}},
        {"forest-light", "Forest Light", false, "forest-pair", {}},
        {"forest", "Forest Dark", true, "forest-pair", {"Forest"}},
        {"polished-ky", "Ky Light", false, "ky-pair", {"Polished Ky"}},
        {"patina-ky", "Ky Dark", true, "ky-pair", {"Patina Ky", "patina", "Patina Ky (base)"}},
        {"paper", "Nord Light", false, "nord-pair", {"Paper Clean"}},
        {"nord", "Nord Dark", true, "nord-pair", {"Nord", "Nord Slate"}},
    };
    const QList<AppTheme::Half>& halves = AppTheme::halves();
    QCOMPARE(halves.size(), qsizetype(std::size(rows)));
    for (qsizetype i = 0; i < halves.size(); ++i) {
        QCOMPARE(halves[i].id, QString::fromLatin1(rows[i].id));
        QCOMPARE(halves[i].name, QString::fromLatin1(rows[i].name));
        QCOMPARE(halves[i].dark, rows[i].dark);
        QCOMPARE(halves[i].pairId, QString::fromLatin1(rows[i].pair));
        QCOMPARE(halves[i].aliases, rows[i].aliases);
    }
}

void AppThemeTest::palettesMatchKyUi()
{
    QCOMPARE(std::size(kKyUi), size_t(12));
    for (const KyUiRow& row : kKyUi) {
        const AppTheme::Half* half = AppTheme::findHalf(QString::fromLatin1(row.id));
        QVERIFY2(half, row.id);
        const ThemePalette& p = half->palette;
        const quint32 got[11] = {p.bg, p.panel, p.ink, p.inkStrong, p.accent, p.accentSoft,
                                 p.line, p.onAccent, p.danger, p.warning, p.success};
        for (int i = 0; i < 11; ++i)
            QVERIFY2(got[i] == row.v[i], qPrintable(QStringLiteral("%1 field %2").arg(row.id).arg(i)));
    }
}

void AppThemeTest::busnesKeepsAndroidAvatarStops()
{
    const ThemePalette light = AppTheme::findHalf(QStringLiteral("busnes-light"))->palette;
    QCOMPARE(light.avatarStart, 0xBF3F18u);
    QCOMPARE(light.avatarEnd, 0xE65124u);
    QCOMPARE(light.avatarBorder, 0xBF3F18u);
    const ThemePalette dark = AppTheme::findHalf(QStringLiteral("busnes-dark"))->palette;
    QCOMPARE(dark.avatarStart, 0xF5865Fu);
    QCOMPARE(dark.avatarEnd, 0xF26A3Du);
    QCOMPARE(dark.avatarBorder, 0xF5865Fu);
    // Other halves start and ring in their accent.
    for (const AppTheme::Half& h : AppTheme::halves()) {
        QVERIFY2(h.palette.avatarStart == h.palette.accent, qPrintable(h.id));
        QVERIFY2(h.palette.avatarBorder == h.palette.accent, qPrintable(h.id));
    }
}

// The migration: every value an older build could have saved, plus the new IDs.
void AppThemeTest::selectionForSavedValue_data()
{
    QTest::addColumn<QString>("saved");
    QTest::addColumn<QString>("pairId");
    QTest::addColumn<int>("mode");

    const int system = int(Mode::System), light = int(Mode::Light), dark = int(Mode::Dark);
    // Busnes Light/Dark: the Busnes pair with that half forced (owner rule).
    QTest::newRow("Busnes Light") << "Busnes Light" << "busnes-pair" << light;
    QTest::newRow("Busnes Dark") << "Busnes Dark" << "busnes-pair" << dark;
    // The old default.
    QTest::newRow("Follow system") << "Follow system" << "busnes-pair" << system;
    // Old palettes that are now a half keep their look (pairs-spec: a saved
    // half applies that half).
    QTest::newRow("Dark Matter") << "Dark Matter" << "matter-pair" << dark;
    QTest::newRow("Light Matter") << "Light Matter" << "matter-pair" << light;
    QTest::newRow("Ocean") << "Ocean" << "ocean-pair" << dark;
    QTest::newRow("White Cliffs") << "White Cliffs" << "ocean-pair" << light;
    QTest::newRow("Forest") << "Forest" << "forest-pair" << dark;
    QTest::newRow("Patina Ky") << "Patina Ky" << "ky-pair" << dark;
    QTest::newRow("Polished Ky") << "Polished Ky" << "ky-pair" << light;
    // Removed palettes reset to Busnes following the system.
    for (const char* removed : {"Tropics", "Tropic Night", "Coffee", "Cyber Punk", "Neon Purple", "Space", "Sky", "Sun"})
        QTest::newRow(removed) << removed << "busnes-pair" << system;
    // Unknown, empty, case-mangled.
    QTest::newRow("garbage") << "Bogus" << "busnes-pair" << system;
    QTest::newRow("empty") << "" << "busnes-pair" << system;
    QTest::newRow("lowercase name") << "busnes light" << "busnes-pair" << system;
    // What this build saves.
    QTest::newRow("pair id") << "nord-pair" << "nord-pair" << system;
    QTest::newRow("half id") << "forest-light" << "forest-pair" << light;
    QTest::newRow("new name") << "Ky Dark" << "ky-pair" << dark;
}

void AppThemeTest::selectionForSavedValue()
{
    QFETCH(QString, saved);
    QFETCH(QString, pairId);
    QFETCH(int, mode);
    QCOMPARE(AppTheme::selectionFor(saved), (Selection{pairId, Mode(mode)}));
}

void AppThemeTest::savedIdRoundTrips()
{
    for (const AppTheme::Pair& pair : AppTheme::pairs()) {
        for (Mode mode : {Mode::System, Mode::Light, Mode::Dark}) {
            const Selection s{pair.id, mode};
            QCOMPARE(AppTheme::selectionFor(AppTheme::savedId(s)), s);
        }
        QCOMPARE(AppTheme::savedId({pair.id, Mode::System}), pair.id);
        QCOMPARE(AppTheme::savedId({pair.id, Mode::Light}), pair.lightId);
        QCOMPARE(AppTheme::savedId({pair.id, Mode::Dark}), pair.darkId);
    }
}

void AppThemeTest::resolveFollowsMode()
{
    const auto name = [](const Selection& s, bool systemDark) { return AppTheme::resolve(s, systemDark).name; };
    QCOMPARE(name({"ocean-pair", Mode::System}, false), QStringLiteral("Ocean Light"));
    QCOMPARE(name({"ocean-pair", Mode::System}, true), QStringLiteral("Ocean Dark"));
    QCOMPARE(name({"ocean-pair", Mode::Light}, true), QStringLiteral("Ocean Light"));
    QCOMPARE(name({"ocean-pair", Mode::Dark}, false), QStringLiteral("Ocean Dark"));
    QCOMPARE(name({"no-such-pair", Mode::System}, true), QStringLiteral("Busnes Dark"));
    QCOMPARE(AppTheme::palette(QStringLiteral("Sky"), false), AppTheme::findHalf("busnes-light")->palette);
    QCOMPARE(AppTheme::palette(QStringLiteral("Sky"), true), AppTheme::findHalf("busnes-dark")->palette);
}

void AppThemeTest::systemPrefersDark()
{
    using AppTheme::SystemScheme;
    QVERIFY(AppTheme::systemPrefersDark(SystemScheme::Dark, 0xFFFFFF));
    QVERIFY(!AppTheme::systemPrefersDark(SystemScheme::Light, 0x000000));
    QVERIFY(AppTheme::systemPrefersDark(SystemScheme::Unknown, 0x202020));
    QVERIFY(!AppTheme::systemPrefersDark(SystemScheme::Unknown, 0xEFF0F1));
}

void AppThemeTest::halfSchemeMatchesLightness()
{
    for (const AppTheme::Half& h : AppTheme::halves()) {
        QVERIFY2(h.palette.isLight() == !h.dark, qPrintable(h.id));
        QVERIFY2(h.palette.preferredColorScheme() ==
                     (h.dark ? ThemePalette::ColorScheme::Dark : ThemePalette::ColorScheme::Light),
                 qPrintable(h.id));
    }
}

QTEST_APPLESS_MAIN(AppThemeTest)
#include "AppThemeTest.moc"
