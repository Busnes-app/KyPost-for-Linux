#include "mail/ScheduledController.h"

#include "domain/DevicePairing.h"
#include "domain/PairingStore.h"
#include "net/NetworkExecutor.h"
#include "stores/SecureStoreFile.h"

#include "../../core/net/FakeRelayServer.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

namespace {

const char* const kList = R"({"scheduled":[)"
    R"({"id":"w-1","status":"scheduled","reason":"","sendAt":"2026-10-18T06:00:00Z","subject":"[Encrypted] Email Sent by KyPost","to":["x@example.com"]},)"
    R"({"id":"r-2","status":"returned","reason":"canceled","sendAt":"2026-10-17T06:00:00Z","subject":"Plans","to":["y@example.com","z@example.com"]}]})";

// Saturday 2026-10-17, 09:20 in Berlin.
QDateTime fixedNow()
{
    return QDateTime(QDate(2026, 10, 17), QTime(9, 20), QTimeZone("Europe/Berlin"));
}

struct Harness
{
    explicit Harness(const QByteArray& response)
        : fake(response)
        , secureStore(dir.path())
        , pairingStore(secureStore)
        , executor(3000)
        , controller(executor, pairingStore, [] { return fixedNow(); })
    {
        DevicePairing pairing;
        pairing.subscriberId = QStringLiteral("sub-1");
        pairing.serverBaseUrl = QStringLiteral("http://127.0.0.1:%1").arg(fake.port());
        pairing.deviceId = QStringLiteral("dev-1");
        pairing.deviceSecret = QStringLiteral("secret-1");
        saved = pairingStore.save(pairing);
    }
    ~Harness() { executor.shutdown(); }

    FakeRelayServer fake;
    QTemporaryDir dir;
    SecureStoreFile secureStore;
    PairingStore pairingStore;
    NetworkExecutor executor;
    ScheduledController controller;
    bool saved = false;
};

} // namespace

class ScheduledControllerTest : public QObject
{
    Q_OBJECT

private slots:
    void presetsAreTheCoreChoices();
    void customTimeKeepsToThePickerWindow();
    void refreshListsWhatTheRelayHolds();
    void onlyAWaitingMessageIsCancellable();
    void cancelReturnsItAndReloads();
    void aRelayWithoutSchedulingSaysSo();
    void aReplyForAReplacedAccountIsDropped();
    void lockingDropsTheListAndRefusesRefreshAndCancel();
    void aListArrivingUnderTheLockIsNotShown();
};

void ScheduledControllerTest::presetsAreTheCoreChoices()
{
    Harness h(QByteArray{});
    const QVariantList presets = h.controller.presets();
    QCOMPARE(presets.size(), 4);
    QCOMPARE(presets.at(0).toMap().value(QStringLiteral("atMs")).toLongLong(),
             QDateTime(QDate(2026, 10, 17), QTime(12, 0), QTimeZone("Europe/Berlin")).toMSecsSinceEpoch());
    QVERIFY(presets.at(0).toMap().value(QStringLiteral("label")).toString().startsWith(QStringLiteral("Later today")));
}

void ScheduledControllerTest::customTimeKeepsToThePickerWindow()
{
    Harness h(QByteArray{});
    QCOMPARE(h.controller.customTime(QStringLiteral("2026-10-17"), QStringLiteral("14:30")),
             QDateTime(QDate(2026, 10, 17), QTime(14, 30), QTimeZone("Europe/Berlin")).toMSecsSinceEpoch());
    QCOMPARE(h.controller.customTime(QStringLiteral("2026-10-17"), QStringLiteral("9:00")), 0); // past
    QCOMPARE(h.controller.customTime(QStringLiteral("2026-10-17"), QStringLiteral("9:22")), 0); // under 5 minutes
    QCOMPARE(h.controller.customTime(QStringLiteral("2027-10-18"), QStringLiteral("10:00")), 0); // over a year
    QCOMPARE(h.controller.customTime(QStringLiteral("tomorrow"), QStringLiteral("10:00")), 0);
    QCOMPARE(h.controller.customTime(QStringLiteral("2026-10-18"), QStringLiteral("25:00")), 0);
}

void ScheduledControllerTest::refreshListsWhatTheRelayHolds()
{
    Harness h(httpResponse(200, "OK", kList));
    QVERIFY(h.saved);
    h.controller.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(!h.controller.busy() && h.controller.items().size() == 2, 10000);

    QVERIFY(h.fake.receivedRequest().startsWith("GET /api/scheduled "));
    const QVariantMap encrypted = h.controller.items().at(0).toMap();
    QCOMPARE(encrypted.value(QStringLiteral("subject")).toString(), QStringLiteral("Encrypted message"));
    QCOMPARE(encrypted.value(QStringLiteral("to")).toString(), QStringLiteral("x@example.com"));
    QVERIFY(!encrypted.value(QStringLiteral("when")).toString().isEmpty());
    const QVariantMap returned = h.controller.items().at(1).toMap();
    QCOMPARE(returned.value(QStringLiteral("subject")).toString(), QStringLiteral("Plans"));
    QCOMPARE(returned.value(QStringLiteral("to")).toString(), QStringLiteral("y@example.com, z@example.com"));
    QCOMPARE(returned.value(QStringLiteral("status")).toString(), QStringLiteral("Returned to Drafts"));
    QCOMPARE(returned.value(QStringLiteral("reason")).toString(), QStringLiteral("canceled"));
}

void ScheduledControllerTest::onlyAWaitingMessageIsCancellable()
{
    Harness h(httpResponse(200, "OK", kList));
    h.controller.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(h.controller.items().size() == 2, 10000);
    QVERIFY(h.controller.items().at(0).toMap().value(QStringLiteral("cancellable")).toBool());
    QVERIFY(!h.controller.items().at(1).toMap().value(QStringLiteral("cancellable")).toBool());
}

void ScheduledControllerTest::cancelReturnsItAndReloads()
{
    Harness h(httpResponse(200, "OK", kList));
    h.fake.setResponseForPath("/cancel", httpResponse(200, "OK", R"({"ok":true,"status":"returned"})"));

    h.controller.cancel(QStringLiteral("w-1"));
    QTRY_VERIFY_WITH_TIMEOUT(!h.controller.busy() && h.controller.items().size() == 2, 10000);

    QCOMPARE(h.fake.receivedRequests().size(), 2);
    QVERIFY(h.fake.receivedRequests().at(0).startsWith("POST /api/scheduled/w-1/cancel "));
    QVERIFY(h.fake.receivedRequests().at(1).startsWith("GET /api/scheduled "));
    QCOMPARE(h.controller.message(), QStringLiteral("Returned to Drafts."));
}

void ScheduledControllerTest::aRelayWithoutSchedulingSaysSo()
{
    Harness h(httpResponse(404, "Not Found", "404 page not found", "text/plain"));
    h.controller.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(!h.controller.busy() && !h.controller.message().isEmpty(), 10000);
    QCOMPARE(h.controller.message(), QStringLiteral("This server can't schedule mail."));
    QVERIFY(h.controller.items().isEmpty());
}

void ScheduledControllerTest::aReplyForAReplacedAccountIsDropped()
{
    Harness h(httpResponse(200, "OK", kList));
    h.controller.refresh();
    DevicePairing other;
    other.subscriberId = QStringLiteral("sub-2");
    other.serverBaseUrl = QStringLiteral("http://127.0.0.1:%1").arg(h.fake.port());
    other.deviceId = QStringLiteral("dev-2");
    other.deviceSecret = QStringLiteral("secret-2");
    QVERIFY(h.pairingStore.save(other));
    h.controller.clear();

    QTRY_VERIFY_WITH_TIMEOUT(!h.controller.busy(), 10000);
    QTest::qWait(50);
    QVERIFY(h.controller.items().isEmpty());
}

// Subjects and recipients are mail metadata: not held, fetched or changed
// while the PIN screen is up.
void ScheduledControllerTest::lockingDropsTheListAndRefusesRefreshAndCancel()
{
    Harness h(httpResponse(200, "OK", kList));
    h.controller.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(!h.controller.busy() && h.controller.items().size() == 2, 10000);

    h.controller.setAppLocked(true);
    QVERIFY(h.controller.items().isEmpty());
    h.controller.refresh();
    h.controller.cancel(QStringLiteral("w-1"));
    QVERIFY(!h.controller.busy());
    QTest::qWait(100);
    QCOMPARE(h.fake.receivedRequests().size(), 1);

    h.controller.setAppLocked(false);
    h.controller.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(!h.controller.busy() && h.controller.items().size() == 2, 10000);
}

void ScheduledControllerTest::aListArrivingUnderTheLockIsNotShown()
{
    Harness h(httpResponse(200, "OK", kList));
    h.controller.refresh();
    h.controller.setAppLocked(true);
    QTRY_VERIFY_WITH_TIMEOUT(!h.controller.busy(), 10000);
    QTest::qWait(50);
    QVERIFY(h.controller.items().isEmpty());
}

QTEST_GUILESS_MAIN(ScheduledControllerTest)
#include "ScheduledControllerTest.moc"
