#include "domain/Snooze.h"

#include <QTest>
#include <QTimeZone>

// Presets follow Android's WhenPicker.kt. Times are built in explicit zones,
// so the result does not depend on the machine running the test.
class SnoozeTest : public QObject
{
    Q_OBJECT

private slots:
    void morningOffersAllFourPresets();
    void laterTodayIsHiddenOnceItWouldBeTomorrow();
    void thisEveningIsHiddenWithinTheHour();
    void nextWeekOnAMondayIsTheFollowingMonday();
    void tomorrowKeepsEightOClockAcrossSpringForward();
    void laterTodayKeepsTheOffsetAcrossFallBack();
    void customTimeIsReadInTheZone();
    void customTimeRefusesThePastAndBeyondAYear();
    void customTimeRefusesASkippedWallClockTime();
    void customTimeRefusesGarbage();
    void windowIsAMinuteToAYear();

private:
    static QDateTime at(const char* zone, int y, int mo, int d, int h, int mi)
    {
        const QDateTime dt(QDate(y, mo, d), QTime(h, mi), QTimeZone(zone));
        Q_ASSERT(dt.isValid());
        return dt;
    }
    static QList<SnoozePreset::Kind> kinds(const QVector<SnoozePreset>& presets)
    {
        QList<SnoozePreset::Kind> out;
        for (const SnoozePreset& p : presets)
            out.append(p.kind);
        return out;
    }
};

using Kind = SnoozePreset::Kind;

void SnoozeTest::morningOffersAllFourPresets()
{
    // Wednesday 09:20.
    const QDateTime now = at("Europe/Berlin", 2026, 10, 14, 9, 20);
    const QVector<SnoozePreset> presets = snoozePresets(now);

    QCOMPARE(kinds(presets), (QList<Kind>{ Kind::LaterToday, Kind::ThisEvening, Kind::Tomorrow, Kind::NextWeek }));
    QCOMPARE(presets[0].at, at("Europe/Berlin", 2026, 10, 14, 12, 0)); // now + 3h, down to the hour
    QCOMPARE(presets[1].at, at("Europe/Berlin", 2026, 10, 14, 18, 0));
    QCOMPARE(presets[2].at, at("Europe/Berlin", 2026, 10, 15, 8, 0));
    QCOMPARE(presets[3].at, at("Europe/Berlin", 2026, 10, 19, 8, 0)); // Monday
}

void SnoozeTest::laterTodayIsHiddenOnceItWouldBeTomorrow()
{
    QVERIFY(kinds(snoozePresets(at("Europe/Berlin", 2026, 10, 14, 20, 59))).contains(Kind::LaterToday));
    QVERIFY(!kinds(snoozePresets(at("Europe/Berlin", 2026, 10, 14, 21, 0))).contains(Kind::LaterToday));
}

void SnoozeTest::thisEveningIsHiddenWithinTheHour()
{
    QVERIFY(kinds(snoozePresets(at("Europe/Berlin", 2026, 10, 14, 16, 59))).contains(Kind::ThisEvening));
    QVERIFY(!kinds(snoozePresets(at("Europe/Berlin", 2026, 10, 14, 17, 0))).contains(Kind::ThisEvening));
}

void SnoozeTest::nextWeekOnAMondayIsTheFollowingMonday()
{
    const QVector<SnoozePreset> presets = snoozePresets(at("Europe/Berlin", 2026, 10, 19, 7, 0));
    QCOMPARE(presets.last().kind, Kind::NextWeek);
    QCOMPARE(presets.last().at, at("Europe/Berlin", 2026, 10, 26, 8, 0));

    // On a Sunday it is the next day, which "Tomorrow" already is; the
    // repeat is dropped.
    const QVector<SnoozePreset> sunday = snoozePresets(at("Europe/Berlin", 2026, 10, 18, 22, 0));
    QCOMPARE(kinds(sunday), (QList<Kind>{ Kind::Tomorrow }));
}

// Berlin skips 02:00-03:00 on 2026-03-29: 23 hours later is still 08:00.
void SnoozeTest::tomorrowKeepsEightOClockAcrossSpringForward()
{
    const QDateTime now = at("Europe/Berlin", 2026, 3, 28, 22, 0);
    const QVector<SnoozePreset> presets = snoozePresets(now);

    QCOMPARE(presets.first().kind, Kind::Tomorrow);
    QCOMPARE(presets.first().at.toUTC(), QDateTime(QDate(2026, 3, 29), QTime(6, 0), QTimeZone::UTC));
    QCOMPARE(now.secsTo(presets.first().at), qint64(9 * 3600));
}

// Berlin repeats 02:00-03:00 on 2026-10-25. 00:30 CEST + 3h is 02:30 CET;
// rounding down keeps CET (01:00Z), not the earlier 02:00 CEST (00:00Z).
void SnoozeTest::laterTodayKeepsTheOffsetAcrossFallBack()
{
    const QDateTime now = at("Europe/Berlin", 2026, 10, 25, 0, 30);
    QCOMPARE(now.toUTC().time(), QTime(22, 30));
    const QVector<SnoozePreset> presets = snoozePresets(now);

    QCOMPARE(presets.first().kind, Kind::LaterToday);
    QCOMPARE(presets.first().at.toUTC(), QDateTime(QDate(2026, 10, 25), QTime(1, 0), QTimeZone::UTC));
    QCOMPARE(presets.first().at.time(), QTime(2, 0));
}

void SnoozeTest::customTimeIsReadInTheZone()
{
    const QDateTime now = at("America/New_York", 2026, 10, 14, 9, 0);
    const std::optional<QDateTime> picked = snoozeCustomTime(QStringLiteral("2026-10-20"), QStringLiteral("07:45"), now);

    QVERIFY(picked.has_value());
    QCOMPARE(picked->toUTC(), QDateTime(QDate(2026, 10, 20), QTime(11, 45), QTimeZone::UTC));
}

void SnoozeTest::customTimeRefusesThePastAndBeyondAYear()
{
    const QDateTime now = at("Europe/Berlin", 2026, 10, 14, 9, 0);

    QVERIFY(!snoozeCustomTime(QStringLiteral("2026-10-14"), QStringLiteral("08:59"), now));
    QVERIFY(!snoozeCustomTime(QStringLiteral("2026-10-14"), QStringLiteral("09:00"), now)); // not a minute ahead
    QVERIFY(snoozeCustomTime(QStringLiteral("2026-10-14"), QStringLiteral("09:01"), now));
    QVERIFY(snoozeCustomTime(QStringLiteral("2027-10-14"), QStringLiteral("09:00"), now));
    QVERIFY(!snoozeCustomTime(QStringLiteral("2027-10-14"), QStringLiteral("09:01"), now));
}

void SnoozeTest::customTimeRefusesASkippedWallClockTime()
{
    const QDateTime now = at("Europe/Berlin", 2026, 3, 20, 9, 0);

    QVERIFY(!snoozeCustomTime(QStringLiteral("2026-03-29"), QStringLiteral("02:30"), now));
    QVERIFY(snoozeCustomTime(QStringLiteral("2026-03-29"), QStringLiteral("03:30"), now));
}

void SnoozeTest::customTimeRefusesGarbage()
{
    const QDateTime now = at("Europe/Berlin", 2026, 10, 14, 9, 0);

    QVERIFY(!snoozeCustomTime(QString(), QString(), now));
    QVERIFY(!snoozeCustomTime(QStringLiteral("2026-02-30"), QStringLiteral("10:00"), now));
    QVERIFY(!snoozeCustomTime(QStringLiteral("2026-10-20"), QStringLiteral("24:00"), now));
    QVERIFY(!snoozeCustomTime(QStringLiteral("tomorrow"), QStringLiteral("10:00"), now));
}

void SnoozeTest::windowIsAMinuteToAYear()
{
    const QDateTime now = at("Europe/Berlin", 2026, 10, 14, 9, 0);

    QVERIFY(!snoozeTimeInWindow(now.addSecs(59), now));
    QVERIFY(snoozeTimeInWindow(now.addSecs(60), now));
    QVERIFY(snoozeTimeInWindow(now.addSecs(365 * 24 * 3600), now));
    QVERIFY(!snoozeTimeInWindow(now.addSecs(365 * 24 * 3600 + 1), now));
    QVERIFY(!snoozeTimeInWindow(now.addSecs(-3600), now));
    QVERIFY(!snoozeTimeInWindow(QDateTime(), now));
}

QTEST_GUILESS_MAIN(SnoozeTest)
#include "SnoozeTest.moc"
