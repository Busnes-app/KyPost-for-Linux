#include "pairing/MfaController.h"

#include "domain/DevicePairing.h"
#include "domain/PairingStore.h"
#include "net/NetworkExecutor.h"
#include "stores/SecureStoreFile.h"

#include "../../core/net/FakeRelayServer.h"

#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

namespace {

constexpr qint64 kNow = 1760000000000;

MfaChallenge sampleChallenge()
{
    MfaChallenge c;
    c.challengeId = QStringLiteral("0123456789abcdef0123456789abcdef0123456789abcdef");
    c.issuedAtMs = kNow;
    c.matchDigits = QStringLiteral("07");
    c.decoyDigits = { QStringLiteral("42"), QStringLiteral("93") };
    c.ipAddress = QStringLiteral("203.0.113.9");
    return c;
}

// A second request with the SAME numbers, issued and received a minute later,
// so it outlives the first.
MfaChallenge laterChallengeWithSameNumbers()
{
    MfaChallenge c = sampleChallenge();
    c.challengeId = QStringLiteral("fedcba9876543210fedcba9876543210fedcba9876543210");
    c.issuedAtMs = kNow + 60 * 1000;
    c.ipAddress = QStringLiteral("198.51.100.77");
    return c;
}

// One paired controller against a fake relay, with a clock the test moves.
struct Harness
{
    explicit Harness(const QByteArray& response)
        : fake(response)
        , secureStore(dir.path())
        , pairingStore(secureStore)
        , executor(3000)
        , controller(executor, pairingStore, [this] { return clock; })
    {
        DevicePairing pairing;
        pairing.subscriberId = QStringLiteral("sub-1");
        pairing.serverBaseUrl = QStringLiteral("http://127.0.0.1:%1").arg(fake.port());
        pairing.registrationUrl = pairing.serverBaseUrl + QStringLiteral("/api/notifications/native/register");
        pairing.pairingToken = QStringLiteral("pair-tok");
        pairing.deviceId = QStringLiteral("dev-1");
        pairing.deviceName = QStringLiteral("Test Device");
        pairing.deviceSecret = QStringLiteral("secret-1");
        saved = pairingStore.save(pairing);
    }
    // NetworkExecutor's contract: receivers outlive shutdown().
    ~Harness() { executor.shutdown(); }

    qint64 clock = kNow;
    FakeRelayServer fake;
    QTemporaryDir dir;
    SecureStoreFile secureStore;
    PairingStore pairingStore;
    NetworkExecutor executor;
    MfaController controller;
    bool saved = false;
};

} // namespace

class MfaControllerTest : public QObject
{
    Q_OBJECT

private slots:
    void receiveOffersTheThreeNumbers();
    void aLockedAppSendsNothing();
    void approveSendsTheTappedNumberNotTheMatch();
    void numbersNotOfferedAreIgnored();
    void approvedChallengeDisappears();
    void denySendsApproveFalse();
    void retryableFailureKeepsTheChallenge();
    void expiredChallengeDisappears();
    void approvalExpiredUnderTheFingerGoesNowhere();
    void denyExpiredUnderTheFingerGoesNowhere();
    void answerNamesTheShownChallenge();
    void pairingChangeDropsChallenges();
};

void MfaControllerTest::receiveOffersTheThreeNumbers()
{
    Harness h(httpResponse(200, "OK", R"({"ok":true,"status":"approved"})"));
    QVERIFY(h.saved);
    QVERIFY(h.controller.receive(sampleChallenge()));
    QVERIFY(!h.controller.receive(sampleChallenge()));

    QVERIFY(h.controller.hasChallenge());
    QStringList choices = h.controller.choices();
    choices.sort();
    QCOMPARE(choices, QStringList({ QStringLiteral("07"), QStringLiteral("42"), QStringLiteral("93") }));
    QCOMPARE(h.controller.ipAddress(), QStringLiteral("203.0.113.9"));
    QCOMPARE(h.controller.secondsRemaining(), 300);
}

void MfaControllerTest::aLockedAppSendsNothing()
{
    Harness h(httpResponse(200, "OK", R"({"ok":true,"status":"approved"})"));
    QVERIFY(h.saved);
    QVERIFY(h.controller.receive(sampleChallenge()));
    h.controller.setAppLocked(true);

    h.controller.approve(h.controller.challengeId(), QStringLiteral("07"));
    h.controller.deny(h.controller.challengeId());

    QVERIFY(!h.controller.inFlight());
    QVERIFY(h.controller.resultIsError());
    QVERIFY(h.controller.hasChallenge());
    QTest::qWait(200);
    QVERIFY(h.fake.receivedRequest().isEmpty());
}

void MfaControllerTest::approveSendsTheTappedNumberNotTheMatch()
{
    Harness h(httpResponse(429, "Too Many Requests", R"({"error":"wrong number","status":"locked"})"));
    QVERIFY(h.saved);
    QVERIFY(h.controller.receive(sampleChallenge()));

    h.controller.approve(h.controller.challengeId(), QStringLiteral("93"));
    QVERIFY(h.controller.inFlight());
    QTRY_VERIFY_WITH_TIMEOUT(!h.controller.inFlight(), 5000);

    const QJsonObject sent = h.fake.receivedJsonBody();
    QCOMPARE(sent.value(QStringLiteral("matchDigits")).toString(), QStringLiteral("93"));
    QCOMPARE(sent.value(QStringLiteral("approve")).toBool(), true);
    QVERIFY(h.fake.receivedRequest().contains("X-Kypost-Device-Secret: secret-1"));
    // Locked is terminal: the challenge goes and the user is told why.
    QVERIFY(!h.controller.hasChallenge());
    QVERIFY(h.controller.resultIsError());
    QVERIFY(h.controller.resultMessage().contains(QStringLiteral("authenticator")));
}

void MfaControllerTest::numbersNotOfferedAreIgnored()
{
    Harness h(httpResponse(200, "OK", R"({"ok":true,"status":"approved"})"));
    QVERIFY(h.saved);
    QVERIFY(h.controller.receive(sampleChallenge()));

    h.controller.approve(h.controller.challengeId(), QStringLiteral("55"));
    QVERIFY(!h.controller.inFlight());
    QTest::qWait(200);
    QVERIFY(h.fake.receivedRequest().isEmpty());
}

void MfaControllerTest::approvedChallengeDisappears()
{
    Harness h(httpResponse(200, "OK", R"({"ok":true,"status":"approved"})"));
    QVERIFY(h.saved);
    QVERIFY(h.controller.receive(sampleChallenge()));

    h.controller.approve(h.controller.challengeId(), QStringLiteral("07"));
    QTRY_VERIFY_WITH_TIMEOUT(!h.controller.inFlight(), 5000);

    QVERIFY(!h.controller.hasChallenge());
    QVERIFY(!h.controller.resultIsError());
    QCOMPARE(h.controller.resultMessage(), QStringLiteral("Sign-in approved."));
    // Answered once, answerable once.
    QVERIFY(!h.controller.receive(sampleChallenge()));
}

void MfaControllerTest::denySendsApproveFalse()
{
    Harness h(httpResponse(200, "OK", R"({"ok":true,"status":"denied"})"));
    QVERIFY(h.saved);
    QVERIFY(h.controller.receive(sampleChallenge()));

    h.controller.deny(h.controller.challengeId());
    QTRY_VERIFY_WITH_TIMEOUT(!h.controller.inFlight(), 5000);

    const QJsonObject sent = h.fake.receivedJsonBody();
    QCOMPARE(sent.value(QStringLiteral("approve")).toBool(true), false);
    QCOMPARE(sent.value(QStringLiteral("matchDigits")).toString(), QString());
    QCOMPARE(h.controller.resultMessage(), QStringLiteral("Sign-in denied."));
    QVERIFY(!h.controller.hasChallenge());
}

void MfaControllerTest::retryableFailureKeepsTheChallenge()
{
    Harness h(httpResponse(500, "Internal Server Error", "boom"));
    QVERIFY(h.saved);
    QVERIFY(h.controller.receive(sampleChallenge()));

    h.controller.approve(h.controller.challengeId(), QStringLiteral("07"));
    QTRY_VERIFY_WITH_TIMEOUT(!h.controller.inFlight(), 5000);

    QVERIFY(h.controller.hasChallenge());
    QVERIFY(h.controller.resultIsError());
}

void MfaControllerTest::expiredChallengeDisappears()
{
    Harness h(httpResponse(200, "OK", R"({"ok":true,"status":"approved"})"));
    QVERIFY(h.saved);
    QVERIFY(h.controller.receive(sampleChallenge()));

    h.clock = kNow + MfaChallengeTracker::kLifetimeMs - 1000;
    h.controller.refresh();
    QVERIFY(h.controller.hasChallenge());
    QCOMPARE(h.controller.secondsRemaining(), 1);

    h.clock = kNow + MfaChallengeTracker::kLifetimeMs;
    h.controller.refresh();
    QVERIFY(!h.controller.hasChallenge());

    h.controller.approve(sampleChallenge().challengeId, QStringLiteral("07"));
    QVERIFY(!h.controller.inFlight());
    QTest::qWait(200);
    QVERIFY(h.fake.receivedRequest().isEmpty());
}

void MfaControllerTest::approvalExpiredUnderTheFingerGoesNowhere()
{
    Harness h(httpResponse(200, "OK", R"({"ok":true,"status":"approved"})"));
    QVERIFY(h.saved);
    QVERIFY(h.controller.receive(sampleChallenge()));
    h.clock = kNow + 60 * 1000;
    QVERIFY(h.controller.receive(laterChallengeWithSameNumbers()));
    // The first one stays on screen; the second only raises the count.
    const QString shown = h.controller.challengeId();
    QCOMPARE(shown, sampleChallenge().challengeId);
    QCOMPARE(h.controller.pendingCount(), 2);

    // The shown request expires between render and tap. The timer has not
    // fired, so the page still offers its numbers.
    h.clock = kNow + MfaChallengeTracker::kLifetimeMs;
    h.controller.approve(shown, QStringLiteral("07"));

    // The tap must not be redirected to the other request, whose numbers match.
    QVERIFY(!h.controller.inFlight());
    QVERIFY(h.controller.resultIsError());
    QTest::qWait(200);
    QVERIFY(h.fake.receivedRequest().isEmpty());
    // The other request is still there, for the user to look at first.
    QCOMPARE(h.controller.challengeId(), laterChallengeWithSameNumbers().challengeId);
}

void MfaControllerTest::denyExpiredUnderTheFingerGoesNowhere()
{
    Harness h(httpResponse(200, "OK", R"({"ok":true,"status":"denied"})"));
    QVERIFY(h.saved);
    QVERIFY(h.controller.receive(sampleChallenge()));
    h.clock = kNow + 60 * 1000;
    QVERIFY(h.controller.receive(laterChallengeWithSameNumbers()));
    const QString shown = h.controller.challengeId();

    h.clock = kNow + MfaChallengeTracker::kLifetimeMs;
    h.controller.deny(shown);

    QVERIFY(!h.controller.inFlight());
    QTest::qWait(200);
    QVERIFY(h.fake.receivedRequest().isEmpty());
    QVERIFY(h.controller.hasChallenge());
}

void MfaControllerTest::answerNamesTheShownChallenge()
{
    Harness h(httpResponse(200, "OK", R"({"ok":true,"status":"approved"})"));
    QVERIFY(h.saved);
    QVERIFY(h.controller.receive(sampleChallenge()));

    // No ID, or an ID that is not pending, sends nothing.
    h.controller.approve(QString(), QStringLiteral("07"));
    h.controller.approve(laterChallengeWithSameNumbers().challengeId, QStringLiteral("07"));
    QVERIFY(!h.controller.inFlight());
    QTest::qWait(200);
    QVERIFY(h.fake.receivedRequest().isEmpty());

    h.controller.approve(h.controller.challengeId(), QStringLiteral("07"));
    QTRY_VERIFY_WITH_TIMEOUT(!h.controller.inFlight(), 5000);
    QCOMPARE(h.fake.receivedJsonBody().value(QStringLiteral("challengeId")).toString(),
             sampleChallenge().challengeId);
}

void MfaControllerTest::pairingChangeDropsChallenges()
{
    Harness h(httpResponse(200, "OK", R"({"ok":true,"status":"approved"})"));
    QVERIFY(h.saved);
    QVERIFY(h.controller.receive(sampleChallenge()));
    h.controller.show();
    QVERIFY(h.controller.isOpen());

    h.controller.clear();
    QVERIFY(!h.controller.hasChallenge());
    QVERIFY(!h.controller.isOpen());
}

QTEST_GUILESS_MAIN(MfaControllerTest)
#include "MfaControllerTest.moc"
