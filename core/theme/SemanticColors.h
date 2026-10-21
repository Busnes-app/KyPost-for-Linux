#pragma once
#include "ThemePalette.h"
#include <QtGlobal>

struct SemanticColor
{
    quint32 rgb;   // 0xRRGGBB
    double alpha;  // 0.0-1.0, 1.0 for fully opaque

    bool operator==(const SemanticColor&) const = default;
};

// Theme-invariant constants, Android AppTheme.kt COLOR_*.
namespace SemanticColors
{
    inline constexpr SemanticColor danger              { 0xFF5F5F, 1.0 };
    inline constexpr SemanticColor dangerBorder        { 0xFFB4AB, 0.4 };
    inline constexpr SemanticColor dangerFill          { 0xFF5F5F, 0.12 };
    inline constexpr SemanticColor dangerActionFill    { 0xFFB4AB, 0.12 };
    inline constexpr SemanticColor dangerActionBorder  { 0xFFB4AB, 0.40 };
    inline constexpr SemanticColor dangerActionText    { 0xFFD8D3, 1.0 };
    inline constexpr SemanticColor warning             { 0xFFD64D, 1.0 };
    inline constexpr SemanticColor warningActionFill   { 0xFFD64D, 0.12 };
    inline constexpr SemanticColor warningActionBorder { 0xFFD64D, 0.40 };
    inline constexpr SemanticColor warningActionText   { 0xFFF0B8, 1.0 };
    inline constexpr SemanticColor successBorder       { 0x7BBF7B, 1.0 };
    inline constexpr SemanticColor successText         { 0xA5DCA5, 1.0 };
    inline constexpr SemanticColor successFill         { 0x7BBF7B, 0.12 };

    struct Affordance
    {
        SemanticColor fill;
        SemanticColor border;
        SemanticColor text;
    };

    enum class Hue { Danger, Warning };

    // Android accentAffordanceColors(): the pale dark-theme trio on a dark
    // palette; on a light one the tint measures ~1.1:1, so a heavier fill and
    // stroke of the hue and text driven 55 % toward black.
    Affordance affordance(Hue hue, const ThemePalette& palette);

    // Android blend(): base moved `fraction` toward tint, channels truncated.
    quint32 blend(quint32 base, quint32 tint, double fraction);
}
