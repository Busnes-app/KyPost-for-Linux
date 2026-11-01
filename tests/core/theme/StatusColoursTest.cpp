// The status colour gate, ported from ky-ui 0.11.0 scripts/check-status-colours.mjs:
// for every half, danger, warning and success read at 4.5:1 or better on bg, on
// panel and on their own soft tint; accent and danger are at least ΔE00 20 apart;
// any two of accent, danger, warning and success at least ΔE00 10. ky-ui runs
// 180 checks across 12 halves; so does this.
#include "theme/AppTheme.h"
#include "theme/SemanticColors.h"

#include <QTest>
#include <cmath>

namespace {

constexpr double kMinContrast = 4.5;
constexpr double kMinAccentDanger = 20;
constexpr double kMinOther = 10;

double linear(quint32 rgb, int shift)
{
    const double v = ((rgb >> shift) & 0xFF) / 255.0;
    return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
}

double luminance(quint32 rgb)
{
    return 0.2126 * linear(rgb, 16) + 0.7152 * linear(rgb, 8) + 0.0722 * linear(rgb, 0);
}

double contrast(quint32 a, quint32 b)
{
    const double la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

struct Lab
{
    double L, a, b;
};

Lab lab(quint32 rgb)
{
    const double r = linear(rgb, 16), g = linear(rgb, 8), b = linear(rgb, 0);
    double xyz[3] = {
        (0.4124564 * r + 0.3575761 * g + 0.1804375 * b) / 0.95047,
        0.2126729 * r + 0.7151522 * g + 0.072175 * b,
        (0.0193339 * r + 0.119192 * g + 0.9503041 * b) / 1.08883,
    };
    for (double& t : xyz)
        t = t > 216.0 / 24389 ? std::cbrt(t) : (24389.0 / 27 * t + 16) / 116;
    return {116 * xyz[1] - 16, 500 * (xyz[0] - xyz[1]), 200 * (xyz[1] - xyz[2])};
}

// CIEDE2000, Sharma, Wu and Dalal (2005), kL = kC = kH = 1; as ky-ui.
double deltaE(quint32 x, quint32 y)
{
    const Lab p = lab(x), q = lab(y);
    const double rad = M_PI / 180;
    const double C = (std::hypot(p.a, p.b) + std::hypot(q.a, q.b)) / 2;
    const double G = 0.5 * (1 - std::sqrt(std::pow(C, 7) / (std::pow(C, 7) + std::pow(25, 7))));
    const double ap1 = p.a * (1 + G), ap2 = q.a * (1 + G);
    const double Cp1 = std::hypot(ap1, p.b), Cp2 = std::hypot(ap2, q.b);
    const auto hue = [&](double b, double a) { return std::fmod(std::atan2(b, a) / rad + 360, 360); };
    const double hp1 = hue(p.b, ap1), hp2 = hue(q.b, ap2);
    const double dL = q.L - p.L, dC = Cp2 - Cp1;
    double dh = Cp1 * Cp2 == 0 ? 0 : hp2 - hp1;
    if (dh > 180)
        dh -= 360;
    else if (dh < -180)
        dh += 360;
    const double dH = 2 * std::sqrt(Cp1 * Cp2) * std::sin(dh * rad / 2);
    const double Lm = (p.L + q.L) / 2, Cm = (Cp1 + Cp2) / 2;
    double hm = hp1 + hp2;
    if (Cp1 * Cp2 != 0)
        hm = std::abs(hp1 - hp2) > 180 ? (hm + (hm < 360 ? 360 : -360)) / 2 : hm / 2;
    const double T = 1 - 0.17 * std::cos((hm - 30) * rad) + 0.24 * std::cos(2 * hm * rad)
                     + 0.32 * std::cos((3 * hm + 6) * rad) - 0.2 * std::cos((4 * hm - 63) * rad);
    const double SL = 1 + 0.015 * std::pow(Lm - 50, 2) / std::sqrt(20 + std::pow(Lm - 50, 2));
    const double SC = 1 + 0.045 * Cm, SH = 1 + 0.015 * Cm * T;
    const double RT = -2 * std::sqrt(std::pow(Cm, 7) / (std::pow(Cm, 7) + std::pow(25, 7)))
                      * std::sin(60 * std::exp(-std::pow((hm - 275) / 25, 2)) * rad);
    return std::sqrt(std::pow(dL / SL, 2) + std::pow(dC / SC, 2) + std::pow(dH / SH, 2)
                     + RT * (dC / SC) * (dH / SH));
}

// Every check for one palette, as "name value min"; failures are value < min.
struct Check
{
    QString name;
    double value;
    double min;
};

QList<Check> evaluate(const ThemePalette& p)
{
    QList<Check> checks;
    const QList<QPair<QString, quint32>> statuses = {
        {QStringLiteral("danger"), p.danger}, {QStringLiteral("warning"), p.warning}, {QStringLiteral("success"), p.success}};
    for (const auto& [name, colour] : statuses) {
        checks.append({name + QStringLiteral(" on bg"), contrast(colour, p.bg), kMinContrast});
        checks.append({name + QStringLiteral(" on panel"), contrast(colour, p.panel), kMinContrast});
        checks.append({name + QStringLiteral(" on soft"), contrast(colour, p.statusSoft(colour)), kMinContrast});
    }
    checks.append({QStringLiteral("ΔE accent–danger"), deltaE(p.accent, p.danger), kMinAccentDanger});
    const QList<QPair<QString, quint32>> hues = {{QStringLiteral("accent"), p.accent},
                                                 {QStringLiteral("danger"), p.danger},
                                                 {QStringLiteral("warning"), p.warning},
                                                 {QStringLiteral("success"), p.success}};
    for (int i = 0; i < hues.size(); ++i) {
        for (int j = i + 1; j < hues.size(); ++j) {
            if (i == 0 && j == 1)
                continue; // accent–danger has the stricter check above
            checks.append({QStringLiteral("ΔE %1–%2").arg(hues[i].first, hues[j].first),
                           deltaE(hues[i].second, hues[j].second), kMinOther});
        }
    }
    return checks;
}

} // namespace

class StatusColoursTest : public QObject
{
    Q_OBJECT

private slots:
    void colourMathMatchesKyUi();
    void softTintsMatchKyUi();
    void everyHalfPassesTheGate();
    void gateRejectsRustDanger();
    void dangerIsNeverTheAccent();
    void blendTruncatesLikeAndroid();
};

// Reference values from ky-ui's contrast()/deltaE() run under node.
void StatusColoursTest::colourMathMatchesKyUi()
{
    const auto near = [](double got, double want) { return qAbs(got - want) < 1e-6; };
    QVERIFY(near(contrast(0x000000, 0xFFFFFF), 21.0));
    QVERIFY(near(contrast(0xA8174E, 0xF8F6F0), 6.706336163719405));
    QVERIFY(near(deltaE(0xBF3F18, 0xA8174E), 23.87849410161051));
    QVERIFY(near(deltaE(0xF5865F, 0xFF8FB1), 22.99745233388378));
    QVERIFY(near(deltaE(0x8FAA74, 0x6EE0D0), 22.34774080473049));
    QCOMPARE(deltaE(0xBF3F18, 0xBF3F18), 0.0);
}

void StatusColoursTest::softTintsMatchKyUi()
{
    // ky-ui color-mix(in srgb, status 12%, panel), from check-status-colours.mjs mix().
    const ThemePalette light = AppTheme::findHalf(QStringLiteral("busnes-light"))->palette;
    QCOMPARE(light.statusSoft(light.danger), 0xF5E3EAu);
    QCOMPARE(light.statusSoft(light.warning), 0xEFECE0u);
    QCOMPARE(light.statusSoft(light.success), 0xE5EEE8u);
    const ThemePalette dark = AppTheme::findHalf(QStringLiteral("busnes-dark"))->palette;
    QCOMPARE(dark.statusSoft(dark.danger), 0x3A373Eu);
    QCOMPARE(dark.statusSoft(dark.warning), 0x3A3F34u);
    QCOMPARE(dark.statusSoft(dark.success), 0x2B433Du);
    const ThemePalette nord = AppTheme::findHalf(QStringLiteral("nord"))->palette;
    QCOMPARE(nord.statusSoft(nord.danger), 0x505260u);
    QCOMPARE(nord.statusSoft(nord.warning), 0x535353u);
    QCOMPARE(nord.statusSoft(nord.success), 0x4A545Du);
    const ThemePalette forest = AppTheme::findHalf(QStringLiteral("forest-light"))->palette;
    QCOMPARE(forest.statusSoft(forest.success), 0xDEECEAu);
}

void StatusColoursTest::everyHalfPassesTheGate()
{
    int count = 0;
    QStringList failures;
    for (const AppTheme::Half& half : AppTheme::halves()) {
        for (const Check& c : evaluate(half.palette)) {
            ++count;
            if (!(c.value >= c.min))
                failures.append(QStringLiteral("%1: %2 %3 < %4").arg(half.id, c.name).arg(c.value, 0, 'f', 2).arg(c.min));
        }
    }
    QVERIFY2(failures.isEmpty(), qPrintable(failures.join(QLatin1Char('\n'))));
    QCOMPARE(count, 180);
}

// The gate is not proven until a bad palette turns it red: the old shared
// danger (#FF5F5F, near the rust accent) on Busnes Light fails it.
void StatusColoursTest::gateRejectsRustDanger()
{
    ThemePalette p = AppTheme::findHalf(QStringLiteral("busnes-light"))->palette;
    p.danger = 0xFF5F5F;
    QStringList failed;
    for (const Check& c : evaluate(p)) {
        if (!(c.value >= c.min))
            failed.append(c.name);
    }
    QVERIFY(failed.contains(QStringLiteral("danger on bg")));
    QVERIFY(failed.contains(QStringLiteral("ΔE accent–danger")));
}

void StatusColoursTest::dangerIsNeverTheAccent()
{
    for (const AppTheme::Half& half : AppTheme::halves())
        QVERIFY2(half.palette.danger != half.palette.accent, qPrintable(half.id));
}

void StatusColoursTest::blendTruncatesLikeAndroid()
{
    QCOMPARE(SemanticColors::blend(0xFF5F5F, 0x000000, 0.55), 0x722A2Au);
    QCOMPARE(SemanticColors::blend(0xFFD64D, 0x000000, 0.55), 0x726022u);
    QCOMPARE(SemanticColors::blend(0xFFFFFF, 0xBF3F18, 0.3), 0xEBC5B9u);
    QCOMPARE(SemanticColors::blend(0x1F2B2E, 0xF5865F, 0.3), 0x5F463Cu);
}

QTEST_APPLESS_MAIN(StatusColoursTest)
#include "StatusColoursTest.moc"
