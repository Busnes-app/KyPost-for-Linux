#include "domain/DeviceApproval.h"

#include <QTest>

// The pure half of the approval wait (#516): how often to poll, when to stop,
// where to ask, and what an approval turns into.
class DeviceApprovalTest : public QObject
{
    Q_OBJECT

private slots:
    void pollDelayFollowsTheServerWithinBounds();
    void failuresBackOffAndRetryAfterIsHonoured();
    void deadlineIsTheServersExpiryClamped();
    void statusEndpointIsTrustedOnlySameOrigin();
    void approvalKeepsThePendingSecretAndTakesTheApprovedId();
};

void DeviceApprovalTest::pollDelayFollowsTheServerWithinBounds()
{
    QCOMPARE(DeviceApproval::nextPollDelayMs(3, 0, 0), 3000);
    QCOMPARE(DeviceApproval::nextPollDelayMs(0, 0, 0), 3000);    // absent: the contract's 3 s
    QCOMPARE(DeviceApproval::nextPollDelayMs(-5, 0, 0), 3000);
    QCOMPARE(DeviceApproval::nextPollDelayMs(1, 0, 0), 1000);
    QCOMPARE(DeviceApproval::nextPollDelayMs(3600, 0, 0), 30000); // a hostile interval is capped
}

void DeviceApprovalTest::failuresBackOffAndRetryAfterIsHonoured()
{
    QCOMPARE(DeviceApproval::nextPollDelayMs(3, 1, 0), 6000);
    QCOMPARE(DeviceApproval::nextPollDelayMs(3, 2, 0), 12000);
    QCOMPARE(DeviceApproval::nextPollDelayMs(3, 3, 0), 24000);
    QCOMPARE(DeviceApproval::nextPollDelayMs(3, 4, 0), 30000);
    QCOMPARE(DeviceApproval::nextPollDelayMs(3, 1000, 0), 30000);
    QCOMPARE(DeviceApproval::nextPollDelayMs(3, 1, 20), 20000); // longer Retry-After wins
    QCOMPARE(DeviceApproval::nextPollDelayMs(3, 2, 5), 12000);  // shorter one does not
    QCOMPARE(DeviceApproval::nextPollDelayMs(3, 0, 600), 30000);
}

void DeviceApprovalTest::deadlineIsTheServersExpiryClamped()
{
    const QDateTime now = QDateTime::fromString(QStringLiteral("2026-11-01T12:00:00Z"), Qt::ISODate);
    QCOMPARE(DeviceApproval::deadline(now.addSecs(300), now), now.addSecs(300));
    QCOMPARE(DeviceApproval::deadline(now.addSecs(10), now), now.addSecs(10));
    QCOMPARE(DeviceApproval::deadline(now.addSecs(86400), now), now.addSecs(DeviceApproval::kMaxWaitSeconds));
    QCOMPARE(DeviceApproval::deadline(QDateTime(), now), now.addSecs(DeviceApproval::kDefaultWaitSeconds));
    QCOMPARE(DeviceApproval::deadline(now.addSecs(-1), now), now.addSecs(DeviceApproval::kDefaultWaitSeconds));
}

void DeviceApprovalTest::statusEndpointIsTrustedOnlySameOrigin()
{
    const QUrl server(QStringLiteral("https://mail.example.com"));
    const QUrl derived(QStringLiteral("https://mail.example.com/api/notifications/native/pairing-status"));
    QCOMPARE(DeviceApproval::statusEndpoint(QString(), server), derived);
    QCOMPARE(DeviceApproval::statusEndpoint(QStringLiteral("https://evil.example/api/notifications/native/pairing-status"), server),
             derived);
    QCOMPARE(DeviceApproval::statusEndpoint(QStringLiteral("http://mail.example.com/api/notifications/native/pairing-status"), server),
             derived);
    QCOMPARE(DeviceApproval::statusEndpoint(QStringLiteral("https://mail.example.com:8443/x"), server), derived);
    QCOMPARE(DeviceApproval::statusEndpoint(QStringLiteral("https://mail.example.com/kypost/api/notifications/native/pairing-status"), server),
             QUrl(QStringLiteral("https://mail.example.com/kypost/api/notifications/native/pairing-status")));
    QCOMPARE(DeviceApproval::statusEndpoint(QString(), QUrl(QStringLiteral("http://127.0.0.1:8080"))),
             QUrl(QStringLiteral("http://127.0.0.1:8080/api/notifications/native/pairing-status")));
}

void DeviceApprovalTest::approvalKeepsThePendingSecretAndTakesTheApprovedId()
{
    NativeRegistrationResult pending;
    pending.outcome = RegistrationOutcome::PendingApproval;
    pending.response.deviceId = QStringLiteral("dev-pending");
    pending.response.deviceSecret = QStringLiteral("pending-secret");
    pending.response.approvalCode = QStringLiteral("048213");
    pending.peerSpkiSha256 = QByteArray(32, 'a');

    PairingStatusResult approved;
    approved.status = PairingStatusResult::Status::Approved;
    approved.response.deviceId = QStringLiteral("dev-merged");
    approved.response.deliveryMode = QStringLiteral("pull");
    approved.response.transport = QStringLiteral("unifiedpush");
    approved.certificatePinVerified = true;

    const NativeRegistrationResult done = DeviceApproval::approvedRegistration(pending, approved);
    QCOMPARE(done.outcome, RegistrationOutcome::Success);
    QVERIFY(done.response.ok);
    QCOMPARE(done.response.deviceId, QStringLiteral("dev-merged"));
    QCOMPARE(done.response.deviceSecret, QStringLiteral("pending-secret"));
    QCOMPARE(done.response.deliveryMode, QStringLiteral("pull"));
    QVERIFY(done.response.approvalCode.isEmpty());
    QCOMPARE(done.peerSpkiSha256, QByteArray(32, 'a')); // no fresh handshake on the poll
    QVERIFY(done.certificatePinVerified);

    approved.peerSpkiSha256 = QByteArray(32, 'b');
    QCOMPARE(DeviceApproval::approvedRegistration(pending, approved).peerSpkiSha256, QByteArray(32, 'b'));
}

QTEST_GUILESS_MAIN(DeviceApprovalTest)
#include "DeviceApprovalTest.moc"
