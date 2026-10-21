#include "theme/AppTheme.h"
#include "theme/SemanticColors.h"

#include <QTest>
#include <cmath>

using SemanticColors::Affordance;
using SemanticColors::Hue;

Q_DECLARE_METATYPE(SemanticColor)

namespace {

double channelLuminance(double c8)
{
    const double c = c8 / 255.0;
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

// WCAG 2 relative luminance of r,g,b in 0..255.
double luminance(double r, double g, double b)
{
    return 0.2126 * channelLuminance(r) + 0.7152 * channelLuminance(g) + 0.0722 * channelLuminance(b);
}

double channel(quint32 rgb, int shift)
{
    return (rgb >> shift) & 0xFF;
}

// Contrast of opaque `text` over `fill` alpha-composited onto `under`.
double contrastOver(quint32 text, SemanticColor fill, quint32 under)
{
    double bg[3];
    for (int i = 0; i < 3; ++i) {
        const int shift = 16 - 8 * i;
        bg[i] = channel(fill.rgb, shift) * fill.alpha + channel(under, shift) * (1.0 - fill.alpha);
    }
    const double lt = luminance(channel(text, 16), channel(text, 8), channel(text, 0));
    const double lb = luminance(bg[0], bg[1], bg[2]);
    return (std::max(lt, lb) + 0.05) / (std::min(lt, lb) + 0.05);
}

void compareAffordance(const Affordance& got, const Affordance& want)
{
    QCOMPARE(got.fill.rgb, want.fill.rgb);
    QCOMPARE(got.fill.alpha, want.fill.alpha);
    QCOMPARE(got.border.rgb, want.border.rgb);
    QCOMPARE(got.border.alpha, want.border.alpha);
    QCOMPARE(got.text.rgb, want.text.rgb);
    QCOMPARE(got.text.alpha, want.text.alpha);
}

} // namespace

class SemanticColorsTest : public QObject
{
    Q_OBJECT

private slots:
    void constantsMatchAndroid_data();
    void constantsMatchAndroid();
    void blendTruncatesLikeAndroid();
    void affordanceOnDarkPalette();
    void affordanceOnLightPalette();
    void lightAffordanceTextMeetsWcagAA();
    void affordanceTextOnPlainSurfacesMeetsWcagAA();
};

void SemanticColorsTest::constantsMatchAndroid_data()
{
    QTest::addColumn<SemanticColor>("got");
    QTest::addColumn<quint32>("rgb");
    QTest::addColumn<double>("alpha");
    using namespace SemanticColors;
    QTest::newRow("danger") << danger << 0xFF5F5Fu << 1.0;
    QTest::newRow("dangerBorder") << dangerBorder << 0xFFB4ABu << 0.4;
    QTest::newRow("dangerFill") << dangerFill << 0xFF5F5Fu << 0.12;
    QTest::newRow("dangerActionFill") << dangerActionFill << 0xFFB4ABu << 0.12;
    QTest::newRow("dangerActionBorder") << dangerActionBorder << 0xFFB4ABu << 0.40;
    QTest::newRow("dangerActionText") << dangerActionText << 0xFFD8D3u << 1.0;
    QTest::newRow("warning") << warning << 0xFFD64Du << 1.0;
    QTest::newRow("warningActionFill") << warningActionFill << 0xFFD64Du << 0.12;
    QTest::newRow("warningActionBorder") << warningActionBorder << 0xFFD64Du << 0.40;
    QTest::newRow("warningActionText") << warningActionText << 0xFFF0B8u << 1.0;
    QTest::newRow("successBorder") << successBorder << 0x7BBF7Bu << 1.0;
    QTest::newRow("successText") << successText << 0xA5DCA5u << 1.0;
    QTest::newRow("successFill") << successFill << 0x7BBF7Bu << 0.12;
}

void SemanticColorsTest::constantsMatchAndroid()
{
    QFETCH(SemanticColor, got);
    QFETCH(quint32, rgb);
    QFETCH(double, alpha);
    QCOMPARE(got.rgb, rgb);
    QCOMPARE(got.alpha, alpha);
}

void SemanticColorsTest::blendTruncatesLikeAndroid()
{
    QCOMPARE(SemanticColors::blend(0xFF5F5F, 0x000000, 0.55), 0x722A2Au);
    QCOMPARE(SemanticColors::blend(0xFFD64D, 0x000000, 0.55), 0x726022u);
    // The Busnes accentSoft derivation.
    QCOMPARE(SemanticColors::blend(0xFFFFFF, 0xBF3F18, 0.3), 0xEBC5B9u);
    QCOMPARE(SemanticColors::blend(0x1F2B2E, 0xF5865F, 0.3), 0x5F463Cu);
    QCOMPARE(AppTheme::palette(QStringLiteral("Busnes Light"), false).accentSoft, 0xEBC5B9u);
    QCOMPARE(AppTheme::palette(QStringLiteral("Busnes Dark"), false).accentSoft, 0x5F463Cu);
}

void SemanticColorsTest::affordanceOnDarkPalette()
{
    const ThemePalette dark = AppTheme::palette(QStringLiteral("Busnes Dark"), false);
    QVERIFY(!dark.isLight());
    compareAffordance(SemanticColors::affordance(Hue::Danger, dark),
                      {{0xFFB4AB, 0.12}, {0xFFB4AB, 0.40}, {0xFFD8D3, 1.0}});
    compareAffordance(SemanticColors::affordance(Hue::Warning, dark),
                      {{0xFFD64D, 0.12}, {0xFFD64D, 0.40}, {0xFFF0B8, 1.0}});
}

void SemanticColorsTest::affordanceOnLightPalette()
{
    const ThemePalette light = AppTheme::palette(QStringLiteral("Busnes Light"), false);
    QVERIFY(light.isLight());
    compareAffordance(SemanticColors::affordance(Hue::Danger, light),
                      {{0xFF5F5F, 0.18}, {0xFF5F5F, 0.65}, {0x722A2A, 1.0}});
    compareAffordance(SemanticColors::affordance(Hue::Warning, light),
                      {{0xFFD64D, 0.18}, {0xFFD64D, 0.65}, {0x726022, 1.0}});
}

void SemanticColorsTest::lightAffordanceTextMeetsWcagAA()
{
    int lightPalettes = 0;
    for (const QString& name : AppTheme::themeNames().mid(1)) {
        const ThemePalette p = AppTheme::palette(name, false);
        if (!p.isLight())
            continue;
        ++lightPalettes;
        for (Hue hue : {Hue::Danger, Hue::Warning}) {
            const Affordance a = SemanticColors::affordance(hue, p);
            const double ratio = contrastOver(a.text.rgb, a.fill, p.panel);
            QVERIFY2(ratio >= 4.5,
                     qPrintable(QStringLiteral("%1 %2: %3:1").arg(name).arg(int(hue)).arg(ratio)));
        }
    }
    QCOMPARE(lightPalettes, 7);
}

// Error and warning lines are plain Text on panel or bg, every palette.
void SemanticColorsTest::affordanceTextOnPlainSurfacesMeetsWcagAA()
{
    const SemanticColor none{0x000000, 0.0};
    for (const QString& name : AppTheme::themeNames().mid(1)) {
        const ThemePalette p = AppTheme::palette(name, false);
        for (Hue hue : {Hue::Danger, Hue::Warning}) {
            const quint32 text = SemanticColors::affordance(hue, p).text.rgb;
            for (quint32 surface : {p.panel, p.bg}) {
                const double ratio = contrastOver(text, none, surface);
                QVERIFY2(ratio >= 4.5, qPrintable(QStringLiteral("%1 %2 on %3: %4:1")
                                                      .arg(name).arg(int(hue)).arg(surface, 6, 16, QLatin1Char('0')).arg(ratio)));
            }
        }
    }
}

QTEST_APPLESS_MAIN(SemanticColorsTest)
#include "SemanticColorsTest.moc"
