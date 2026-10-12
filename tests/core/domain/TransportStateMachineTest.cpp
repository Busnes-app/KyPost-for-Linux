#include "domain/TransportStateMachine.h"

#include "db/Database.h"
#include "db/PushDao.h"
#include "domain/DevicePairing.h"
#include "domain/PairingStore.h"
#include "domain/PushRepository.h"
#include "models/PushNotification.h"
#include "net/HttpClient.h"
#include "net/PushNotificationClient.h"
#include "stores/CursorStore.h"
#include "stores/SecureStoreFile.h"
#include "stores/SettingsStore.h"

#include "../net/FakeRelayServer.h"

#include <QNetworkAccessManager>
#include <QSignalSpy>
#include <QTimeZone>
#include <QTemporaryDir>
#include <QTest>

namespace {

// Owns every real dependency PushRepository needs (matches the wiring
// pattern in PushRepositoryTest.cpp) so TransportStateMachine's polling
// tier can be exercised against a real PushRepository rather than a stub.
// pair(), when called, points the stored pairing at `fake` so pullOnce()
// actually reaches it; tests that never expect a network call (the
// distributor tier) simply leave the repository unpaired, since
// PushRepository::pullOnce() returns an empty vector without making a
// request when there is no stored pairing.
struct RepoHarness
{
    Database db;
    QTemporaryDir cursorDir;
    QTemporaryDir secureDir;
    QTemporaryDir settingsDir;
    CursorStore cursorStore;
    SecureStoreFile secureStore;
    PairingStore pairingStore;
    SettingsStore settingsStore;
    QNetworkAccessManager manager;
    HttpClient http;
    PushNotificationClient client;
    PushDao pushDao;
    PushRepository repository;

    RepoHarness()
        : cursorStore(cursorDir.filePath(QStringLiteral("cursors.ini")))
        , secureStore(secureDir.path())
        , pairingStore(secureStore)
        , settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")))
        , http(manager)
        , client(http)
        , pushDao(db.handle())
        , repository(pushDao, cursorStore, client, pairingStore, settingsStore)
    {
        db.open(QStringLiteral(":memory:"));
    }

    void pair(quint16 fakeRelayPort)
    {
        DevicePairing pairing;
        pairing.subscriberId = QStringLiteral("sub-1");
        pairing.serverBaseUrl = QStringLiteral("http://127.0.0.1:%1").arg(fakeRelayPort);
        pairing.registrationUrl = pairing.serverBaseUrl + QStringLiteral("/api/notifications/native/register");
        pairing.pairingToken = QStringLiteral("pair-tok");
        pairing.deviceId = QStringLiteral("device-1");
        pairing.deviceName = QStringLiteral("My Linux Desktop");
        pairing.deviceSecret = QStringLiteral("secret-1");
        pairingStore.save(pairing);
    }
};

} // namespace

// The embedded ntfy subscriber tier this suite used to cover (a FakeNtfyServer
// harness plus six tests spanning foreground/background transitions, the
// connection-loss failure budget and the promote-back retry timer) went away
// with that tier on 2026-07-26 -- see core/domain/TransportStateMachine.h.
// What remains is the two-tier decision and the polling fetch path.
class TransportStateMachineTest : public QObject
{
    Q_OBJECT

private slots:
    void startsInPolling();
    void distributorAvailableEntersDistributorAndStopsPolling();
    void distributorUnavailableReturnsToPolling();
    void enterTierIdempotencyEmitsTierChangedOnce();
    void pollTimerFetchesAndEmitsPollTick();
    void backstopWaitsForThreeHoursOfSilence();
    void backstopPullsOnceThenWaitsAgain();
    void aPushRestartsTheSilence();
    void backstopNeverRunsInPollingTier();
    void backstopSkipsWhatPushDeliveredAndOldBacklog();
};

namespace {

// Silence is measured from now(); tests move this instead of waiting hours.
struct FakeClock
{
    qint64 ms = QDateTime::currentMSecsSinceEpoch();
    std::function<qint64()> fn() { return [this]() { return ms; }; }
    void advance(qint64 by) { ms += by; }
};

constexpr qint64 kSilence = TransportStateMachine::kPushSilenceMs;

QByteArray pullBody(const QByteArray& items, int cursor)
{
    return R"({"deliveryMode":"push","cursor":)" + QByteArray::number(cursor)
        + R"(,"notifications":[)" + items + "]}";
}

QByteArray pullItem(int seq, const QString& messageId, qint64 createdAtMs)
{
    return QStringLiteral(R"({"seq":%1,"title":"T","body":"B","createdAt":"%2","data":{"messageId":"%3"}})")
        .arg(seq)
        .arg(QDateTime::fromMSecsSinceEpoch(createdAtMs, QTimeZone::UTC).toString(Qt::ISODate), messageId)
        .toUtf8();
}

} // namespace

void TransportStateMachineTest::startsInPolling()
{
    RepoHarness repo; // left unpaired -- pullOnce() is a no-op either way

    TransportStateMachine machine(repo.repository);

    QCOMPARE(machine.currentTier(), TransportTier::Polling);
}

void TransportStateMachineTest::distributorAvailableEntersDistributorAndStopsPolling()
{
    RepoHarness repo; // unpaired: pullOnce() is a no-op, so any stray poll tick would
                       // still carry an empty result rather than crash -- pollSpy's
                       // count is what actually proves the timer stopped

    // Short interval: proves the poll timer is genuinely stopped (not just
    // slow) by giving it many chances to fire during the wait window below.
    TransportStateMachine machine(repo.repository, nullptr, /*pollIntervalMs=*/20);
    QSignalSpy tierSpy(&machine, &TransportStateMachine::tierChanged);
    QSignalSpy pollSpy(&machine, &TransportStateMachine::pollTick);

    machine.setDistributorAvailable(true);

    QCOMPARE(machine.currentTier(), TransportTier::Distributor);
    QCOMPARE(tierSpy.size(), 1);
    QCOMPARE(tierSpy.at(0).at(0).value<TransportTier>(), TransportTier::Distributor);

    // ~15 missed 20ms intervals if the timer were still running.
    QTest::qWait(300);
    QCOMPARE(pollSpy.size(), 0);
}

void TransportStateMachineTest::distributorUnavailableReturnsToPolling()
{
    RepoHarness repo;

    TransportStateMachine machine(repo.repository, nullptr, /*pollIntervalMs=*/20);
    machine.setDistributorAvailable(true);
    QCOMPARE(machine.currentTier(), TransportTier::Distributor);

    QSignalSpy pollSpy(&machine, &TransportStateMachine::pollTick);
    machine.setDistributorAvailable(false);

    QCOMPARE(machine.currentTier(), TransportTier::Polling);
    // The poll timer is restarted by the transition, not merely left stopped.
    QVERIFY(pollSpy.wait());
}

void TransportStateMachineTest::enterTierIdempotencyEmitsTierChangedOnce()
{
    RepoHarness repo;

    TransportStateMachine machine(repo.repository, nullptr, /*pollIntervalMs=*/20);
    QSignalSpy tierSpy(&machine, &TransportStateMachine::tierChanged);

    machine.setDistributorAvailable(true);
    machine.setDistributorAvailable(true);
    machine.setDistributorAvailable(true);

    QCOMPARE(machine.currentTier(), TransportTier::Distributor);
    QCOMPARE(tierSpy.size(), 1);
}

// Proves the polling tier's fetch path is actually wired end to end: a real
// PushRepository pulling from a real FakeRelayServer, invoked by the poll
// timer firing (not by calling pullOnce() directly), with the result
// surfaced through pollTick. pollIntervalMs is overridden to a short value
// (see the constructor's doc comment) so this doesn't have to wait out the
// real 90s production cadence.
void TransportStateMachineTest::pollTimerFetchesAndEmitsPollTick()
{
    const QByteArray body = R"({"deliveryMode":"pull","cursor":7,"notifications":[)"
                             R"({"seq":7,"title":"Hi","body":"Body","data":{"messageId":"msg-1"}}]})";
    FakeRelayServer fake(httpResponse(200, "OK", body));

    RepoHarness repo;
    repo.pair(fake.port());

    TransportStateMachine machine(repo.repository, nullptr, /*pollIntervalMs=*/20);
    QSignalSpy pollSpy(&machine, &TransportStateMachine::pollTick);

    QCOMPARE(machine.currentTier(), TransportTier::Polling); // starts here by default
    QVERIFY(pollSpy.wait());

    QCOMPARE(pollSpy.size(), 1);
    const QVector<PushNotification> delivered = pollSpy.at(0).at(0).value<QVector<PushNotification>>();
    QCOMPARE(delivered.size(), 1);
    QCOMPARE(delivered.first().messageId, QStringLiteral("msg-1"));
    QVERIFY(fake.receivedRequest().contains("GET"));
}

void TransportStateMachineTest::backstopWaitsForThreeHoursOfSilence()
{
    FakeRelayServer fake(httpResponse(200, "OK", pullBody("", 0)));
    RepoHarness repo;
    repo.pair(fake.port());
    FakeClock clock;
    TransportStateMachine machine(repo.repository, nullptr, 90000, clock.fn());
    QSignalSpy backstopSpy(&machine, &TransportStateMachine::pushSilenceBackstop);
    machine.setDistributorAvailable(true);

    clock.advance(kSilence - 1);
    QVERIFY(!machine.checkPushSilence());
    QCOMPARE(backstopSpy.size(), 0);
    QVERIFY(fake.receivedRequest().isEmpty());
}

void TransportStateMachineTest::backstopPullsOnceThenWaitsAgain()
{
    FakeRelayServer fake(httpResponse(200, "OK", pullBody("", 0)));
    RepoHarness repo;
    repo.pair(fake.port());
    FakeClock clock;
    TransportStateMachine machine(repo.repository, nullptr, 90000, clock.fn());
    QSignalSpy pollSpy(&machine, &TransportStateMachine::pollTick);
    QSignalSpy backstopSpy(&machine, &TransportStateMachine::pushSilenceBackstop);
    machine.setDistributorAvailable(true);

    clock.advance(kSilence);
    QVERIFY(machine.checkPushSilence());
    QCOMPARE(pollSpy.size(), 1);
    QCOMPARE(backstopSpy.size(), 1);
    QVERIFY(fake.receivedRequest().contains("GET /api/notifications/native/pull"));

    // The backstop counts as hearing from the relay.
    clock.advance(kSilence - 1);
    QVERIFY(!machine.checkPushSilence());
    clock.advance(1);
    QVERIFY(machine.checkPushSilence());
    QCOMPARE(backstopSpy.size(), 2);
}

void TransportStateMachineTest::aPushRestartsTheSilence()
{
    RepoHarness repo; // unpaired: a pull would be a no-op, the return value is the proof
    FakeClock clock;
    TransportStateMachine machine(repo.repository, nullptr, 90000, clock.fn());
    machine.setDistributorAvailable(true);

    clock.advance(2 * 60 * 60 * 1000);
    machine.notePushReceived();
    clock.advance(kSilence - 1);
    QVERIFY(!machine.checkPushSilence());
    clock.advance(1);
    QVERIFY(machine.checkPushSilence());
}

void TransportStateMachineTest::backstopNeverRunsInPollingTier()
{
    RepoHarness repo;
    FakeClock clock;
    TransportStateMachine machine(repo.repository, nullptr, 90000, clock.fn());
    QSignalSpy backstopSpy(&machine, &TransportStateMachine::pushSilenceBackstop);

    clock.advance(10 * kSilence);
    QVERIFY(!machine.checkPushSilence());

    // Nor after the distributor goes away again.
    machine.setDistributorAvailable(true);
    machine.setDistributorAvailable(false);
    clock.advance(10 * kSilence);
    QVERIFY(!machine.checkPushSilence());
    QCOMPARE(backstopSpy.size(), 0);
}

// A distributor-tier cursor is never advanced, so the backstop's first pull
// returns the whole queue. Only what push missed, and is recent, may alert.
void TransportStateMachineTest::backstopSkipsWhatPushDeliveredAndOldBacklog()
{
    FakeClock clock;
    const qint64 at = clock.ms + kSilence;
    FakeRelayServer fake(httpResponse(200, "OK", pullBody(
        pullItem(1, QStringLiteral("too-old"), at - 2 * kSilence - 1) + ","
            + pullItem(2, QStringLiteral("pushed"), at - 60000) + ","
            + pullItem(3, QStringLiteral("missed"), at - 60000), 3)));
    RepoHarness repo;
    repo.pair(fake.port());
    PushNotification pushed;
    pushed.messageId = QStringLiteral("pushed");
    QVERIFY(repo.repository.recordPushArrival(pushed, clock.ms).has_value());
    QVERIFY(repo.pushDao.markConsumed(QStringLiteral("pushed")));

    TransportStateMachine machine(repo.repository, nullptr, 90000, clock.fn());
    QSignalSpy pollSpy(&machine, &TransportStateMachine::pollTick);
    machine.setDistributorAvailable(true);
    clock.advance(kSilence);
    QVERIFY(machine.checkPushSilence());

    const auto delivered = pollSpy.at(0).at(0).value<QVector<PushNotification>>();
    QCOMPARE(delivered.size(), 1);
    QCOMPARE(delivered.first().messageId, QStringLiteral("missed"));
    QVERIFY(!repo.pushDao.findById(QStringLiteral("too-old")).has_value());
    QVERIFY(repo.pushDao.findById(QStringLiteral("pushed"))->consumed); // not overwritten
    QCOMPARE(repo.cursorStore.notificationCursor(), qint64(3));
}

QTEST_GUILESS_MAIN(TransportStateMachineTest)
#include "TransportStateMachineTest.moc"
