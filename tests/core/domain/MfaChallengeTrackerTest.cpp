#include "domain/MfaChallengeTracker.h"

#include <QSet>
#include <QTest>

namespace {

constexpr qint64 kNow = 1760000000000;

MfaChallenge challenge(int n, qint64 issuedAtMs = kNow)
{
    MfaChallenge c;
    c.challengeId = QStringLiteral("%1").arg(n, 48, 16, QLatin1Char('0'));
    c.issuedAtMs = issuedAtMs;
    c.matchDigits = QStringLiteral("07");
    c.decoyDigits = QStringList{ QStringLiteral("42"), QStringLiteral("93") };
    return c;
}

} // namespace

class MfaChallengeTrackerTest : public QObject
{
    Q_OBJECT

private slots:
    void choicesAreTheMatchAndDecoys();
    void choicesAreShuffled();
    void duplicatesAreRefused();
    void answeredChallengesCannotBeReplayed();
    void expiresFiveMinutesAfterIssue();
    void alreadyExpiredIsRefused();
    void farFutureIssuedAtIsRefused();
    void futureIssuedAtCannotStretchTheWindow();
    void pendingIsCappedByEvictingTheOldest();
    void newestFirst();
    void clearDropsEverything();
};

void MfaChallengeTrackerTest::choicesAreTheMatchAndDecoys()
{
    MfaChallengeTracker tracker;
    QVERIFY(tracker.add(challenge(1), kNow));
    const auto p = tracker.pending(challenge(1).challengeId, kNow);
    QVERIFY(p.has_value());
    QStringList sorted = p->choices;
    sorted.sort();
    QCOMPARE(sorted, QStringList({ QStringLiteral("07"), QStringLiteral("42"), QStringLiteral("93") }));
}

void MfaChallengeTrackerTest::choicesAreShuffled()
{
    // The correct answer must not sit in a fixed slot. 60 draws all landing
    // in one position has probability 3 * (1/3)^60.
    QSet<int> positions;
    for (int i = 0; i < 60; ++i) {
        MfaChallengeTracker tracker;
        QVERIFY(tracker.add(challenge(1), kNow));
        positions.insert(int(tracker.allPending(kNow).first().choices.indexOf(QStringLiteral("07"))));
    }
    QVERIFY(positions.size() > 1);
}

void MfaChallengeTrackerTest::duplicatesAreRefused()
{
    MfaChallengeTracker tracker;
    QVERIFY(tracker.add(challenge(1), kNow));
    QVERIFY(!tracker.add(challenge(1), kNow + 1000));
    QCOMPARE(tracker.allPending(kNow).size(), 1);
}

void MfaChallengeTrackerTest::answeredChallengesCannotBeReplayed()
{
    MfaChallengeTracker tracker;
    QVERIFY(tracker.add(challenge(1), kNow));
    tracker.markAnswered(challenge(1).challengeId);
    QVERIFY(!tracker.pending(challenge(1).challengeId, kNow).has_value());
    QVERIFY(!tracker.add(challenge(1), kNow + 1000));
    QVERIFY(tracker.allPending(kNow).isEmpty());
}

void MfaChallengeTrackerTest::expiresFiveMinutesAfterIssue()
{
    MfaChallengeTracker tracker;
    QVERIFY(tracker.add(challenge(1, kNow - 60'000), kNow));
    const qint64 expiry = kNow - 60'000 + MfaChallengeTracker::kLifetimeMs;
    QVERIFY(tracker.pending(challenge(1).challengeId, expiry - 1).has_value());
    QVERIFY(!tracker.pending(challenge(1).challengeId, expiry).has_value());
}

void MfaChallengeTrackerTest::alreadyExpiredIsRefused()
{
    MfaChallengeTracker tracker;
    QVERIFY(!tracker.add(challenge(1, kNow - MfaChallengeTracker::kLifetimeMs), kNow));
    QVERIFY(tracker.allPending(kNow).isEmpty());
}

void MfaChallengeTrackerTest::farFutureIssuedAtIsRefused()
{
    MfaChallengeTracker tracker;
    QVERIFY(!tracker.add(challenge(1, kNow + MfaChallengeTracker::kMaxFutureSkewMs + 1), kNow));
    QVERIFY(tracker.add(challenge(2, kNow + MfaChallengeTracker::kMaxFutureSkewMs), kNow));
}

void MfaChallengeTrackerTest::futureIssuedAtCannotStretchTheWindow()
{
    MfaChallengeTracker tracker;
    QVERIFY(tracker.add(challenge(1, kNow + 60'000), kNow));
    QCOMPARE(tracker.allPending(kNow).first().expiresAtMs, kNow + MfaChallengeTracker::kLifetimeMs);
}

void MfaChallengeTrackerTest::pendingIsCappedByEvictingTheOldest()
{
    MfaChallengeTracker tracker;
    for (int i = 0; i <= MfaChallengeTracker::kMaxPending; ++i)
        QVERIFY(tracker.add(challenge(i + 1, kNow - 100'000 + i), kNow));
    const QList<MfaChallengeTracker::Pending> all = tracker.allPending(kNow);
    QCOMPARE(all.size(), MfaChallengeTracker::kMaxPending);
    QVERIFY(!tracker.pending(challenge(1).challengeId, kNow).has_value());
    QVERIFY(tracker.pending(challenge(MfaChallengeTracker::kMaxPending + 1).challengeId, kNow).has_value());
}

void MfaChallengeTrackerTest::newestFirst()
{
    MfaChallengeTracker tracker;
    QVERIFY(tracker.add(challenge(1, kNow - 2000), kNow));
    QVERIFY(tracker.add(challenge(2, kNow - 1000), kNow));
    QCOMPARE(tracker.allPending(kNow).first().challenge.challengeId, challenge(2).challengeId);
}

void MfaChallengeTrackerTest::clearDropsEverything()
{
    MfaChallengeTracker tracker;
    QVERIFY(tracker.add(challenge(1), kNow));
    tracker.clear();
    QVERIFY(tracker.allPending(kNow).isEmpty());
}

QTEST_GUILESS_MAIN(MfaChallengeTrackerTest)
#include "MfaChallengeTrackerTest.moc"
