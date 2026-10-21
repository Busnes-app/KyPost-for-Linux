#pragma once

// Android's text sizes in sp (STYLE_GUIDE.md §2). Multiply by scaleFactor()
// for logical pixels.
namespace TypeScale
{
    inline constexpr int wordmark = 32; // bold
    inline constexpr int subject = 18;
    inline constexpr int sender = 14;
    inline constexpr int preview = 13;
    inline constexpr int date = 12;
    inline constexpr int eyebrow = 11;  // all caps
    inline constexpr double eyebrowLetterSpacingEm = 0.08;

    // The system font relative to 10 pt, the Plasma default, so a stock
    // desktop renders every token at its sp value. Non-positive or missing
    // sizes give 1.0: a bogus font can never yield zero-size text.
    constexpr double scaleFactor(double systemPointSize, int systemPixelSize)
    {
        if (systemPointSize > 0)
            return systemPointSize / 10.0;
        if (systemPixelSize > 0)
            return systemPixelSize / (10.0 * 96 / 72);
        return 1.0;
    }
}
