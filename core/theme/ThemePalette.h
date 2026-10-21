#pragma once
#include <QtGlobal>  // quint32 only -- NOT QColor/QtGui, see global constraint #1

struct ThemePalette
{
    quint32 bg;
    quint32 panel;
    quint32 ink;
    quint32 inkStrong;
    quint32 accent;
    quint32 accentSoft;
    quint32 line;
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

    // Text/icon colour on an `accent` fill: readableOn(accent).
    quint32 readableOnAccent() const;
};
