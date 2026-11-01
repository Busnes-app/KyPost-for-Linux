#include "theme/ThemePalette.h"

#include <cmath>

bool ThemePalette::isPerceptuallyLight(quint32 rgbHex)
{
    const double r = ((rgbHex >> 16) & 0xFF) / 255.0;
    const double g = ((rgbHex >> 8) & 0xFF) / 255.0;
    const double b = (rgbHex & 0xFF) / 255.0;
    const double luminance = 0.299 * r + 0.587 * g + 0.114 * b;
    return luminance > 0.55;
}

bool ThemePalette::isLight() const
{
    return isPerceptuallyLight(bg);
}

ThemePalette::ColorScheme ThemePalette::preferredColorScheme() const
{
    return isLight() ? ColorScheme::Light : ColorScheme::Dark;
}

quint32 ThemePalette::readableOn(quint32 rgbHex)
{
    return isPerceptuallyLight(rgbHex) ? 0x000000 : 0xFFFFFF;
}

quint32 ThemePalette::mix(quint32 a, int percent, quint32 b)
{
    // Rounds half up like ky-ui's check-status-colours.mjs (JS Math.round).
    quint32 out = 0;
    for (int shift = 16; shift >= 0; shift -= 8) {
        const double ca = (a >> shift) & 0xFF;
        const double cb = (b >> shift) & 0xFF;
        const double v = ca * percent / 100.0 + cb * (1.0 - percent / 100.0);
        out |= static_cast<quint32>(std::floor(v + 0.5)) << shift;
    }
    return out;
}

quint32 ThemePalette::statusSoft(quint32 status) const
{
    return mix(status, 12, panel);
}

quint32 ThemePalette::statusBorder(quint32 status) const
{
    return mix(status, 50, panel);
}
