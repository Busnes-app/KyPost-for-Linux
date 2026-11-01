#include "theme/AppTheme.h"

namespace {

// ky-ui tokens.css :root status defaults, chosen by each half's own scheme.
constexpr quint32 kDangerLight = 0xA8174E;
constexpr quint32 kDangerDark = 0xFF8FB1;
constexpr quint32 kWarningLight = 0x7A5F00;
constexpr quint32 kWarningDark = 0xFFD25E;
constexpr quint32 kSuccessLight = 0x267442;
constexpr quint32 kSuccessDark = 0x86EFAC;

struct Colours
{
    quint32 bg, panel, ink, inkStrong, accent, accentSoft, line, onAccent;
};

// A half with the shared status colours for its scheme, and avatar stops
// from its accent: accent toward 15 % inkStrong, ringed in accent.
ThemePalette half(const Colours& c, bool dark, quint32 success = 0, quint32 danger = 0)
{
    return ThemePalette{
        .bg = c.bg,
        .panel = c.panel,
        .ink = c.ink,
        .inkStrong = c.inkStrong,
        .accent = c.accent,
        .accentSoft = c.accentSoft,
        .line = c.line,
        .onAccent = c.onAccent,
        .danger = danger ? danger : (dark ? kDangerDark : kDangerLight),
        .warning = dark ? kWarningDark : kWarningLight,
        .success = success ? success : (dark ? kSuccessDark : kSuccessLight),
        .avatarStart = c.accent,
        .avatarEnd = ThemePalette::mix(c.accent, 85, c.inkStrong),
        .avatarBorder = c.accent,
    };
}

QList<AppTheme::Half> buildHalves()
{
    using AppTheme::Half;
    // Busnes: tokens.css. line and accentSoft over panel are flattened
    // there; avatar stops stay Android's.
    ThemePalette busnesLight = half({0xF8F6F0, 0xFFFFFF, 0x566461, 0x182326, 0xBF3F18, 0xFBF0EC, 0xCCCFCF, 0xFFFFFF},
                                    false);
    busnesLight.avatarStart = 0xBF3F18;
    busnesLight.avatarEnd = 0xE65124;
    busnesLight.avatarBorder = 0xBF3F18;
    ThemePalette busnesDark = half({0x182326, 0x1F2B2E, 0xB3BCB8, 0xF2EFE8, 0xF5865F, 0x2B2622, 0x525A5B, 0x182326},
                                   true);
    busnesDark.avatarStart = 0xF5865F;
    busnesDark.avatarEnd = 0xF26A3D;
    busnesDark.avatarBorder = 0xF5865F;

    const auto s = [](const char* text) { return QString::fromLatin1(text); };
    return {
        {s("busnes-light"), s("Busnes Light"), false, s("busnes-pair"), {}, busnesLight},
        {s("busnes-dark"), s("Busnes Dark"), true, s("busnes-pair"), {}, busnesDark},
        {s("light-matter"), s("Matter Light"), false, s("matter-pair"), {s("Light Matter")},
         half({0xF5EFE5, 0xFFF8EE, 0x4C3D32, 0x2D1F15, 0x805A39, 0xE2D0B8, 0xD4C7B5, 0xFFFFFF}, false)},
        {s("dark-matter"), s("Matter Dark"), true, s("matter-pair"), {s("Dark Matter"), s("Current")},
         half({0x1D1B19, 0x292725, 0xCFC6BB, 0xF0E9DF, 0xC29A72, 0x49392C, 0x45403A, 0x24170F}, true)},
        {s("white-cliffs"), s("Ocean Light"), false, s("ocean-pair"), {s("White Cliffs"), s("Cliffs")},
         half({0xF7F9FB, 0xFFFFFF, 0x2E4C63, 0x163246, 0x1D5FA6, 0xDDE9F7, 0xC7D6DF, 0xFFFFFF}, false)},
        {s("ocean"), s("Ocean Dark"), true, s("ocean-pair"), {s("Ocean")},
         half({0x0F1B24, 0x152A36, 0xB8D8E8, 0xE0F2FB, 0x6AA8E8, 0x1F3A55, 0x2F5567, 0x0A1B22}, true)},
        // Forest's success is teal: the shared green sits too close to its accent.
        {s("forest-light"), s("Forest Light"), false, s("forest-pair"), {},
         half({0xF3F6EF, 0xFCFDF9, 0x435342, 0x203322, 0x4B6B37, 0xE2EBDA, 0xC5D0BC, 0xFFFFFF}, false, 0x00737A)},
        {s("forest"), s("Forest Dark"), true, s("forest-pair"), {s("Forest")},
         half({0x142018, 0x1F2F24, 0xC7DBC7, 0xE3F0DF, 0x8FAA74, 0x3A5837, 0x4F694F, 0x12200F}, true, 0x6EE0D0)},
        {s("polished-ky"), s("Ky Light"), false, s("ky-pair"), {s("Polished Ky")},
         half({0xEEF2F6, 0xFFFFFF, 0x475569, 0x0F172A, 0x08758B, 0xDAF2F4, 0xC2CDD8, 0xFFFFFF}, false)},
        {s("patina-ky"), s("Ky Dark"), true, s("ky-pair"), {s("Patina Ky"), s("patina"), s("Patina Ky (base)")},
         half({0x10161E, 0x1B2530, 0xAFBDCA, 0xE2E8F0, 0x6FC9CC, 0x203D41, 0x34414D, 0x10161E}, true)},
        {s("paper"), s("Nord Light"), false, s("nord-pair"), {s("Paper Clean")},
         half({0xECEFF4, 0xFFFFFF, 0x4C566A, 0x2E3440, 0x4A6A94, 0xDBE4EF, 0xC5CDD9, 0xFFFFFF}, false)},
        // Nord keeps its own danger and success; accentSoft is rgba(136,192,208,.2) over panel.
        {s("nord"), s("Nord Dark"), true, s("nord-pair"), {s("Nord"), s("Nord Slate")},
         half({0x2E3440, 0x3B4252, 0xD8DEE9, 0xECEFF4, 0x88C0D0, 0x4A5B6B, 0x4C566A, 0x0D0F14}, true, 0xBCD4AA,
              0xECC4C8)},
    };
}

} // namespace

namespace AppTheme {

const QList<Pair>& pairs()
{
    static const QList<Pair> list = {
        {QStringLiteral("busnes-pair"), QStringLiteral("Busnes"), QStringLiteral("busnes-light"),
         QStringLiteral("busnes-dark")},
        {QStringLiteral("matter-pair"), QStringLiteral("Matter"), QStringLiteral("light-matter"),
         QStringLiteral("dark-matter")},
        {QStringLiteral("ocean-pair"), QStringLiteral("Ocean"), QStringLiteral("white-cliffs"),
         QStringLiteral("ocean")},
        {QStringLiteral("forest-pair"), QStringLiteral("Forest"), QStringLiteral("forest-light"),
         QStringLiteral("forest")},
        {QStringLiteral("ky-pair"), QStringLiteral("Ky"), QStringLiteral("polished-ky"), QStringLiteral("patina-ky")},
        {QStringLiteral("nord-pair"), QStringLiteral("Nord"), QStringLiteral("paper"), QStringLiteral("nord")},
    };
    return list;
}

const QList<Half>& halves()
{
    static const QList<Half> list = buildHalves();
    return list;
}

const Half* findHalf(const QString& idNameOrAlias)
{
    for (const Half& h : halves()) {
        if (h.id == idNameOrAlias || h.name == idNameOrAlias || h.aliases.contains(idNameOrAlias))
            return &h;
    }
    return nullptr;
}

const Pair* findPair(const QString& pairId)
{
    for (const Pair& p : pairs()) {
        if (p.id == pairId)
            return &p;
    }
    return nullptr;
}

QString defaultPairId()
{
    return QStringLiteral("busnes-pair");
}

Selection selectionFor(const QString& saved)
{
    if (findPair(saved))
        return {saved, Mode::System};
    if (const Half* h = findHalf(saved))
        return {h->pairId, h->dark ? Mode::Dark : Mode::Light};
    return {defaultPairId(), Mode::System};
}

QString savedId(const Selection& selection)
{
    const Pair* p = findPair(selection.pairId);
    if (!p)
        p = findPair(defaultPairId());
    switch (selection.mode) {
    case Mode::Light:
        return p->lightId;
    case Mode::Dark:
        return p->darkId;
    case Mode::System:
        break;
    }
    return p->id;
}

const Half& resolve(const Selection& selection, bool systemDark)
{
    const Pair* p = findPair(selection.pairId);
    if (!p)
        p = findPair(defaultPairId());
    const bool dark = selection.mode == Mode::System ? systemDark : selection.mode == Mode::Dark;
    return *findHalf(dark ? p->darkId : p->lightId);
}

ThemePalette palette(const Selection& selection, bool systemDark)
{
    return resolve(selection, systemDark).palette;
}

ThemePalette palette(const QString& saved, bool systemDark)
{
    return palette(selectionFor(saved), systemDark);
}

bool systemPrefersDark(SystemScheme hint, quint32 windowRgb)
{
    if (hint == SystemScheme::Unknown)
        return !ThemePalette::isPerceptuallyLight(windowRgb);
    return hint == SystemScheme::Dark;
}

} // namespace AppTheme
