#include "net/NativeRegistrationClient.h"

#include "net/HttpClient.h"

#include "FakeRelayServer.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QTest>

namespace {

// The client checks only the lengths; the relay validates the point.
WebPushSubscription keys()
{
    return { QByteArray(65, '\x04'), QByteArray(16, 'a') };
}

} // namespace

class NativeRegistrationClientTest : public QObject
{
    Q_OBJECT

private slots:
    void successParsesResponseAndSendsExpectedBody();
    void successOmitsOptionalFieldsWhenNotProvided();
    void successDerivesPullEndpointWhenServerOmitsIt();
    void unauthorizedFrom401();
    void aRedeemedTokenKeepsItsStatus();
    void backendMisconfiguredFrom503();
    void aPushRefreshUsesTheDeviceCredentialAndNoPairingToken();
    void aPushRefreshKeepsTheStatusOfARelayWithoutTheRoute();
    void noQueryParamsOnRequestUrl();
    void unusableCredentialInA200IsRefused_data();
    void unusableCredentialInA200IsRefused();
    void missingPushKeysAreRefusedWithoutARequest();
    void pendingFrom202CarriesTheApprovalCodeAndCredential();
    void a202WithoutAUsablePendingDeviceIsRefused_data();
    void a202WithoutAUsablePendingDeviceIsRefused();
    void pairingStatusMapsEveryAnswer_data();
    void pairingStatusMapsEveryAnswer();
    void pairingStatusSendsThePendingCredentialAsHeaders();
};

void NativeRegistrationClientTest::successParsesResponseAndSendsExpectedBody()
{
    const QByteArray body = R"({"ok":true,"synced":true,"deviceId":"dev-1","deviceSecret":"top-secret-device-secret",)"
                             R"("devices":2,"deliveryMode":"pull",)"
                             R"("pullEndpoint":"http://relay.example/api/notifications/native/pull",)"
                             R"("transport":"unifiedpush"})";
    FakeRelayServer fake(httpResponse(200, "OK", body));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    NativeRegistrationClient client(http);

    const QUrl endpoint(QStringLiteral("http://127.0.0.1:%1/api/notifications/native/register").arg(fake.port()));
    const NativeRegistrationResult result =
        client.registerDevice(endpoint, QStringLiteral("sub-1"), QStringLiteral("pair-tok"),
                               QStringLiteral("https://push.example/endpoint"), QStringLiteral("device-1"),
                               QStringLiteral("My Desktop"), keys());

    QCOMPARE(result.outcome, RegistrationOutcome::Success);
    QVERIFY(result.response.ok);
    QVERIFY(result.response.synced);
    QCOMPARE(result.response.deviceId, QStringLiteral("dev-1"));
    QCOMPARE(result.response.deviceSecret, QStringLiteral("top-secret-device-secret"));
    QCOMPARE(result.response.devices, 2);
    QCOMPARE(result.response.deliveryMode, QStringLiteral("pull"));
    QCOMPARE(result.response.pullEndpoint, QStringLiteral("http://relay.example/api/notifications/native/pull"));
    QCOMPARE(result.response.transport, QStringLiteral("unifiedpush"));

    const QJsonObject sent = fake.receivedJsonBody();
    QCOMPARE(sent.value(QStringLiteral("subscriberId")).toString(), QStringLiteral("sub-1"));
    QVERIFY(!sent.contains(QStringLiteral("subscriberHash")));
    QCOMPARE(sent.value(QStringLiteral("pairingToken")).toString(), QStringLiteral("pair-tok"));
    QCOMPARE(sent.value(QStringLiteral("deviceToken")).toString(), QStringLiteral("https://push.example/endpoint"));
    QCOMPARE(sent.value(QStringLiteral("deviceId")).toString(), QStringLiteral("device-1"));
    QCOMPARE(sent.value(QStringLiteral("deviceName")).toString(), QStringLiteral("My Desktop"));
    QCOMPARE(sent.value(QStringLiteral("platform")).toString(), QStringLiteral("linux"));
    QCOMPARE(sent.value(QStringLiteral("transport")).toString(), QStringLiteral("unifiedpush"));
    // Base64url without padding, the encoding webpush-go and webpush_keys.go accept.
    QCOMPARE(sent.value(QStringLiteral("p256dh")).toString(), QString(QStringLiteral("BAQE").repeated(21) + QStringLiteral("BAQ")));
    QCOMPARE(sent.value(QStringLiteral("auth")).toString(), QStringLiteral("YWFhYWFhYWFhYWFhYWFhYQ"));

    QVERIFY(fake.receivedRequest().contains("Content-Type: application/json"));
}

void NativeRegistrationClientTest::successOmitsOptionalFieldsWhenNotProvided()
{
    const QByteArray body = R"({"ok":true,"synced":false,"deviceId":"dev-2","deviceSecret":"sec-2","devices":1,)"
                             R"("deliveryMode":"push","pullEndpoint":"","transport":"unifiedpush"})";
    FakeRelayServer fake(httpResponse(200, "OK", body));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    NativeRegistrationClient client(http);

    const QUrl endpoint(QStringLiteral("http://127.0.0.1:%1/api/notifications/native/register").arg(fake.port()));
    const NativeRegistrationResult result = client.registerDevice(
        endpoint, QStringLiteral("sub-1"), QStringLiteral("pair-tok"), QStringLiteral("https://push.example/endpoint"),
        QString(), QString(), keys());

    QCOMPARE(result.outcome, RegistrationOutcome::Success);

    const QJsonObject sent = fake.receivedJsonBody();
    QVERIFY(!sent.contains(QStringLiteral("subscriberHash")));
    QVERIFY(!sent.contains(QStringLiteral("deviceId")));
    QVERIFY(!sent.contains(QStringLiteral("deviceName")));
    QVERIFY(!sent.contains(QStringLiteral("appVersion")));
}

void NativeRegistrationClientTest::successDerivesPullEndpointWhenServerOmitsIt()
{
    const QByteArray body = R"({"ok":true,"synced":true,"deviceId":"dev-3","deviceSecret":"sec-3","devices":1,)"
                             R"("deliveryMode":"pull","pullEndpoint":"","transport":"unifiedpush"})";
    FakeRelayServer fake(httpResponse(200, "OK", body));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    NativeRegistrationClient client(http);

    const QUrl endpoint(QStringLiteral("http://127.0.0.1:%1/api/notifications/native/register").arg(fake.port()));
    const NativeRegistrationResult result = client.registerDevice(
        endpoint, QStringLiteral("sub-1"), QStringLiteral("pair-tok"), QStringLiteral("https://push.example/endpoint"),
        QString(), QString(), keys());

    QCOMPARE(result.outcome, RegistrationOutcome::Success);
    QCOMPARE(result.response.pullEndpoint,
             QStringLiteral("http://127.0.0.1:%1/api/notifications/native/pull").arg(fake.port()));
}

// A 200 carrying any JSON object used to count as Success: `ok` was parsed
// and never read, and an empty deviceId/deviceSecret was stored as-is. That
// let a deep link whose `reg` named an unrelated same-origin endpoint -- the
// relay's unauthenticated /api/health answers POST with a JSON object --
// overwrite the working device credential with empty strings while the UI
// still reported "Paired".
void NativeRegistrationClientTest::unusableCredentialInA200IsRefused_data()
{
    QTest::addColumn<QByteArray>("body");

    QTest::newRow("health-endpoint-shaped")
        << QByteArray(R"({"status":"ok","uptime":42})");
    QTest::newRow("ok-false")
        << QByteArray(R"({"ok":false,"deviceId":"d","deviceSecret":"s"})");
    QTest::newRow("empty-deviceId")
        << QByteArray(R"({"ok":true,"deviceId":"","deviceSecret":"s"})");
    QTest::newRow("empty-deviceSecret")
        << QByteArray(R"({"ok":true,"deviceId":"d","deviceSecret":""})");
}

void NativeRegistrationClientTest::unusableCredentialInA200IsRefused()
{
    QFETCH(QByteArray, body);

    FakeRelayServer fake(httpResponse(200, "OK", body));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    NativeRegistrationClient client(http);

    const QUrl endpoint(QStringLiteral("http://127.0.0.1:%1/api/notifications/native/register").arg(fake.port()));
    const NativeRegistrationResult result = client.registerDevice(
        endpoint, QStringLiteral("sub-1"), QStringLiteral("pair-tok"), QStringLiteral("https://push.example/endpoint"),
        QString(), QString(), keys());

    QCOMPARE(result.outcome, RegistrationOutcome::Failure);
    QVERIFY(!result.detail.isEmpty());
    // Nothing usable escapes to the caller, so PairingStore::save() can never
    // be handed an empty credential to persist over a working one.
    QVERIFY(result.response.deviceSecret.isEmpty());
}

void NativeRegistrationClientTest::unauthorizedFrom401()
{
    FakeRelayServer fake(httpResponse(401, "Unauthorized", "{}"));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    NativeRegistrationClient client(http);

    const QUrl endpoint(QStringLiteral("http://127.0.0.1:%1/api/notifications/native/register").arg(fake.port()));
    const NativeRegistrationResult result = client.registerDevice(
        endpoint, QStringLiteral("sub-1"), QStringLiteral("pair-tok"), QStringLiteral("https://push.example/endpoint"),
        QString(), QString(), keys());

    QCOMPARE(result.outcome, RegistrationOutcome::Unauthorized);
}

// The relay redeems a pairing token once; a re-registration with the stored
// one is a 409, which main() reports like the 401 instead of dropping it.
void NativeRegistrationClientTest::aRedeemedTokenKeepsItsStatus()
{
    FakeRelayServer fake(httpResponse(409, "Conflict", "pairing token already used\n"));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    NativeRegistrationClient client(http);
    const QUrl endpoint(QStringLiteral("http://127.0.0.1:%1/api/notifications/native/register").arg(fake.port()));
    const NativeRegistrationResult result = client.registerDevice(
        endpoint, QStringLiteral("sub-1"), QStringLiteral("pair-tok"), QStringLiteral("https://push.example/endpoint"),
        QString(), QString(), keys());
    QCOMPARE(result.outcome, RegistrationOutcome::Failure);
    QCOMPARE(result.statusCode, 409);
}

void NativeRegistrationClientTest::backendMisconfiguredFrom503()
{
    FakeRelayServer fake(httpResponse(503, "Service Unavailable", "{}"));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    NativeRegistrationClient client(http);

    const QUrl endpoint(QStringLiteral("http://127.0.0.1:%1/api/notifications/native/register").arg(fake.port()));
    const NativeRegistrationResult result = client.registerDevice(
        endpoint, QStringLiteral("sub-1"), QStringLiteral("pair-tok"), QStringLiteral("https://push.example/endpoint"),
        QString(), QString(), keys());

    QCOMPARE(result.outcome, RegistrationOutcome::BackendMisconfigured);
}

void NativeRegistrationClientTest::noQueryParamsOnRequestUrl()
{
    FakeRelayServer fake(httpResponse(200, "OK", R"({"ok":true})"));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    NativeRegistrationClient client(http);

    const QUrl endpoint(QStringLiteral("http://127.0.0.1:%1/api/notifications/native/register").arg(fake.port()));
    client.registerDevice(endpoint, QStringLiteral("sub-1"), QStringLiteral("pair-tok"),
                           QStringLiteral("https://push.example/endpoint"), QString(), QString(), keys());

    // Unlike every other Relay endpoint in this batch, native/register takes
    // no query-param auth — sub must not be appended to the URL.
    QVERIFY(fake.receivedRequest().contains("POST /api/notifications/native/register HTTP/1.1"));
    QVERIFY(!fake.receivedRequest().contains("?"));
}

void NativeRegistrationClientTest::missingPushKeysAreRefusedWithoutARequest()
{
    // Registering without keys is what makes the relay send cleartext.
    FakeRelayServer fake(httpResponse(200, "OK", R"({"ok":true,"deviceId":"d","deviceSecret":"s"})"));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    NativeRegistrationClient client(http);

    const QUrl endpoint(QStringLiteral("http://127.0.0.1:%1/api/notifications/native/register").arg(fake.port()));
    const NativeRegistrationResult result =
        client.registerDevice(endpoint, QStringLiteral("sub-1"), QStringLiteral("pair-tok"),
                               QStringLiteral("https://push.example/endpoint"), QString(), QString(), {});

    QCOMPARE(result.outcome, RegistrationOutcome::Failure);
    QCOMPARE(fake.connectionCount(), 0);
}


void NativeRegistrationClientTest::pendingFrom202CarriesTheApprovalCodeAndCredential()
{
    const QByteArray body = R"({"ok":false,"synced":false,"status":"pending","error":"approve this device",)"
                             R"("deviceId":"3f1c","deviceSecret":"pending-secret","approvalCode":"048213",)"
                             R"("expiresAt":"2026-11-01T12:05:00Z",)"
                             R"("statusEndpoint":"https://mail.example.com/api/notifications/native/pairing-status",)"
                             R"("pollIntervalSeconds":3})";
    FakeRelayServer fake(httpResponse(202, "Accepted", body));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    NativeRegistrationClient client(http);

    const QUrl endpoint(QStringLiteral("http://127.0.0.1:%1/api/notifications/native/register").arg(fake.port()));
    const NativeRegistrationResult result = client.registerDevice(
        endpoint, QStringLiteral("sub-1"), QStringLiteral("pair-tok"), QStringLiteral("https://push.example/e"),
        QString(), QStringLiteral("host - Linux"), keys());

    QCOMPARE(result.outcome, RegistrationOutcome::PendingApproval);
    QCOMPARE(result.response.deviceId, QStringLiteral("3f1c"));
    QCOMPARE(result.response.deviceSecret, QStringLiteral("pending-secret"));
    QCOMPARE(result.response.approvalCode, QStringLiteral("048213"));
    QCOMPARE(result.response.expiresAt.toUTC().toString(Qt::ISODate), QStringLiteral("2026-11-01T12:05:00Z"));
    QCOMPARE(result.response.statusEndpoint,
             QStringLiteral("https://mail.example.com/api/notifications/native/pairing-status"));
    QCOMPARE(result.response.pollIntervalSeconds, 3);
    QVERIFY(!result.response.ok);

    // The approval prompt shows the OS next to the device name.
    QVERIFY(fake.receivedJsonBody().contains(QStringLiteral("osVersion")));
}

void NativeRegistrationClientTest::a202WithoutAUsablePendingDeviceIsRefused_data()
{
    QTest::addColumn<QByteArray>("body");
    QTest::newRow("no secret") << QByteArray(R"({"status":"pending","deviceId":"d","approvalCode":"048213"})");
    QTest::newRow("no id") << QByteArray(R"({"status":"pending","deviceSecret":"s","approvalCode":"048213"})");
    QTest::newRow("short code") << QByteArray(R"({"status":"pending","deviceId":"d","deviceSecret":"s","approvalCode":"4821"})");
    QTest::newRow("non-digit code") << QByteArray(R"({"status":"pending","deviceId":"d","deviceSecret":"s","approvalCode":"04821x"})");
    QTest::newRow("not pending") << QByteArray(R"({"status":"approved","deviceId":"d","deviceSecret":"s","approvalCode":"048213"})");
}

void NativeRegistrationClientTest::a202WithoutAUsablePendingDeviceIsRefused()
{
    QFETCH(QByteArray, body);
    FakeRelayServer fake(httpResponse(202, "Accepted", body));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    NativeRegistrationClient client(http);

    const QUrl endpoint(QStringLiteral("http://127.0.0.1:%1/api/notifications/native/register").arg(fake.port()));
    const NativeRegistrationResult result = client.registerDevice(
        endpoint, QStringLiteral("sub-1"), QStringLiteral("pair-tok"), QStringLiteral("https://push.example/e"),
        QString(), QString(), keys());
    QCOMPARE(result.outcome, RegistrationOutcome::Failure);
}

void NativeRegistrationClientTest::pairingStatusMapsEveryAnswer_data()
{
    using Status = PairingStatusResult::Status;
    QTest::addColumn<QByteArray>("response");
    QTest::addColumn<int>("status");
    QTest::addColumn<int>("retryAfter");
    QTest::addColumn<QString>("deviceId");

    QTest::newRow("202 pending") << httpResponse(202, "Accepted", R"({"status":"pending","pollIntervalSeconds":5})")
                                 << int(Status::Pending) << 0 << QString();
    QTest::newRow("200 approved")
        << httpResponse(200, "OK", R"({"ok":true,"synced":true,"status":"approved","deviceId":"merged",)"
                                   R"("deliveryMode":"pull","transport":"unifiedpush"})")
        << int(Status::Approved) << 0 << QStringLiteral("merged");
    QTest::newRow("200 without a device id")
        << httpResponse(200, "OK", R"({"ok":true,"status":"approved"})") << int(Status::Retry) << 0 << QString();
    QTest::newRow("200 not approved")
        << httpResponse(200, "OK", R"({"ok":true,"status":"pending","deviceId":"x"})") << int(Status::Retry) << 0
        << QString();
    QTest::newRow("410 rejected")
        << httpResponse(410, "Gone", R"({"status":"rejected","error":"denied or expired"})") << int(Status::Rejected)
        << 0 << QString();
    QTest::newRow("429 retry-after")
        << httpResponse(429, "Too Many Requests", "{}", "application/json", { { "Retry-After", "12" } })
        << int(Status::Retry) << 12 << QString();
    QTest::newRow("503") << httpResponse(503, "Service Unavailable", "{}") << int(Status::Retry) << 0 << QString();
    QTest::newRow("garbled 202") << httpResponse(202, "Accepted", "not json") << int(Status::Retry) << 0 << QString();
}

void NativeRegistrationClientTest::pairingStatusMapsEveryAnswer()
{
    QFETCH(QByteArray, response);
    QFETCH(int, status);
    QFETCH(int, retryAfter);
    QFETCH(QString, deviceId);
    FakeRelayServer fake(response);
    QNetworkAccessManager manager;
    HttpClient http(manager);
    NativeRegistrationClient client(http);

    const PairingStatusResult result = client.pairingStatus(
        QUrl(QStringLiteral("http://127.0.0.1:%1/api/notifications/native/pairing-status").arg(fake.port())),
        RelayAuth { QStringLiteral("dev"), QStringLiteral("secret") }, {});
    QCOMPARE(int(result.status), status);
    QCOMPARE(result.retryAfterSeconds, retryAfter);
    QCOMPARE(result.response.deviceId, deviceId);
}

void NativeRegistrationClientTest::pairingStatusSendsThePendingCredentialAsHeaders()
{
    FakeRelayServer fake(httpResponse(202, "Accepted", R"({"status":"pending"})"));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    NativeRegistrationClient client(http);

    client.pairingStatus(
        QUrl(QStringLiteral("http://127.0.0.1:%1/api/notifications/native/pairing-status").arg(fake.port())),
        RelayAuth { QStringLiteral("dev-pending"), QStringLiteral("pending-secret") }, {});
    const QByteArray request = fake.receivedRequest();
    QVERIFY(request.startsWith("GET /api/notifications/native/pairing-status HTTP/1.1"));
    QVERIFY(request.contains("X-Kypost-Device-Id: dev-pending"));
    QVERIFY(request.contains("X-Kypost-Device-Secret: pending-secret"));
}

// Owner answer Q4: an endpoint rotation goes to native/refresh with the
// device's own credential. The pairing token is single-use, so it never rides.
void NativeRegistrationClientTest::aPushRefreshUsesTheDeviceCredentialAndNoPairingToken()
{
    FakeRelayServer fake(httpResponse(200, "OK", R"({"ok":true,"deviceId":"dev-1","transport":"unifiedpush"})"));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    NativeRegistrationClient client(http);

    const NativeRegistrationResult result = client.refreshPushEndpoint(
        QUrl(QStringLiteral("http://127.0.0.1:%1").arg(fake.port())), RelayAuth{ QStringLiteral("dev-1"), QStringLiteral("s3cret") },
        QStringLiteral("https://push.example/new"), keys());
    QCOMPARE(result.outcome, RegistrationOutcome::Success);
    const QByteArray request = fake.receivedRequests().value(0);
    QVERIFY(request.startsWith("POST /api/notifications/native/refresh"));
    QVERIFY(request.contains("X-Kypost-Device-Id: dev-1"));
    QVERIFY(request.contains("X-Kypost-Device-Secret: s3cret"));
    const QJsonObject sent = fake.receivedJsonBody();
    QCOMPARE(sent.value(QStringLiteral("deviceToken")).toString(), QStringLiteral("https://push.example/new"));
    QCOMPARE(sent.value(QStringLiteral("transport")).toString(), QStringLiteral("unifiedpush"));
    QVERIFY(!sent.value(QStringLiteral("p256dh")).toString().isEmpty());
    QVERIFY(!sent.value(QStringLiteral("auth")).toString().isEmpty());
    QVERIFY(!sent.contains(QStringLiteral("pairingToken")));
}

// 404 is a relay from before the route: the caller falls back to re-registering.
void NativeRegistrationClientTest::aPushRefreshKeepsTheStatusOfARelayWithoutTheRoute()
{
    FakeRelayServer fake(httpResponse(404, "Not Found", "404 page not found\n"));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    NativeRegistrationClient client(http);

    const NativeRegistrationResult result = client.refreshPushEndpoint(
        QUrl(QStringLiteral("http://127.0.0.1:%1").arg(fake.port())), RelayAuth{ QStringLiteral("dev-1"), QStringLiteral("s3cret") },
        QStringLiteral("https://push.example/new"), keys());
    QCOMPARE(result.outcome, RegistrationOutcome::Failure);
    QCOMPARE(result.statusCode, 404);
}

QTEST_GUILESS_MAIN(NativeRegistrationClientTest)
#include "NativeRegistrationClientTest.moc"
