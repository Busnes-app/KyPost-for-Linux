#pragma once
#include <QtGlobal>  // quint32 only -- NOT QColor/QtGui, see global constraint #1

// One half of a theme pair, ky-ui themes.js/tokens.css values as 0xRRGGBB.
struct ThemePalette
{
    quint32 bg;
    quint32 panel;
    quint32 ink;
    quint32 inkStrong;
    quint32 accent;
    // ky-ui --accent-soft, flattened over panel where ky-ui gives an rgba.
    quint32 accentSoft;
    // ky-ui --line, flattened over panel where ky-ui gives an rgba.
    quint32 line;
    // ky-ui --button-text: text and icons on an accent fill.
    quint32 onAccent;
    // Status colours (ky-ui 0.8.0), separate from the accent. Each reads at
    // 4.5:1 on bg, panel and its own soft tint (statusSoft()).
    quint32 danger;
    quint32 warning;
    quint32 success;
    // Avatar gradient stops and 1px ring (Android ThemePalette avatar*).
    quint32 avatarStart;
    quint32 avatarEnd;
    quint32 avatarBorder;

    bool operator==(const ThemePalette&) const = default;

    // True when a hex color (0xRRGGBB) reads as visually "light" by the
    // same formula as the Swift reference: luminance =
    // 0.299*R + 0.587*G + 0.114*B (each channel normalized 0..1),
    // light when luminance > 0.55.
    static bool isPerceptuallyLight(quint32 rgbHex);

    // bg's perceptual lightness -- light themes must present a light
    // system/QML color-scheme hint (Task 6 consumer).
    bool isLight() const; // isPerceptuallyLight(bg)

    enum class ColorScheme { Light, Dark };
    ColorScheme preferredColorScheme() const; // isLight() ? Light : Dark

    // Black on a perceptually light colour, else white: Android's readableOn().
    static quint32 readableOn(quint32 rgbHex);

    // CSS color-mix(in srgb, a percent%, b): per-channel, rounded.
    static quint32 mix(quint32 a, int percent, quint32 b);

    // ky-ui --ky-<status>-soft: the status at 12 % over panel. A pill fill.
    quint32 statusSoft(quint32 status) const;
    // A status outline: the status at 50 % over panel.
    quint32 statusBorder(quint32 status) const;
};
