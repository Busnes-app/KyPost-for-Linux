#include "theme/SemanticColors.h"

namespace SemanticColors {

quint32 blend(quint32 base, quint32 tint, double fraction)
{
    // Float, like Android's Kotlin Float maths, so truncation lands the same.
    const float f = static_cast<float>(fraction);
    quint32 out = 0;
    for (int shift = 16; shift >= 0; shift -= 8) {
        const int b = (base >> shift) & 0xFF;
        const int t = (tint >> shift) & 0xFF;
        out |= static_cast<quint32>(static_cast<int>(b + (t - b) * f)) << shift;
    }
    return out;
}

Affordance affordance(Hue hue, const ThemePalette& palette)
{
    const bool isDanger = hue == Hue::Danger;
    if (!palette.isLight())
        return isDanger ? Affordance{dangerActionFill, dangerActionBorder, dangerActionText}
                      : Affordance{warningActionFill, warningActionBorder, warningActionText};

    const quint32 rgb = isDanger ? danger.rgb : warning.rgb;
    return {{rgb, 0.18}, {rgb, 0.65}, {blend(rgb, 0x000000, 0.55), 1.0}};
}

} // namespace SemanticColors
