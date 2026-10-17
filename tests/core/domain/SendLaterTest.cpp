#include "domain/SendLater.h"

#include <QTest>
#include <QTimeZone>

namespace {

const QTimeZone kBerlin("Europe/Berlin");

QDateTime berlin(int month, int day, int hour, int minute = 0)
{
    return QDateTime(QDate(2026, month, day), QTime(hour, minute), kBerlin);
}

QVector<SendLaterPreset> kinds(const QVector<SendLaterChoice>& choices)
{
    QVector<SendLaterPreset> out;
    for (const SendLaterChoice& c : choices)
        out.append(c.preset);
    return out;
}

} // namespace

class SendLaterTest : public QObject
{
    Q_OBJECT

private slots:
    void theRelayWindowRefusesThePastAndTheFarFuture();
    void thePickerKeepsAFiveMinuteMargin();
    void aSaturdayMorningOffersAll();
    void lateInTheDayDropsTodaysPresets();
    void presetsStayInTheUsersZoneAcrossDst();
    void mondayMeansNextMonday();
};

void SendLaterTest::theRelayWindowRefusesThePastAndTheFarFuture()
{
    const QDateTime now = berlin(10, 17, 10);
    QVERIFY(!sendAtAccepted(now.addSecs(-60), now));
    QVERIFY(!sendAtAccepted(now, now));
    QVERIFY(!sendAtAccepted(now.addSecs(59), now));
    QVERIFY(sendAtAccepted(now.addSecs(60), now));
    QVERIFY(sendAtAccepted(now.addDays(366), now));
    QVERIFY(!sendAtAccepted(now.addDays(366).addSecs(1), now));
    QVERIFY(!sendAtAccepted(QDateTime(), now));
}

void SendLaterTest::thePickerKeepsAFiveMinuteMargin()
{
    const QDateTime now = berlin(10, 17, 10);
    QVERIFY(!sendLaterOffered(now.addSecs(4 * 60), now));
    QVERIFY(sendLaterOffered(now.addSecs(5 * 60), now));
    QVERIFY(sendLaterOffered(now.addDays(365), now));
    QVERIFY(!sendLaterOffered(now.addDays(365).addSecs(1), now));
}

void SendLaterTest::aSaturdayMorningOffersAll()
{
    // 2026-10-17 is a Saturday.
    const QDateTime now = berlin(10, 17, 9, 20);
    const QVector<SendLaterChoice> choices = sendLaterChoices(now);
    QCOMPARE(kinds(choices), (QVector<SendLaterPreset>{ SendLaterPreset::LaterToday, SendLaterPreset::ThisEvening,
                                                        SendLaterPreset::Tomorrow, SendLaterPreset::NextWeek }));
    QCOMPARE(choices.at(0).at, berlin(10, 17, 12));
    QCOMPARE(choices.at(1).at, berlin(10, 17, 18));
    QCOMPARE(choices.at(2).at, berlin(10, 18, 8));
    QCOMPARE(choices.at(3).at, berlin(10, 19, 8));
}

void SendLaterTest::lateInTheDayDropsTodaysPresets()
{
    // 17:30: three hours on is still today but evening is under an hour away.
    QCOMPARE(kinds(sendLaterChoices(berlin(10, 17, 17, 30))),
             (QVector<SendLaterPreset>{ SendLaterPreset::LaterToday, SendLaterPreset::Tomorrow,
                                        SendLaterPreset::NextWeek }));
    // 22:00: three hours on is tomorrow.
    QCOMPARE(kinds(sendLaterChoices(berlin(10, 17, 22))),
             (QVector<SendLaterPreset>{ SendLaterPreset::Tomorrow, SendLaterPreset::NextWeek }));
}

void SendLaterTest::presetsStayInTheUsersZoneAcrossDst()
{
    // Berlin leaves summer time on 2026-10-25: Monday 26th 08:00 is +01:00.
    const QDateTime now = berlin(10, 24, 10);
    const QVector<SendLaterChoice> choices = sendLaterChoices(now);
    QCOMPARE(choices.last().preset, SendLaterPreset::NextWeek);
    QCOMPARE(choices.last().at.toUTC(), QDateTime(QDate(2026, 10, 26), QTime(7, 0), QTimeZone::UTC));
    QCOMPARE(choices.last().at.timeZone(), kBerlin);
}

void SendLaterTest::mondayMeansNextMonday()
{
    const QVector<SendLaterChoice> choices = sendLaterChoices(berlin(10, 19, 6));
    QCOMPARE(choices.last().preset, SendLaterPreset::NextWeek);
    QCOMPARE(choices.last().at, berlin(10, 26, 8));
}

QTEST_APPLESS_MAIN(SendLaterTest)
#include "SendLaterTest.moc"
