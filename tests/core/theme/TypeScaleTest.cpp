#include "theme/TypeScale.h"

#include <QTest>

class TypeScaleTest : public QObject
{
    Q_OBJECT

private slots:
    void tokensMatchAndroidSp();
    void scaleFactor();
};

void TypeScaleTest::tokensMatchAndroidSp()
{
    QCOMPARE(TypeScale::wordmark, 32);
    QCOMPARE(TypeScale::subject, 18);
    QCOMPARE(TypeScale::sender, 14);
    QCOMPARE(TypeScale::preview, 13);
    QCOMPARE(TypeScale::date, 12);
    QCOMPARE(TypeScale::eyebrow, 11);
    QCOMPARE(TypeScale::eyebrowLetterSpacingEm, 0.08);
}

void TypeScaleTest::scaleFactor()
{
    QCOMPARE(TypeScale::scaleFactor(10, -1), 1.0);
    QCOMPARE(TypeScale::scaleFactor(12, -1), 1.2);
    QCOMPARE(TypeScale::scaleFactor(-1, 16), 1.2);
    QCOMPARE(TypeScale::scaleFactor(-1, -1), 1.0);
    QCOMPARE(TypeScale::scaleFactor(0, 0), 1.0);
    QCOMPARE(TypeScale::scaleFactor(-3.5, -8), 1.0);
}

QTEST_APPLESS_MAIN(TypeScaleTest)
#include "TypeScaleTest.moc"
