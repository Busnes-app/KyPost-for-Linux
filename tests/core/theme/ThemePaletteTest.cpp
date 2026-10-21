#include "theme/AppTheme.h"
#include "theme/Shape.h"
#include "theme/ThemePalette.h"

#include <QTest>

class ThemePaletteTest : public QObject
{
    Q_OBJECT

private slots:
    void readableOnAccentContrast();
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
    // Cyber Punk's accent and Dark Matter's (luminance .633) both take black.
    QCOMPARE(ThemePalette::readableOn(0x00F5D4), 0x000000u);
    QCOMPARE(AppTheme::palette(QStringLiteral("Dark Matter"), false).readableOnAccent(), 0x000000u);

    QCOMPARE(AppTheme::palette(QStringLiteral("Busnes Light"), false).readableOnAccent(), 0xFFFFFFu);
    QCOMPARE(AppTheme::palette(QStringLiteral("Busnes Dark"), false).readableOnAccent(), 0x000000u);

    for (const QString& name : AppTheme::themeNames()) {
        const quint32 on = AppTheme::palette(name, false).readableOnAccent();
        QVERIFY2(on == 0x000000u || on == 0xFFFFFFu, qPrintable(name));
    }
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
