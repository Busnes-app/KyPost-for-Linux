#pragma once
#include "ThemePalette.h"
#include <QString>
#include <QStringList>

namespace AppTheme
{
    // Exact list + order from Android THEME_OPTIONS -- binding contract, do
    // not reorder or rename. "Follow system" is not a palette: it paints
    // Busnes Light or Busnes Dark from the desktop's colour scheme.
    QStringList themeNames();

    QString defaultThemeName(); // "Follow system"

    // Android themePaletteFor(): the named palette; Follow system or an
    // unknown name gives Busnes Dark when `systemDark`, else Busnes Light.
    ThemePalette palette(const QString& name, bool systemDark);

    enum class SystemScheme { Unknown, Light, Dark };

    // A Light/Dark hint decides; Unknown (Qt 5, Qt < 6.5, or a desktop that
    // does not say) falls back to the platform window colour's lightness.
    bool systemPrefersDark(SystemScheme hint, quint32 windowRgb);
}
