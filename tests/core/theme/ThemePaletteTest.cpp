#include "theme/AppTheme.h"
#include "theme/Shape.h"
#include "theme/ThemePalette.h"

#include <QTest>
#include <cmath>

class ThemePaletteTest : public QObject
{
    Q_OBJECT

private slots:
    void readableOnAccentContrast();
    void mixRoundsLikeCss();
    void onAccentReadsOnAccent();
    void shapeRadiiMatchLiterals();
};

void ThemePaletteTest::readableOnAccentContrast()
{
    QVERIFY(ThemePalette::isPerceptuallyLight(0xFFFFFF));
    QVERIFY(!ThemePalette::isPerceptuallyLight(0x000000));

    // 0x8C8C8C: luminance 140/255 = .549 (white). 0x8D8D8D: 141/255 = .553 (black).
    QCOMPARE(ThemePalette::readableOn(0x8C8C8C), 0xFFFFFFu);
    QCOMPARE(ThemePalette::readableOn(0x8D8D8D), 0x000000u);
    QCOMPARE(ThemePalette::readableOn(0xFFFFFF), 0x000000u);
    QCOMPARE(ThemePalette::readableOn(0x000000), 0xFFFFFFu);
    QCOMPARE(ThemePalette::readableOn(0x00F5D4), 0x000000u);
}

void ThemePaletteTest::mixRoundsLikeCss()
{
    // color-mix(in srgb, a p%, b), rounded half up per channel.
    QCOMPARE(ThemePalette::mix(0xFFFFFF, 50, 0x000000), 0x808080u); // 127.5 -> 128
    QCOMPARE(ThemePalette::mix(0xA8174E, 12, 0xFFFFFF), 0xF5E3EAu);
    QCOMPARE(ThemePalette::mix(0x123456, 100, 0xABCDEF), 0x123456u);
    QCOMPARE(ThemePalette::mix(0x123456, 0, 0xABCDEF), 0xABCDEFu);
}

void ThemePaletteTest::onAccentReadsOnAccent()
{
    // ky-ui's button text, not a computed black/white: Busnes Dark is #182326.
    for (const AppTheme::Half& h : AppTheme::halves()) {
        const double contrast = [](quint32 a, quint32 b) {
            const auto lum = [](quint32 c) {
                double sum = 0;
                const double w[3] = {0.2126, 0.7152, 0.0722};
                for (int i = 0; i < 3; ++i) {
                    const double v = ((c >> (16 - 8 * i)) & 0xFF) / 255.0;
                    sum += w[i] * (v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4));
                }
                return sum;
            };
            const double la = lum(a), lb = lum(b);
            return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
        }(h.palette.onAccent, h.palette.accent);
        QVERIFY2(contrast >= 4.5, qPrintable(QStringLiteral("%1 %2").arg(h.id).arg(contrast)));
    }
    QCOMPARE(AppTheme::findHalf(QStringLiteral("busnes-dark"))->palette.onAccent, 0x182326u);
}

void ThemePaletteTest::shapeRadiiMatchLiterals()
{
    QCOMPARE(Shape::field, 14);
    QCOMPARE(Shape::button, 10);
    QCOMPARE(Shape::panel, 14);
    QCOMPARE(Shape::sheet, 14);
    QCOMPARE(Shape::emptyState, 10);
    QCOMPARE(Shape::fieldStroke, 2);
}

QTEST_APPLESS_MAIN(ThemePaletteTest)
#include "ThemePaletteTest.moc"
