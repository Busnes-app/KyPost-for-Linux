#include "theme/AppTheme.h"

#include <QHash>

namespace {

const QHash<QString, ThemePalette>& allPalettes()
{
    static const QHash<QString, ThemePalette> palettes = {
        // Busnes pair: Android themePalettes. accentSoft has no Android source:
        // panel blended 30 % toward accent with Android's truncating blend().
        {QStringLiteral("Busnes Light"),
         ThemePalette{0xF8F6F0, 0xFFFFFF, 0x566461, 0x182326, 0xBF3F18, 0xEBC5B9, 0xCCCFCF,
                      0xBF3F18, 0xE65124, 0xBF3F18}},
        {QStringLiteral("Busnes Dark"),
         ThemePalette{0x182326, 0x1F2B2E, 0xB3BCB8, 0xF2EFE8, 0xF5865F, 0x5F463C, 0x525A5B,
                      0xF5865F, 0xF26A3D, 0xF5865F}},
        {QStringLiteral("Dark Matter"),
         ThemePalette{0x1A1A1E, 0x252530, 0xD4C5E2, 0xE8DDF5, 0xC29A72, 0x5A3F31, 0x404050,
                      0xC29A72, 0x9A7450, 0x8F6B4A}},
        {QStringLiteral("Light Matter"),
         ThemePalette{0xF5EFE5, 0xFFF8EE, 0x4C3D32, 0x2D1F15, 0xC29A72, 0xE6D2BE, 0xC5B29D,
                      0xC29A72, 0x9A7450, 0x8F6B4A}},
        {QStringLiteral("Tropics"),
         ThemePalette{0xF4F1EB, 0xFFFAF0, 0x43362D, 0x241A14, 0x9BC400, 0xD4E3A0, 0xC4B7A3,
                      0x9BC400, 0x7EA100, 0x78A100}},
        {QStringLiteral("Tropic Night"),
         ThemePalette{0x15131A, 0x221F2B, 0xCDBDE0, 0xE8DDF5, 0x9BC400, 0x6B4A42, 0x3C3650,
                      0x9BC400, 0x7EA100, 0x78A100}},
        {QStringLiteral("Ocean"),
         ThemePalette{0x0F1B24, 0x152A36, 0xB8D8E8, 0xE0F2FB, 0x5EA9BE, 0x214657, 0x2F5567,
                      0x74BACD, 0x4F91A6, 0x4F91A6}},
        {QStringLiteral("Coffee"),
         ThemePalette{0x1D1714, 0x2A211D, 0xD6C0B3, 0xF0DED2, 0xB47F5C, 0x5F3F2F, 0x4A3830,
                      0xB47F5C, 0x8F5F42, 0x8F5F42}},
        {QStringLiteral("White Cliffs"),
         ThemePalette{0xF7F9FB, 0xFFFFFF, 0x2E4C63, 0x163246, 0x5EA8D8, 0xDFF1FB, 0x8FC3DF,
                      0x4F9BC8, 0x58B65A, 0x2F7FB0}},
        {QStringLiteral("Cyber Punk"),
         ThemePalette{0x120918, 0x1E1028, 0xF5D0FF, 0xFFE9FF, 0x00F5D4, 0x3B1760, 0x5C2D84,
                      0x00F5D4, 0x00C9AD, 0x00C9AD}},
        {QStringLiteral("Neon Purple"),
         ThemePalette{0x130B1D, 0x231233, 0xE4CCFF, 0xF2E6FF, 0xC86CFF, 0x47206C, 0x63358A,
                      0xC86CFF, 0x9D45D3, 0x9D45D3}},
        {QStringLiteral("Space"),
         ThemePalette{0x0B0F1A, 0x151C2D, 0xC8D5F0, 0xE7EFFF, 0x86A8FF, 0x263E74, 0x34496F,
                      0x86A8FF, 0x6788DD, 0x6788DD}},
        {QStringLiteral("Sky"),
         ThemePalette{0xDFF1FF, 0xF4FBFF, 0x2F4F64, 0x183142, 0x6DB3D6, 0xB6DCED, 0x93BDD2,
                      0x6DB3D6, 0x4F93B8, 0x4F93B8}},
        {QStringLiteral("Forest"),
         ThemePalette{0x142018, 0x1F2F24, 0xC7DBC7, 0xE3F0DF, 0x8FAA74, 0x3A5837, 0x4F694F,
                      0x8FAA74, 0x6F8D5A, 0x6F8D5A}},
        {QStringLiteral("Sun"),
         ThemePalette{0xFFF3DC, 0xFFF9EC, 0x5A4024, 0x392611, 0xE0AB4F, 0xF1D9A2, 0xD4B27A,
                      0xE0AB4F, 0xBB8631, 0xBB8631}},
        {QStringLiteral("Patina Ky"),
         ThemePalette{0x0D0F14, 0x161A22, 0x64748B, 0xE2E8F0, 0x4DEEEA, 0x0E4A48, 0x1E293B,
                      0x4DEEEA, 0x10B981, 0x0E9668}},
        {QStringLiteral("Polished Ky"),
         ThemePalette{0xEEF2F6, 0xFFFFFF, 0x475569, 0x0F172A, 0x0891B2, 0xCFFAFE, 0xCBD5E1,
                      0x0891B2, 0x10B981, 0x059669}},
    };
    return palettes;
}

} // namespace

namespace AppTheme {

QStringList themeNames()
{
    return {
        QStringLiteral("Follow system"),
        QStringLiteral("Busnes Light"),
        QStringLiteral("Busnes Dark"),
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
}

QString defaultThemeName()
{
    return QStringLiteral("Follow system");
}

ThemePalette palette(const QString& name, bool systemDark)
{
    const QHash<QString, ThemePalette>& palettes = allPalettes();
    const auto it = palettes.constFind(name);
    if (it != palettes.constEnd())
        return it.value();
    // Follow system, and any unknown name (it falls back to Follow system).
    return palettes.value(systemDark ? QStringLiteral("Busnes Dark") : QStringLiteral("Busnes Light"));
}

bool systemPrefersDark(SystemScheme hint, quint32 windowRgb)
{
    if (hint == SystemScheme::Unknown)
        return !ThemePalette::isPerceptuallyLight(windowRgb);
    return hint == SystemScheme::Dark;
}

} // namespace AppTheme
