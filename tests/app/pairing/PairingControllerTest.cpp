#include "pairing/PairingController.h"

#include "domain/DeviceRegistrationService.h"
#include "domain/DevicePairing.h"
#include "domain/PairingStore.h"
#include "net/DeregisterClient.h"
#include "net/CertificatePinSink.h"
#include "net/NetworkExecutor.h"
#include "net/HttpClient.h"
#include "net/NativeRegistrationClient.h"
#include "stores/SecureStoreFile.h"
#include "stores/SettingsStore.h"

#include "../../core/net/FakeRelayServer.h"
#include "../../core/net/TestTls.h"

#include <QJsonObject>
#include <QMap>
#include <QNetworkAccessManager>
#include <QDateTime>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QSslSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QSysInfo>
#include <memory>
#include <QUrl>
#include <QUrlQuery>

class PairingControllerTest : public QObject
{
    Q_OBJECT

private slots:
    void pushStatusShowsOnlyTheDistributorHost();
    void pairFromDeepLinkEntersConfirmStateWithoutNetworkCall();
    void confirmPendingPairWithNoPendingRequestFails();
    void cancelPendingPairDiscardsRequestWithNoNetworkCall();
    void pairFromDeepLinkHappyPathPairsAndPersists();
    void pairFromDeepLinkSendsDeviceTokenWhenSet();
    void pairFromDeepLinkDerivesRegistrationUrlFromSrvWhenRegOmitted();
    void pairFromDeepLinkMissingRequiredParam_data();
    void pairFromDeepLinkMissingRequiredParam();
    void pairFromDeepLinkRejectsNonNativePairHost();
    void pairFromDeepLinkRejectsPlaintextHttpServerUrl();
    void pairFromDeepLinkAllowsPlaintextHttpForLoopbackServerUrl();
    void pairFromDeepLinkRejectsRegOnDifferentOriginThanSrv();
    void pairFromDeepLinkArmsTheServerPublishedPin();
    void pairFromDeepLinkRefusesAMalformedPin_data();
    void pairFromDeepLinkRefusesAMalformedPin();
    void pairFromDeepLinkNotifiesFreshPendingPairEvenWhenStateLabelUnchanged();
    void pairFromPastedLinkRejectsNonLinkTextWithNoNetworkCall();
    void refreshFromStoreReflectsPreSeededPairingStoreAndRemovePairingClears();
    void removePairingSkipsNetworkCallWhenNoDeviceSecretStored();
    void removePairingDeregistersServerSideWhenDeviceSecretPresent();
    void resetReturnsToIdleAfterFailure();
    void certificateMismatchCarriesBothFingerprintsInComparableForm();
    void reconnectToServerIsRefusedWhileAppLocked();
    void reconnectToServerKeepsThePairingAndDoesNotDeregister();
    void pairingADifferentAccountPurgesThePreviousAccountsCachedData();
    void pairingTheSameAccountAgainPurgesNothing();
    void unpairingThenPairingAnotherAccountStillPurges();
    void unpairingThenPairingTheSameAccountPurgesNothing();
    void aLinkThatRepinsThisAccountSaysSo();
    void aFailedRegistrationDestroysNothing();
    void aFailedPurgeRefusesTheNewPairingAndEscalates();
    void anUnreadableSecretStoreRefusesToPairRatherThanSkipThePurge();

    // Behaviour that only exists once the registration is asynchronous.
    void confirmPendingPairReturnsWithoutBlocking();
    void aSecondConfirmWhileOneIsInFlightIsIgnored();
    void pairFromDeepLinkRefusedWhileAppLocked();
    void confirmPendingPairRefusedWhenAppLocksAfterConfirmStateEntered();
    void pairFromDeepLinkWorksAgainAfterUnlock();
    void pendingPairOriginDisclosesSchemeAndPort();
    void registrationUrlWithAForeignPathIsRejected();
    void punycodeHostsAreShownInAsciiFormNotDecoded();
    void removePairingIsRefusedWhileLocked();

    // New-device approval (#516): register answers 202 for a web-minted token.
    void approvalThenApprovedCompletesPairing();
    void approvalDeniedReturnsToTheStart();
    void cancelApprovalStopsPollingAndWipesTheCredential();
    void approvalExpiresAtTheServersDeadline();
    void approvalPollToAnotherTrustedIssuerIsRefusedBeforeTheCredentialIsSent();
    void approvalOverTlsCompletesPinnedToTheRegistrationsIssuer();
    void aNewWaitStillPollsWhileACancelledPollIsOut();
    void aDirectRegistrationPairsAtOnceWithoutPolling();

private:
    // Builds a kypost://native-pair?... link from a param map, letting
    // callers omit keys to exercise the missing-required-param path.
    static QUrl buildLink(const QMap<QString, QString>& params);
    // 32 bytes whose base64 contains both '+' and '/'. Not arbitrary: a pin
    // that happens to encode to the alphanumeric subset would sail through
    // the percent-encoding these tests exist to exercise, and would make the
    // "'+' arrived as a space" row a no-op rewrite of a valid pin.
    static QByteArray samplePinBytes();
};

QByteArray PairingControllerTest::samplePinBytes()
{
    QByteArray raw(32, '\0');
    for (int i = 0; i < raw.size(); ++i)
        raw[i] = static_cast<char>((i * i + 251) % 256);
    return raw; // base64: "+/z/BAsUHyw7TF90i6S/3PscP2SLtN8MO2yf1AtEf7w="
}

QUrl PairingControllerTest::buildLink(const QMap<QString, QString>& params)
{
    QUrl url;
    url.setScheme(QStringLiteral("kypost"));
    url.setHost(QStringLiteral("native-pair"));
    QUrlQuery query;
    for (auto it = params.constBegin(); it != params.constEnd(); ++it)
        query.addQueryItem(it.key(), it.value());
    url.setQuery(query);
    return url;
}

void PairingControllerTest::pairFromDeepLinkEntersConfirmStateWithoutNetworkCall()
{
    // VibeSec regression guard: a recognized link must never hit the
    // network until confirmPendingPair() is called explicitly -- see
    // PairingController::pairFromDeepLink's doc comment.
    FakeRelayServer fake(httpResponse(200, "OK", R"({"ok":true,"deviceId":"should-not-be-used"})"));

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));

    QNetworkAccessManager manager;
    HttpClient http(manager);
    NativeRegistrationClient client(http);
    HttpClientPinSink pinSink(http);
    NetworkExecutor executor(3000);
    DeviceRegistrationService service(client, pairingStore, settingsStore, pinSink);

    PairingController controller(service, pairingStore, settingsStore, pinSink, executor);

    QMap<QString, QString> params;
    params[QStringLiteral("sub")] = QStringLiteral("sub-confirm");
    params[QStringLiteral("srv")] = QStringLiteral("http://127.0.0.1:%1").arg(fake.port());
    params[QStringLiteral("pt")] = QStringLiteral("pair-tok-confirm");

    QVERIFY(controller.pairFromDeepLink(buildLink(params)));

    QCOMPARE(controller.pairingState(), QStringLiteral("confirm"));
    QCOMPARE(controller.pendingPairOrigin(), QStringLiteral("http://127.0.0.1:%1").arg(fake.port()));
    QVERIFY(!controller.isPaired());
    QVERIFY(fake.receivedRequest().isEmpty());
    QVERIFY(!pairingStore.load().has_value());
}

void PairingControllerTest::confirmPendingPairWithNoPendingRequestFails()
{
    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));

    QNetworkAccessManager manager;
    HttpClient http(manager);
    NativeRegistrationClient client(http);
    HttpClientPinSink pinSink(http);
    NetworkExecutor executor(3000);
    DeviceRegistrationService service(client, pairingStore, settingsStore, pinSink);

    PairingController controller(service, pairingStore, settingsStore, pinSink, executor);

    QVERIFY(!controller.confirmPendingPair());
    QCOMPARE(controller.pairingState(), QStringLiteral("failed"));
}

void PairingControllerTest::cancelPendingPairDiscardsRequestWithNoNetworkCall()
{
    FakeRelayServer fake(httpResponse(200, "OK", R"({"ok":true,"deviceId":"should-not-be-used"})"));

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));

    QNetworkAccessManager manager;
    HttpClient http(manager);
    NativeRegistrationClient client(http);
    HttpClientPinSink pinSink(http);
    NetworkExecutor executor(3000);
    DeviceRegistrationService service(client, pairingStore, settingsStore, pinSink);

    PairingController controller(service, pairingStore, settingsStore, pinSink, executor);

    QMap<QString, QString> params;
    params[QStringLiteral("sub")] = QStringLiteral("sub-cancel");
    params[QStringLiteral("srv")] = QStringLiteral("http://127.0.0.1:%1").arg(fake.port());
    params[QStringLiteral("pt")] = QStringLiteral("pair-tok-cancel");

    QVERIFY(controller.pairFromDeepLink(buildLink(params)));
    QCOMPARE(controller.pairingState(), QStringLiteral("confirm"));

    controller.cancelPendingPair();

    QCOMPARE(controller.pairingState(), QStringLiteral("idle"));
    QVERIFY(!controller.isPaired());
    QVERIFY(fake.receivedRequest().isEmpty());
    QVERIFY(!pairingStore.load().has_value());

    // The pending request is gone -- a later confirm has nothing to act on.
    QVERIFY(!controller.confirmPendingPair());
    QVERIFY(fake.receivedRequest().isEmpty());
}

void PairingControllerTest::pairFromDeepLinkHappyPathPairsAndPersists()
{
    const QByteArray body = R"({"ok":true,"synced":true,"deviceId":"dev-1","deviceSecret":"fresh-device-secret",)"
                             R"("devices":1,"deliveryMode":"pull",)"
                             R"("pullEndpoint":"http://relay.example/api/notifications/native/pull",)"
                             R"("transport":"unifiedpush"})";
    FakeRelayServer fake(httpResponse(200, "OK", body));

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));

    QNetworkAccessManager manager;
    HttpClient http(manager);
    NativeRegistrationClient client(http);
    HttpClientPinSink pinSink(http);
    NetworkExecutor executor(3000);
    DeviceRegistrationService service(client, pairingStore, settingsStore, pinSink);

    PairingController controller(service, pairingStore, settingsStore, pinSink, executor);
    QVERIFY(!controller.isPaired());
    QCOMPARE(controller.pairingState(), QStringLiteral("idle"));

    QSignalSpy pairingChangedSpy(&controller, &PairingController::pairingChanged);
    QSignalSpy stateChangedSpy(&controller, &PairingController::pairingStateChanged);
    QSignalSpy pairedFromLinkSpy(&controller, &PairingController::pairedFromLink);

    const QString serverBaseUrl = QStringLiteral("http://127.0.0.1:%1").arg(fake.port());
    const QString registrationUrl = serverBaseUrl + QStringLiteral("/api/notifications/native/register");

    QMap<QString, QString> params;
    params[QStringLiteral("sub")] = QStringLiteral("sub-1");
    params[QStringLiteral("srv")] = serverBaseUrl;
    params[QStringLiteral("pt")] = QStringLiteral("pair-tok");
    params[QStringLiteral("reg")] = registrationUrl;

    QVERIFY(controller.pairFromDeepLink(buildLink(params)));

    // VibeSec fix: a recognized link waits for explicit confirmation before
    // any network call -- see PairingController::pairFromDeepLink's doc
    // comment.
    QCOMPARE(controller.pairingState(), QStringLiteral("confirm"));
    QCOMPARE(controller.pendingPairOrigin(), QStringLiteral("http://127.0.0.1:%1").arg(fake.port()));
    QVERIFY(fake.receivedRequest().isEmpty());

    // Returns "started", not "paired": the registration is now dispatched
    // off this thread and the answer arrives on pairingState.
    QVERIFY(controller.confirmPendingPair());
    QCOMPARE(controller.pairingState(), QStringLiteral("working"));

    QTRY_COMPARE_WITH_TIMEOUT(controller.pairingState(), QStringLiteral("paired"), 5000);
    QVERIFY(controller.pairingError().isEmpty());
    QVERIFY(controller.isPaired());
    // What the app-lock offer (Android #181) waits for.
    QCOMPARE(pairedFromLinkSpy.count(), 1);
    QCOMPARE(controller.deviceId(), QStringLiteral("dev-1"));
    QCOMPARE(controller.pairedServerHost(), QStringLiteral("127.0.0.1"));
    // Task 39: deliveryMode/transport read straight through SettingsStore,
    // written by DeviceRegistrationService::pair() from this same response
    // body ("deliveryMode":"pull","transport":"unifiedpush" above).
    QCOMPARE(controller.deliveryMode(), QStringLiteral("pull"));
    QCOMPARE(controller.transport(), QStringLiteral("unifiedpush"));
    // "confirm" then "working" then "paired" -- at least three transitions.
    QVERIFY(stateChangedSpy.count() >= 3);
    QVERIFY(pairingChangedSpy.count() >= 1);

    const std::optional<DevicePairing> loaded = pairingStore.load();
    QVERIFY(loaded.has_value());
    QCOMPARE(loaded->subscriberId, QStringLiteral("sub-1"));
    QCOMPARE(loaded->deviceSecret, QStringLiteral("fresh-device-secret"));
    QCOMPARE(loaded->registrationUrl, registrationUrl);
    QCOMPARE(loaded->deviceId, QStringLiteral("dev-1"));
    const QString expectedName = QSysInfo::machineHostName().trimmed().isEmpty()
        ? QStringLiteral("Linux")
        : QSysInfo::machineHostName().trimmed() + QStringLiteral(" - Linux");
    QCOMPARE(loaded->deviceName, expectedName);

    const QJsonObject sent = fake.receivedJsonBody();
    QCOMPARE(sent.value(QStringLiteral("subscriberId")).toString(), QStringLiteral("sub-1"));
    QVERIFY(!sent.contains(QStringLiteral("subscriberHash")));
    QCOMPARE(sent.value(QStringLiteral("pairingToken")).toString(), QStringLiteral("pair-tok"));
    QCOMPARE(sent.value(QStringLiteral("deviceName")).toString(), expectedName);
    // This test never calls setDeviceToken(), so m_deviceToken stays at its
    // default-constructed empty QString() -- verifies the no-endpoint-yet
    // case (e.g. pairing completes before UnifiedPushConnector has ever
    // reported an endpoint). See pairFromDeepLinkSendsDeviceTokenWhenSet
    // below for the real-endpoint case.
    QCOMPARE(sent.value(QStringLiteral("deviceToken")).toString(), QString());
}

void PairingControllerTest::pairFromDeepLinkSendsDeviceTokenWhenSet()
{
    // Task 43 regression guard: when setDeviceToken() has been called (as
    // main.cpp does whenever UnifiedPushConnector reports an endpoint,
    // including once immediately after pushConnector's construction --
    // see PairingController.h's class doc comment), pairFromParsedParams()
    // must send that value as deviceToken rather than QString(). Reverting
    // the Task 43 fix (passing QString() unconditionally instead of
    // m_deviceToken) would fail this test while leaving
    // pairFromDeepLinkHappyPathPairsAndPersists above green.
    const QByteArray body = R"({"ok":true,"synced":true,"deviceId":"dev-4","deviceSecret":"secret-4",)"
                             R"("devices":1,"deliveryMode":"pull",)"
                             R"("pullEndpoint":"http://relay.example/api/notifications/native/pull",)"
                             R"("transport":"unifiedpush"})";
    FakeRelayServer fake(httpResponse(200, "OK", body));

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));

    QNetworkAccessManager manager;
    HttpClient http(manager);
    NativeRegistrationClient client(http);
    HttpClientPinSink pinSink(http);
    NetworkExecutor executor(3000);
    DeviceRegistrationService service(client, pairingStore, settingsStore, pinSink);

    PairingController controller(service, pairingStore, settingsStore, pinSink, executor);
    controller.setDeviceToken(QStringLiteral("some-real-endpoint"));

    const QString serverBaseUrl = QStringLiteral("http://127.0.0.1:%1").arg(fake.port());
    const QString registrationUrl = serverBaseUrl + QStringLiteral("/api/notifications/native/register");

    QMap<QString, QString> params;
    params[QStringLiteral("sub")] = QStringLiteral("sub-4");
    params[QStringLiteral("srv")] = serverBaseUrl;
    params[QStringLiteral("pt")] = QStringLiteral("pair-tok-4");
    params[QStringLiteral("reg")] = registrationUrl;

    QVERIFY(controller.pairFromDeepLink(buildLink(params)));
    QCOMPARE(controller.pairingState(), QStringLiteral("confirm"));
    QVERIFY(controller.confirmPendingPair());
    QTRY_COMPARE_WITH_TIMEOUT(controller.pairingState(), QStringLiteral("paired"), 5000);

    const QJsonObject sent = fake.receivedJsonBody();
    QCOMPARE(sent.value(QStringLiteral("deviceToken")).toString(), QStringLiteral("some-real-endpoint"));
}

void PairingControllerTest::pairFromDeepLinkDerivesRegistrationUrlFromSrvWhenRegOmitted()
{
    const QByteArray body = R"({"ok":true,"synced":true,"deviceId":"dev-2","deviceSecret":"secret-2",)"
                             R"("devices":1,"deliveryMode":"pull",)"
                             R"("pullEndpoint":"http://relay.example/api/notifications/native/pull",)"
                             R"("transport":"unifiedpush"})";
    FakeRelayServer fake(httpResponse(200, "OK", body));

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));

    QNetworkAccessManager manager;
    HttpClient http(manager);
    NativeRegistrationClient client(http);
    HttpClientPinSink pinSink(http);
    NetworkExecutor executor(3000);
    DeviceRegistrationService service(client, pairingStore, settingsStore, pinSink);

    PairingController controller(service, pairingStore, settingsStore, pinSink, executor);

    // Trailing slash on srv exercises the strip-trailing-slash rule too.
    const QString serverBaseUrl = QStringLiteral("http://127.0.0.1:%1/").arg(fake.port());

    QMap<QString, QString> params;
    params[QStringLiteral("sub")] = QStringLiteral("sub-2");
    params[QStringLiteral("srv")] = serverBaseUrl;
    params[QStringLiteral("pt")] = QStringLiteral("pair-tok-2");
    // reg deliberately omitted.

    QVERIFY(controller.pairFromDeepLink(buildLink(params)));
    QCOMPARE(controller.pairingState(), QStringLiteral("confirm"));
    QVERIFY(controller.confirmPendingPair());
    QTRY_COMPARE_WITH_TIMEOUT(controller.pairingState(), QStringLiteral("paired"), 5000);

    const std::optional<DevicePairing> loaded = pairingStore.load();
    QVERIFY(loaded.has_value());
    QCOMPARE(loaded->registrationUrl,
             QStringLiteral("http://127.0.0.1:%1/api/notifications/native/register").arg(fake.port()));

    // The derived path is what the request actually hit, not just what got
    // persisted.
    QVERIFY(fake.receivedRequest().contains("POST /api/notifications/native/register HTTP/1.1"));
}

void PairingControllerTest::pairFromDeepLinkMissingRequiredParam_data()
{
    QTest::addColumn<QString>("omittedKey");
    QTest::newRow("sub missing") << QStringLiteral("sub");
    QTest::newRow("srv missing") << QStringLiteral("srv");
    QTest::newRow("pt missing") << QStringLiteral("pt");
}

void PairingControllerTest::pairFromDeepLinkMissingRequiredParam()
{
    QFETCH(QString, omittedKey);

    // Response would signal success if hit -- the test only passes if it's
    // never hit at all.
    FakeRelayServer fake(httpResponse(200, "OK", R"({"ok":true,"deviceId":"should-not-be-used"})"));

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));

    QNetworkAccessManager manager;
    HttpClient http(manager);
    NativeRegistrationClient client(http);
    HttpClientPinSink pinSink(http);
    NetworkExecutor executor(3000);
    DeviceRegistrationService service(client, pairingStore, settingsStore, pinSink);

    PairingController controller(service, pairingStore, settingsStore, pinSink, executor);

    QMap<QString, QString> params;
    params[QStringLiteral("sub")] = QStringLiteral("sub-x");
    params[QStringLiteral("srv")] = QStringLiteral("http://127.0.0.1:%1").arg(fake.port());
    params[QStringLiteral("pt")] = QStringLiteral("pair-tok-x");
    params.remove(omittedKey);

    QSignalSpy stateChangedSpy(&controller, &PairingController::pairingStateChanged);

    QVERIFY(!controller.pairFromDeepLink(buildLink(params)));

    QCOMPARE(controller.pairingState(), QStringLiteral("failed"));
    QVERIFY(!controller.pairingError().isEmpty());
    QVERIFY(!controller.isPaired());
    QCOMPARE(stateChangedSpy.count(), 1); // idle -> failed directly, no "working" in between
    QVERIFY(fake.receivedRequest().isEmpty()); // zero network calls
    QVERIFY(!pairingStore.load().has_value());
}

void PairingControllerTest::pairFromDeepLinkRejectsNonNativePairHost()
{
    // kypost://desktop-pair is explicitly out of scope per Phase 6
    // global constraint 6 -- must be treated as unrecognized, not routed.
    FakeRelayServer fake(httpResponse(200, "OK", R"({"ok":true})"));

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));

    QNetworkAccessManager manager;
    HttpClient http(manager);
    NativeRegistrationClient client(http);
    HttpClientPinSink pinSink(http);
    NetworkExecutor executor(3000);
    DeviceRegistrationService service(client, pairingStore, settingsStore, pinSink);

    PairingController controller(service, pairingStore, settingsStore, pinSink, executor);

    QUrl link;
    link.setScheme(QStringLiteral("kypost"));
    link.setHost(QStringLiteral("desktop-pair"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("sub"), QStringLiteral("sub-1"));
    query.addQueryItem(QStringLiteral("srv"), QStringLiteral("http://127.0.0.1:%1").arg(fake.port()));
    query.addQueryItem(QStringLiteral("pt"), QStringLiteral("pair-tok"));
    link.setQuery(query);

    QVERIFY(!controller.pairFromDeepLink(link));
    QCOMPARE(controller.pairingState(), QStringLiteral("failed"));
    QVERIFY(fake.receivedRequest().isEmpty());
}

void PairingControllerTest::pairFromDeepLinkRejectsPlaintextHttpServerUrl()
{
    // VibeSec regression guard: a MITM who rewrites an otherwise-legitimate
    // https:// pairing link to http:// must not be able to make the app
    // pair (and send the pairing token + real push deviceToken) in
    // cleartext -- see PairingController.cpp's parseNativePairLink.
    FakeRelayServer fake(httpResponse(200, "OK", R"({"ok":true,"deviceId":"should-not-be-used"})"));

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));

    QNetworkAccessManager manager;
    HttpClient http(manager);
    NativeRegistrationClient client(http);
    HttpClientPinSink pinSink(http);
    NetworkExecutor executor(3000);
    DeviceRegistrationService service(client, pairingStore, settingsStore, pinSink);

    PairingController controller(service, pairingStore, settingsStore, pinSink, executor);

    QMap<QString, QString> params;
    params[QStringLiteral("sub")] = QStringLiteral("sub-http");
    // A non-loopback host over plaintext http -- must be rejected outright.
    params[QStringLiteral("srv")] = QStringLiteral("http://relay.example.com");
    params[QStringLiteral("pt")] = QStringLiteral("pair-tok-http");

    QVERIFY(!controller.pairFromDeepLink(buildLink(params)));

    QCOMPARE(controller.pairingState(), QStringLiteral("failed"));
    QVERIFY(!controller.isPaired());
    QVERIFY(fake.receivedRequest().isEmpty());
    QVERIFY(!pairingStore.load().has_value());
}

void PairingControllerTest::pairFromDeepLinkAllowsPlaintextHttpForLoopbackServerUrl()
{
    // Local/self-hosted development relays (and every other test in this
    // file) legitimately use http://127.0.0.1:<port> -- the https-only rule
    // must carve out loopback, not just accept https everywhere.
    const QByteArray body = R"({"ok":true,"synced":true,"deviceId":"dev-loop","deviceSecret":"secret-loop",)"
                             R"("devices":1,"deliveryMode":"pull",)"
                             R"("pullEndpoint":"http://relay.example/api/notifications/native/pull",)"
                             R"("transport":"unifiedpush"})";
    FakeRelayServer fake(httpResponse(200, "OK", body));

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));

    QNetworkAccessManager manager;
    HttpClient http(manager);
    NativeRegistrationClient client(http);
    HttpClientPinSink pinSink(http);
    NetworkExecutor executor(3000);
    DeviceRegistrationService service(client, pairingStore, settingsStore, pinSink);

    PairingController controller(service, pairingStore, settingsStore, pinSink, executor);

    QMap<QString, QString> params;
    params[QStringLiteral("sub")] = QStringLiteral("sub-loop");
    params[QStringLiteral("srv")] = QStringLiteral("http://127.0.0.1:%1").arg(fake.port());
    params[QStringLiteral("pt")] = QStringLiteral("pair-tok-loop");

    QVERIFY(controller.pairFromDeepLink(buildLink(params)));
    QCOMPARE(controller.pairingState(), QStringLiteral("confirm"));
    QVERIFY(controller.confirmPendingPair());
    QTRY_COMPARE_WITH_TIMEOUT(controller.pairingState(), QStringLiteral("paired"), 5000);
}

void PairingControllerTest::pairFromDeepLinkRejectsRegOnDifferentOriginThanSrv()
{
    // VibeSec regression guard: `reg` used to be able to point the actual
    // registration POST (carrying the subscriberId/pairingToken/real push
    // deviceToken) at a completely different host than `srv`, the only
    // value the confirm dialog ever displays -- see
    // PairingController.cpp's parseNativePairLink.
    FakeRelayServer fake(httpResponse(200, "OK", R"({"ok":true,"deviceId":"should-not-be-used"})"));

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));

    QNetworkAccessManager manager;
    HttpClient http(manager);
    NativeRegistrationClient client(http);
    HttpClientPinSink pinSink(http);
    NetworkExecutor executor(3000);
    DeviceRegistrationService service(client, pairingStore, settingsStore, pinSink);

    PairingController controller(service, pairingStore, settingsStore, pinSink, executor);

    QMap<QString, QString> params;
    params[QStringLiteral("sub")] = QStringLiteral("sub-crossorigin");
    params[QStringLiteral("srv")] = QStringLiteral("http://127.0.0.1:%1").arg(fake.port());
    params[QStringLiteral("pt")] = QStringLiteral("pair-tok-crossorigin");
    // reg points at a different host entirely -- the confirm dialog would
    // still only ever show "127.0.0.1" from srv above.
    params[QStringLiteral("reg")] = QStringLiteral("http://attacker.example/register");

    QVERIFY(!controller.pairFromDeepLink(buildLink(params)));

    QCOMPARE(controller.pairingState(), QStringLiteral("failed"));
    QVERIFY(!controller.isPaired());
    QVERIFY(fake.receivedRequest().isEmpty());
    QVERIFY(!pairingStore.load().has_value());
}

// The `pin` parameter, end to end from the wire string the relay builds.
//
// Before this, parseNativePairLink read sub/srv/pt/reg and stopped, so a
// server that published a pin got a client that ignored it and sent the
// pairing token and push credentials inside a trust-on-first-use window
// anyway.
void PairingControllerTest::pairFromDeepLinkArmsTheServerPublishedPin()
{
    const QByteArray body = R"({"ok":true,"synced":true,"deviceId":"dev-1","deviceSecret":"fresh",)"
                             R"("devices":1,"deliveryMode":"pull","transport":"unifiedpush"})";
    FakeRelayServer fake(httpResponse(200, "OK", body));

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));

    QNetworkAccessManager manager;
    HttpClient http(manager);
    NativeRegistrationClient client(http);
    HttpClientPinSink pinSink(http);
    NetworkExecutor executor(3000);
    DeviceRegistrationService service(client, pairingStore, settingsStore, pinSink);

    PairingController controller(service, pairingStore, settingsStore, pinSink, executor);

    const QByteArray rawPin = samplePinBytes();
    const QString serverBaseUrl = QStringLiteral("http://127.0.0.1:%1").arg(fake.port());

    // Built as the relay builds it: "sha256/" prefix, and the base64
    // alphabet's +, / and = percent-encoded the way URLSearchParams emits
    // them. Going through the encoded wire form rather than QUrlQuery is the
    // point -- a pin that only survives an in-process round trip proves
    // nothing about the link a user actually clicks.
    const QString wirePin = QStringLiteral("sha256/") + QString::fromLatin1(rawPin.toBase64());
    const QString link = QStringLiteral("kypost://native-pair?sub=sub-1&pt=pair-tok&srv=%1&pin=%2")
                             .arg(QString::fromLatin1(QUrl::toPercentEncoding(serverBaseUrl)),
                                  QString::fromLatin1(QUrl::toPercentEncoding(wirePin)));

    QVERIFY(controller.pairFromDeepLink(QUrl(link)));
    QCOMPARE(controller.pairingState(), QStringLiteral("confirm"));

    QVERIFY(controller.confirmPendingPair());
    QTRY_COMPARE_WITH_TIMEOUT(controller.pairingState(), QStringLiteral("paired"), 5000);

    // Enforcing in this process, scoped to the origin the confirm dialog
    // showed...
    QCOMPARE(pinSink.pinState().spkiSha256, rawPin);
    QCOMPARE(pinSink.pinState().origin, QUrl(serverBaseUrl));
    // ...and persisted, so the next launch enforces it too. The fake relay is
    // plain http, so there was no handshake to capture a pin from: this value
    // can only have come from the link.
    const std::optional<DevicePairing> loaded = pairingStore.load();
    QVERIFY(loaded.has_value());
    QCOMPARE(loaded->certificateSpkiSha256, QString::fromLatin1(rawPin.toBase64()));
}

void PairingControllerTest::pairFromDeepLinkRefusesAMalformedPin_data()
{
    QTest::addColumn<QString>("pin");

    const QByteArray raw = samplePinBytes();
    const QString valid = QString::fromLatin1(raw.toBase64());

    QTest::newRow("truncated") << valid.left(20);
    QTest::newRow("one char short") << valid.mid(1);
    // 33 bytes rather than 32 -- decodes cleanly, wrong digest length.
    QTest::newRow("too long") << QString::fromLatin1(QByteArray(33, '\x2a').toBase64());
    QTest::newRow("valid pin of the wrong digest") << QString::fromLatin1(QByteArray(31, '\x2a').toBase64());
    QTest::newRow("not base64") << QStringLiteral("this is not a certificate pin at all!!!!!!!!");
    // The corruption the relay's URLSearchParams encoding exists to prevent:
    // a bare '+' arriving as a space. It must fail closed, not fall back to
    // trust on first use.
    QTest::newRow("plus decoded to space") << QString(valid).replace(QLatin1Char('+'), QLatin1Char(' '));
    QTest::newRow("hex instead of base64") << QString::fromLatin1(raw.toHex());
    QTest::newRow("prefix only") << QStringLiteral("sha256/");
}

// Refused, not ignored. Dropping an unreadable pin would hand an attacker who
// can corrupt one character in transit a silent downgrade back to the
// trust-on-first-use window this parameter exists to close.
void PairingControllerTest::pairFromDeepLinkRefusesAMalformedPin()
{
    QFETCH(QString, pin);

    FakeRelayServer fake(httpResponse(200, "OK", R"({"ok":true,"deviceId":"should-not-be-used"})"));

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));

    QNetworkAccessManager manager;
    HttpClient http(manager);
    NativeRegistrationClient client(http);
    HttpClientPinSink pinSink(http);
    NetworkExecutor executor(3000);
    DeviceRegistrationService service(client, pairingStore, settingsStore, pinSink);

    PairingController controller(service, pairingStore, settingsStore, pinSink, executor);

    QMap<QString, QString> params;
    params[QStringLiteral("sub")] = QStringLiteral("sub-1");
    params[QStringLiteral("srv")] = QStringLiteral("http://127.0.0.1:%1").arg(fake.port());
    params[QStringLiteral("pt")] = QStringLiteral("pair-tok");
    params[QStringLiteral("pin")] = pin;

    QVERIFY(!controller.pairFromDeepLink(buildLink(params)));
    QCOMPARE(controller.pairingState(), QStringLiteral("failed"));
    QVERIFY(!controller.isPaired());
    QVERIFY(fake.receivedRequest().isEmpty());
    QVERIFY(!pairingStore.load().has_value());
}

void PairingControllerTest::pairFromDeepLinkNotifiesFreshPendingPairEvenWhenStateLabelUnchanged()
{
    // VibeSec regression guard: setPairingState() used to dedup on
    // (state, error) alone, so a SECOND kypost://native-pair link arriving
    // while the confirm dialog was already open (same "confirm"/"" state)
    // silently swapped m_pendingPair to the new (attacker) link's params
    // without ever emitting pairingStateChanged() -- pendingPairOrigin's
    // QML binding never re-evaluated, so the dialog kept showing the FIRST
    // link's host while "Pair" would have acted on the SECOND link's data.
    FakeRelayServer fake(httpResponse(200, "OK", R"({"ok":true,"deviceId":"should-not-be-used"})"));

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));

    QNetworkAccessManager manager;
    HttpClient http(manager);
    NativeRegistrationClient client(http);
    HttpClientPinSink pinSink(http);
    NetworkExecutor executor(3000);
    DeviceRegistrationService service(client, pairingStore, settingsStore, pinSink);

    PairingController controller(service, pairingStore, settingsStore, pinSink, executor);

    QMap<QString, QString> firstParams;
    firstParams[QStringLiteral("sub")] = QStringLiteral("sub-first");
    firstParams[QStringLiteral("srv")] = QStringLiteral("http://127.0.0.1:%1").arg(fake.port());
    firstParams[QStringLiteral("pt")] = QStringLiteral("pair-tok-first");

    QVERIFY(controller.pairFromDeepLink(buildLink(firstParams)));
    QCOMPARE(controller.pairingState(), QStringLiteral("confirm"));
    QCOMPARE(controller.pendingPairOrigin(), QStringLiteral("http://127.0.0.1:%1").arg(fake.port()));

    QSignalSpy stateChangedSpy(&controller, &PairingController::pairingStateChanged);

    QMap<QString, QString> secondParams;
    secondParams[QStringLiteral("sub")] = QStringLiteral("sub-second");
    secondParams[QStringLiteral("srv")] = QStringLiteral("https://192.0.2.1"); // TEST-NET-1, deliberately unreachable
    secondParams[QStringLiteral("pt")] = QStringLiteral("pair-tok-second");

    QVERIFY(controller.pairFromDeepLink(buildLink(secondParams)));

    // Still "confirm" (same label), but pairingStateChanged MUST fire again
    // so a bound QML label re-reads pendingPairOrigin -- otherwise the UI
    // shows stale (first link's) data while the pending params underneath
    // have already moved to the second link's.
    QCOMPARE(controller.pairingState(), QStringLiteral("confirm"));
    QCOMPARE(controller.pendingPairOrigin(), QStringLiteral("https://192.0.2.1"));
    QVERIFY(stateChangedSpy.count() >= 1);
}

void PairingControllerTest::pairFromPastedLinkRejectsNonLinkTextWithNoNetworkCall()
{
    FakeRelayServer fake(httpResponse(200, "OK", R"({"ok":true})"));

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));

    QNetworkAccessManager manager;
    HttpClient http(manager);
    NativeRegistrationClient client(http);
    HttpClientPinSink pinSink(http);
    NetworkExecutor executor(3000);
    DeviceRegistrationService service(client, pairingStore, settingsStore, pinSink);

    PairingController controller(service, pairingStore, settingsStore, pinSink, executor);

    QVERIFY(!controller.pairFromPastedLink(QStringLiteral("this is not a pairing link")));
    QCOMPARE(controller.pairingState(), QStringLiteral("failed"));
    QVERIFY(fake.receivedRequest().isEmpty());
}

void PairingControllerTest::refreshFromStoreReflectsPreSeededPairingStoreAndRemovePairingClears()
{
    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);

    DevicePairing pairing;
    pairing.subscriberId = QStringLiteral("sub-seed");
    pairing.serverBaseUrl = QStringLiteral("https://relay.example.com:8443");
    pairing.registrationUrl = QStringLiteral("https://relay.example.com:8443/api/notifications/native/register");
    pairing.pairingToken = QStringLiteral("tok-seed");
    pairing.deviceId = QStringLiteral("dev-seed");
    pairing.deviceName = QStringLiteral("Seeded Device");
    pairing.deviceSecret = QStringLiteral("secret-seed");
    QVERIFY(pairingStore.save(pairing));

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));

    QNetworkAccessManager manager;
    HttpClient http(manager);
    NativeRegistrationClient client(http);
    HttpClientPinSink pinSink(http);
    NetworkExecutor executor(3000);
    DeviceRegistrationService service(client, pairingStore, settingsStore, pinSink);

    // Construction alone must reflect the pre-seeded pairing -- see
    // PairingController's constructor comment; no explicit refreshFromStore()
    // call needed here.
    PairingController controller(service, pairingStore, settingsStore, pinSink, executor);

    QVERIFY(controller.isPaired());
    QCOMPARE(controller.deviceId(), QStringLiteral("dev-seed"));
    QCOMPARE(controller.pairedServerHost(), QStringLiteral("relay.example.com"));
    // Task 39: this seed only touches PairingStore, never
    // DeviceRegistrationService::pair() -- SettingsStore's delivery fields
    // stay at their "never registered" empty default regardless of
    // isPaired, matching Settings.qml's Notifications pane "Not yet
    // registered" fallback.
    QVERIFY(controller.deliveryMode().isEmpty());
    QVERIFY(controller.transport().isEmpty());

    controller.removePairing();

    QVERIFY(!controller.isPaired());
    QVERIFY(controller.deviceId().isEmpty());
    QVERIFY(controller.pairedServerHost().isEmpty());
    QVERIFY(!pairingStore.load().has_value());
}

void PairingControllerTest::removePairingSkipsNetworkCallWhenNoDeviceSecretStored()
{
    // A pairing created before the per-device-secret migration has no
    // deviceSecret at all -- removePairing() must fall back to exactly the
    // old local-only clear, never attempting a request (whose credentials
    // would be blank/meaningless).
    FakeRelayServer fake(httpResponse(200, "OK", R"({"ok":true})"));

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);

    DevicePairing pairing;
    pairing.subscriberId = QStringLiteral("sub-nosecret");
    pairing.serverBaseUrl = QStringLiteral("http://127.0.0.1:%1").arg(fake.port());
    pairing.registrationUrl =
        QStringLiteral("http://127.0.0.1:%1/api/notifications/native/register").arg(fake.port());
    pairing.pairingToken = QStringLiteral("tok-nosecret");
    pairing.deviceId = QStringLiteral("dev-nosecret");
    pairing.deviceName = QStringLiteral("Pre-Migration Device");
    // deviceSecret deliberately left empty.
    QVERIFY(pairingStore.save(pairing));

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));

    QNetworkAccessManager manager;
    HttpClient http(manager);
    NativeRegistrationClient client(http);
    HttpClientPinSink pinSink(http);
    NetworkExecutor executor(3000);
    DeviceRegistrationService service(client, pairingStore, settingsStore, pinSink);

    PairingController controller(service, pairingStore, settingsStore, pinSink, executor);
    QVERIFY(controller.isPaired());

    controller.removePairing();

    QVERIFY(!controller.isPaired());
    QVERIFY(!pairingStore.load().has_value());
    QVERIFY(fake.receivedRequest().isEmpty());
}

void PairingControllerTest::removePairingDeregistersServerSideWhenDeviceSecretPresent()
{
    FakeRelayServer fake(httpResponse(200, "OK", R"({"ok":true})"));

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);

    DevicePairing pairing;
    pairing.subscriberId = QStringLiteral("sub-full");
    pairing.serverBaseUrl = QStringLiteral("http://127.0.0.1:%1").arg(fake.port());
    pairing.registrationUrl = QStringLiteral("http://127.0.0.1:%1/api/notifications/native/register").arg(fake.port());
    pairing.pairingToken = QStringLiteral("tok-full");
    pairing.deviceId = QStringLiteral("dev-full");
    pairing.deviceName = QStringLiteral("Fully Paired Device");
    pairing.deviceSecret = QStringLiteral("secret-full");
    QVERIFY(pairingStore.save(pairing));

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));

    QNetworkAccessManager manager;
    HttpClient http(manager);
    NativeRegistrationClient client(http);
    HttpClientPinSink pinSink(http);
    NetworkExecutor executor(3000);
    DeviceRegistrationService service(client, pairingStore, settingsStore, pinSink);

    PairingController controller(service, pairingStore, settingsStore, pinSink, executor);
    QVERIFY(controller.isPaired());

    controller.removePairing();

    // Local state clears SYNCHRONOUSLY -- that is what the user asked for
    // and it must not depend on the relay being reachable.
    QVERIFY(!controller.isPaired());
    QVERIFY(!pairingStore.load().has_value());

    // The deregister POST is dispatched and forgotten; its result was
    // already ignored, so waiting for it was never the point.
    QTRY_VERIFY_WITH_TIMEOUT(!fake.receivedRequest().isEmpty(), 5000);
    const QByteArray request = fake.receivedRequest();
    QVERIFY(request.contains("POST /api/notifications/native/deregister HTTP/1.1"));
    QVERIFY(request.contains("X-Kypost-Device-Id: dev-full"));
    QVERIFY(request.contains("X-Kypost-Device-Secret: secret-full"));
}

void PairingControllerTest::resetReturnsToIdleAfterFailure()
{
    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));

    QNetworkAccessManager manager;
    HttpClient http(manager);
    NativeRegistrationClient client(http);
    HttpClientPinSink pinSink(http);
    NetworkExecutor executor(3000);
    DeviceRegistrationService service(client, pairingStore, settingsStore, pinSink);

    PairingController controller(service, pairingStore, settingsStore, pinSink, executor);

    QVERIFY(!controller.pairFromPastedLink(QStringLiteral("not a link")));
    QCOMPARE(controller.pairingState(), QStringLiteral("failed"));

    controller.reset();

    QCOMPARE(controller.pairingState(), QStringLiteral("idle"));
    QVERIFY(controller.pairingError().isEmpty());
}

// Shared graph for the app-lock tests below. Every other test in this file
// builds this by hand on the stack; a macro keeps that shape (stack-local, no
// hidden ownership) without a fifth verbatim copy.
#define PAIRING_TEST_FIXTURE()                                                              \
    FakeRelayServer fake(httpResponse(200, "OK", R"({"ok":true,"deviceId":"dev-1"})"));      \
    QTemporaryDir secureDir;                                                                 \
    QVERIFY(secureDir.isValid());                                                            \
    SecureStoreFile secureStore(secureDir.path());                                           \
    PairingStore pairingStore(secureStore);                                                  \
    QTemporaryDir settingsDir;                                                               \
    QVERIFY(settingsDir.isValid());                                                          \
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));       \
    QNetworkAccessManager manager;                                                           \
    HttpClient http(manager);                                                                \
    NativeRegistrationClient regClient(http);                                                \
    HttpClientPinSink pinSink(http);                                                          \
    NetworkExecutor executor(3000);                                                           \
    DeviceRegistrationService service(regClient, pairingStore, settingsStore, pinSink);       \
    DeregisterClient deregisterClient(http);                                                 \
    PairingController controller(service, pairingStore, settingsStore, pinSink, executor);     \
    QMap<QString, QString> params;                                                           \
    params[QStringLiteral("sub")] = QStringLiteral("sub-lock");                               \
    params[QStringLiteral("srv")] = QStringLiteral("http://127.0.0.1:%1").arg(fake.port());   \
    params[QStringLiteral("pt")] = QStringLiteral("pair-tok-lock");

// A kypost://native-pair link can arrive from anywhere on the session bus while
// the app is locked. The confirm prompt is a QQC2 Popup, which Qt renders inside
// QQuickOverlay -- above any z-ordered sibling, including the app-lock overlay
// (z: 1000). Rather than depend on that stacking question, the controller
// refuses to enter the confirm state at all while locked, so the answer does not
// matter. Matches Android, where PushPairingActivity is a LockedActivity and
// finishes on start, discarding the intent.
void PairingControllerTest::pairFromDeepLinkRefusedWhileAppLocked()
{
    PAIRING_TEST_FIXTURE()

    controller.setAppLocked(true);
    QVERIFY(!controller.pairFromDeepLink(buildLink(params)));

    QCOMPARE(controller.pairingState(), QStringLiteral("failed"));
    // Nothing is left pending: an attacker-supplied payload must not survive
    // across the lock waiting for a later confirm.
    QVERIFY(controller.pendingPairOrigin().isEmpty());
    QVERIFY(!controller.isPaired());
    QVERIFY(fake.receivedRequest().isEmpty());
}

// The case the z-order bug actually exposes: the dialog is already open and
// visible when the lock engages. Pressing Pair must do nothing.
void PairingControllerTest::confirmPendingPairRefusedWhenAppLocksAfterConfirmStateEntered()
{
    PAIRING_TEST_FIXTURE()

    QVERIFY(controller.pairFromDeepLink(buildLink(params)));
    QCOMPARE(controller.pairingState(), QStringLiteral("confirm"));

    controller.setAppLocked(true);
    QVERIFY(!controller.confirmPendingPair());

    QVERIFY(!controller.isPaired());
    QVERIFY(fake.receivedRequest().isEmpty());
    QVERIFY(!pairingStore.load().has_value());
}

// Refusing while locked must not be sticky -- the user re-opens the link after
// unlocking, exactly as on Android.
void PairingControllerTest::pairFromDeepLinkWorksAgainAfterUnlock()
{
    PAIRING_TEST_FIXTURE()

    controller.setAppLocked(true);
    QVERIFY(!controller.pairFromDeepLink(buildLink(params)));

    controller.setAppLocked(false);
    QVERIFY(controller.pairFromDeepLink(buildLink(params)));
    QCOMPARE(controller.pairingState(), QStringLiteral("confirm"));
    QCOMPARE(controller.pendingPairOrigin(), QStringLiteral("http://127.0.0.1:%1").arg(fake.port()));
}

// pendingPairHost() returned QUrl::host() only, so "http://evil.example:8443"
// and "https://evil.example" were indistinguishable in the one confirmation the
// user ever gets. The scheme is the difference between sending the pairing token
// and the real push device token over TLS or in cleartext.
void PairingControllerTest::pendingPairOriginDisclosesSchemeAndPort()
{
    PAIRING_TEST_FIXTURE()

    QVERIFY(controller.pairFromDeepLink(buildLink(params)));

    const QString origin = controller.pendingPairOrigin();
    QVERIFY2(origin.startsWith(QStringLiteral("http://")), qPrintable(origin));
    QVERIFY2(origin.contains(QString::number(fake.port())), qPrintable(origin));
    // Loopback http is the one legitimate cleartext case, and it must still be
    // announced as insecure rather than blending in with https.
    QVERIFY(controller.pendingPairInsecure());
}


// `reg` had to share srv's ORIGIN, but sameOrigin compares scheme/host/port
// and leaves the path free -- so a link could name the user's own real mail
// server (all the confirm dialog shows) while pointing the registration POST
// at an unrelated same-origin endpoint. The relay's unauthenticated
// /api/health answers POST with a JSON object, which the registration client
// used to accept, overwriting the working credential with empty strings.
void PairingControllerTest::registrationUrlWithAForeignPathIsRejected()
{
    FakeRelayServer fake(httpResponse(200, "OK", "{}"));

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));

    QNetworkAccessManager manager;
    HttpClient http(manager);
    NativeRegistrationClient client(http);
    HttpClientPinSink pinSink(http);
    NetworkExecutor executor(3000);
    DeviceRegistrationService service(client, pairingStore, settingsStore, pinSink);

    PairingController controller(service, pairingStore, settingsStore, pinSink, executor);

    const QString srv = QStringLiteral("http://127.0.0.1:%1").arg(fake.port());
    const auto link = [&srv](const QString& reg) {
        return QStringLiteral("kypost://native-pair?sub=s&pt=t&srv=%1&reg=%2")
            .arg(QString::fromLatin1(QUrl::toPercentEncoding(srv)),
                 QString::fromLatin1(QUrl::toPercentEncoding(reg)));
    };

    // Same origin, wrong path -- the whole point of the finding.
    QVERIFY(!controller.pairFromDeepLink(link(srv + QStringLiteral("/api/health"))));
    QVERIFY(!controller.pairFromDeepLink(link(srv + QStringLiteral("/"))));
    QVERIFY(!controller.pairFromDeepLink(link(srv + QStringLiteral("/assets/app.json"))));
    // Nothing was contacted on the way to refusing.
    QVERIFY(fake.receivedRequest().isEmpty());

    // The one legitimate value still works.
    QVERIFY(controller.pairFromDeepLink(
        link(srv + QStringLiteral("/api/notifications/native/register"))));
}

// The confirm dialog is the only control between a hostile deep link and a
// pairing. QUrl::host() defaults to FullyDecoded, which turns punycode back
// into Unicode, and Qt applies no confusable-script policy (.com is on its
// IDN whitelist) -- so "mail.xn--urll-76d.com" rendered as a string that is
// glyph-identical to the real host in the monospace font it is shown in.
void PairingControllerTest::punycodeHostsAreShownInAsciiFormNotDecoded()
{
    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));

    QNetworkAccessManager manager;
    HttpClient http(manager);
    NativeRegistrationClient client(http);
    HttpClientPinSink pinSink(http);
    NetworkExecutor executor(3000);
    DeviceRegistrationService service(client, pairingStore, settingsStore, pinSink);

    PairingController controller(service, pairingStore, settingsStore, pinSink, executor);

    const QString hostile = QStringLiteral("https://mail.xn--urll-76d.com");
    QVERIFY(controller.pairFromDeepLink(
        QStringLiteral("kypost://native-pair?sub=s&pt=t&srv=%1")
            .arg(QString::fromLatin1(QUrl::toPercentEncoding(hostile)))));

    const QString shown = controller.pendingPairOrigin();
    // The ACE form, verbatim -- not the decoded lookalike.
    QCOMPARE(shown, QStringLiteral("https://mail.xn--urll-76d.com"));
    QVERIFY(shown.isEmpty() || shown.toLatin1() == shown.toUtf8());
}

// removePairing() deregisters the device server-side and destroys the
// credential. Its two siblings already refused while locked; it did not --
// and a Kirigami.OverlaySheet left open when the app locked renders inside
// QQuickOverlay, above the lock gate, still clickable.
void PairingControllerTest::removePairingIsRefusedWhileLocked()
{
    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);

    DevicePairing existing;
    existing.subscriberId = QStringLiteral("sub-1");
    existing.serverBaseUrl = QStringLiteral("https://relay.example");
    existing.deviceId = QStringLiteral("dev-1");
    existing.deviceSecret = QStringLiteral("sec-1");
    QVERIFY(pairingStore.save(existing));

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));

    QNetworkAccessManager manager;
    HttpClient http(manager);
    NativeRegistrationClient client(http);
    HttpClientPinSink pinSink(http);
    NetworkExecutor executor(3000);
    DeviceRegistrationService service(client, pairingStore, settingsStore, pinSink);

    PairingController controller(service, pairingStore, settingsStore, pinSink, executor);

    controller.setAppLocked(true);
    controller.removePairing();

    // Still paired: the credential survives an unpair attempted while locked.
    QVERIFY(pairingStore.isPaired());
    QVERIFY(controller.isPaired());

    // ...and the same call works once unlocked, so this is a gate, not a break.
    controller.setAppLocked(false);
    controller.removePairing();
    QVERIFY(!pairingStore.isPaired());
}

// The whole point of the conversion: confirming a pairing hands control
// straight back instead of sitting inside HttpClient's nested event loop for
// the length of a round trip -- during which QML input was still being
// delivered into a half-finished pairing.
void PairingControllerTest::confirmPendingPairReturnsWithoutBlocking()
{
    // A server that accepts the connection and never answers, so the
    // request runs until the executor's transfer timeout. Written out
    // rather than using PAIRING_TEST_FIXTURE(), which bakes in a 200 OK.
    FakeRelayServer fake(QByteArray{});
    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    NativeRegistrationClient regClient(http);
    HttpClientPinSink pinSink(http);
    NetworkExecutor executor(1500);
    DeviceRegistrationService service(regClient, pairingStore, settingsStore, pinSink);
    PairingController controller(service, pairingStore, settingsStore, pinSink, executor);
    QMap<QString, QString> params;
    params[QStringLiteral("sub")] = QStringLiteral("sub-async");
    params[QStringLiteral("srv")] = QStringLiteral("http://127.0.0.1:%1").arg(fake.port());
    params[QStringLiteral("pt")] = QStringLiteral("pair-tok-async");

    QVERIFY(controller.pairFromDeepLink(buildLink(params)));
    QCOMPARE(controller.pairingState(), QStringLiteral("confirm"));

    QElapsedTimer timer;
    timer.start();
    QVERIFY(controller.confirmPendingPair());
    const qint64 elapsed = timer.elapsed();

    QVERIFY2(elapsed < 200,
             qPrintable(QStringLiteral("confirmPendingPair() blocked for %1 ms").arg(elapsed)));
    QCOMPARE(controller.pairingState(), QStringLiteral("working"));

    // And it resolves rather than being lost.
    QTRY_COMPARE_WITH_TIMEOUT(controller.pairingState(), QStringLiteral("failed"), 10000);
}

// Replaces what ReentrancyGuard did here, for a different reason: there is
// no nested event loop left to be re-entered through, so this is coalescing.
// It still matters -- two overlapping registrations would race to set
// pairingState, and each one the server accepts mints a device secret and
// retires the previous one.
void PairingControllerTest::aSecondConfirmWhileOneIsInFlightIsIgnored()
{
    // A server that accepts the connection and never answers, so the
    // request runs until the executor's transfer timeout. Written out
    // rather than using PAIRING_TEST_FIXTURE(), which bakes in a 200 OK.
    FakeRelayServer fake(QByteArray{});
    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    NativeRegistrationClient regClient(http);
    HttpClientPinSink pinSink(http);
    NetworkExecutor executor(1500);
    DeviceRegistrationService service(regClient, pairingStore, settingsStore, pinSink);
    PairingController controller(service, pairingStore, settingsStore, pinSink, executor);
    QMap<QString, QString> params;
    params[QStringLiteral("sub")] = QStringLiteral("sub-async");
    params[QStringLiteral("srv")] = QStringLiteral("http://127.0.0.1:%1").arg(fake.port());
    params[QStringLiteral("pt")] = QStringLiteral("pair-tok-async");

    QVERIFY(controller.pairFromDeepLink(buildLink(params)));
    QVERIFY(controller.confirmPendingPair());
    QCOMPARE(controller.pairingState(), QStringLiteral("working"));

    // Second confirm while the first is still out: refused, and it does not
    // knock the state machine back from under the first one's completion.
    QVERIFY(!controller.confirmPendingPair());
    QCOMPARE(controller.pairingState(), QStringLiteral("working"));

    QTRY_COMPARE_WITH_TIMEOUT(controller.pairingState(), QStringLiteral("failed"), 10000);
}


// The recovery dialog asks the user to approve a certificate change. It can
// only do that honestly if it names BOTH keys, in the form the user can
// compare against their own server -- colon-separated uppercase hex of the
// SPKI SHA-256, what `openssl pkey -pubin -outform der | sha256sum` prints.
void PairingControllerTest::certificateMismatchCarriesBothFingerprintsInComparableForm()
{
    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));

    QNetworkAccessManager manager;
    HttpClient http(manager);
    NativeRegistrationClient client(http);
    HttpClientPinSink pinSink(http);
    NetworkExecutor executor(3000);
    DeviceRegistrationService service(client, pairingStore, settingsStore, pinSink);
    PairingController controller(service, pairingStore, settingsStore, pinSink, executor);

    // Nothing pinned, nothing observed: both empty rather than a formatted
    // hash of nothing. SHA256("") is a fixed public constant and rendering it
    // as "the expected fingerprint" would be a fabricated security fact.
    QVERIFY(controller.expectedCertificateFingerprint().isEmpty());
    QVERIFY(controller.observedCertificateFingerprint().isEmpty());

    const QByteArray pinned(32, '\x01');
    const QByteArray presented(32, '\xAB');
    pinSink.setPin(pinned, QUrl(QStringLiteral("https://relay.example.com")));

    QSignalSpy mismatchSpy(&controller, &PairingController::certificateMismatchChanged);
    controller.setCertificateMismatch(true, presented);

    QVERIFY(controller.certificateMismatch());
    QCOMPARE(mismatchSpy.count(), 1);
    QCOMPARE(controller.expectedCertificateFingerprint(),
             QStringLiteral("01:01:01:01:01:01:01:01:01:01:01:01:01:01:01:01:"
                             "01:01:01:01:01:01:01:01:01:01:01:01:01:01:01:01"));
    QCOMPARE(controller.observedCertificateFingerprint(),
             QStringLiteral("AB:AB:AB:AB:AB:AB:AB:AB:AB:AB:AB:AB:AB:AB:AB:AB:"
                             "AB:AB:AB:AB:AB:AB:AB:AB:AB:AB:AB:AB:AB:AB:AB:AB"));

    // Clearing must drop the observed key too. A fingerprint left over from a
    // previous mismatch, shown against a later one, is worse than none.
    controller.setCertificateMismatch(false);
    QVERIFY(!controller.certificateMismatch());
    QVERIFY(controller.observedCertificateFingerprint().isEmpty());
}

// reconnectToServer() rotates the device credential and re-anchors the TLS
// trust anchor, so it carries the same lock guard removePairing() does. A
// QQC2 Popup renders inside QQuickOverlay, which Qt stacks above the app-lock
// overlay -- a Settings sheet left open when the app locks stays visible AND
// clickable over the PIN screen. QML z-order is not a security boundary.
void PairingControllerTest::reconnectToServerIsRefusedWhileAppLocked()
{
    FakeRelayServer fake(httpResponse(200, "OK", R"({"ok":true,"deviceId":"dev-new"})"));

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);

    DevicePairing pairing;
    pairing.subscriberId = QStringLiteral("sub-1");
    pairing.serverBaseUrl = QStringLiteral("http://127.0.0.1:%1").arg(fake.port());
    pairing.registrationUrl =
        QStringLiteral("http://127.0.0.1:%1/api/notifications/native/register").arg(fake.port());
    pairing.pairingToken = QStringLiteral("tok-1");
    pairing.deviceId = QStringLiteral("dev-1");
    pairing.deviceSecret = QStringLiteral("secret-1");
    QVERIFY(pairingStore.save(pairing));

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));

    QNetworkAccessManager manager;
    HttpClient http(manager);
    NativeRegistrationClient client(http);
    HttpClientPinSink pinSink(http);
    NetworkExecutor executor(3000);
    DeviceRegistrationService service(client, pairingStore, settingsStore, pinSink);
    PairingController controller(service, pairingStore, settingsStore, pinSink, executor);

    controller.setAppLocked(true);
    controller.reconnectToServer();

    QVERIFY2(fake.receivedRequest().isEmpty(),
             "a locked app must not be able to re-register from a popup over the lock screen");
}

// The whole point of this action: recover trust WITHOUT the destructive path.
// removePairing() deregisters the device server-side and forces the user to
// obtain a fresh pairing link; this must do neither, and must leave the
// cached pairing identity intact.
void PairingControllerTest::reconnectToServerKeepsThePairingAndDoesNotDeregister()
{
    FakeRelayServer fake(httpResponse(
        200, "OK", R"({"ok":true,"deviceId":"dev-1","deviceSecret":"secret-rotated","transport":"unifiedpush"})"));

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);

    DevicePairing pairing;
    pairing.subscriberId = QStringLiteral("sub-1");
    pairing.serverBaseUrl = QStringLiteral("http://127.0.0.1:%1").arg(fake.port());
    pairing.registrationUrl =
        QStringLiteral("http://127.0.0.1:%1/api/notifications/native/register").arg(fake.port());
    pairing.pairingToken = QStringLiteral("tok-1");
    pairing.deviceId = QStringLiteral("dev-1");
    pairing.deviceSecret = QStringLiteral("secret-original");
    QVERIFY(pairingStore.save(pairing));

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));

    QNetworkAccessManager manager;
    HttpClient http(manager);
    NativeRegistrationClient client(http);
    HttpClientPinSink pinSink(http);
    NetworkExecutor executor(3000);
    DeviceRegistrationService service(client, pairingStore, settingsStore, pinSink);
    PairingController controller(service, pairingStore, settingsStore, pinSink, executor);

    controller.setCertificateMismatch(true, QByteArray(32, '\xAB'));
    QVERIFY(controller.certificateMismatch());
    QSignalSpy pairedFromLinkSpy(&controller, &PairingController::pairedFromLink);

    controller.reconnectToServer();
    QTRY_VERIFY_WITH_TIMEOUT(!controller.pairingState().isEmpty(), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(controller.pairingState(), QStringLiteral("paired"), 5000);

    // The request that went out is a REGISTRATION, not a deregistration.
    const QByteArray request = fake.receivedRequest();
    QVERIFY(request.contains("/api/notifications/native/register"));
    QVERIFY2(!request.contains("deregister"),
             "reconnect must not deregister the device -- that is removePairing()'s job");

    // Still paired, same subscriber and server. The secret rotates because
    // every successful register mints a new one server-side.
    QVERIFY(controller.isPaired());
    const std::optional<DevicePairing> after = pairingStore.load();
    QVERIFY(after.has_value());
    QCOMPARE(after->subscriberId, QStringLiteral("sub-1"));
    QCOMPARE(after->deviceSecret, QStringLiteral("secret-rotated"));

    // And the banner is gone: the certificate that just served the
    // registration is the pinned one now.
    QVERIFY(!controller.certificateMismatch());
    // A repair, not a pairing: no app-lock offer follows it.
    QCOMPARE(pairedFromLinkSpy.count(), 0);
}


namespace {

// Builds a controller wired to recording replacement handlers, with `pairing`
// pre-seeded as the account already on the device.
// Reads FAIL here; they do not answer "absent". That difference is the whole
// point -- see PairingStore::loadChecked().
class UnreachableSecureStore : public SecureStore
{
public:
    ReadResult read(const QString&) const override { return ReadResult{ ReadStatus::Failed, QString() }; }
    bool set(const QString&, const QString&) override { return false; }
    std::optional<QString> get(const QString&) const override { return std::nullopt; }
    bool remove(const QString&) override { return false; }
    bool contains(const QString&) const override { return false; }
};

struct ReplacementFixture
{
    FakeRelayServer fake;
    QTemporaryDir secureDir;
    QTemporaryDir settingsDir;
    std::unique_ptr<SecureStore> secureStore;
    std::unique_ptr<PairingStore> pairingStore;
    std::unique_ptr<SettingsStore> settingsStore;
    QNetworkAccessManager manager;
    std::unique_ptr<HttpClient> http;
    std::unique_ptr<NativeRegistrationClient> client;
    std::unique_ptr<HttpClientPinSink> pinSink;
    std::unique_ptr<NetworkExecutor> executor;
    std::unique_ptr<DeviceRegistrationService> service;
    std::unique_ptr<PairingController> controller;

    int purgeCalls = 0;
    int escalateCalls = 0;
    bool purgeSucceeds = true;
    // Swaps SecureStoreFile for a store whose reads FAIL rather than answer
    // "absent" -- a locked wallet, no Secret Service, a D-Bus timeout.
    bool storeUnreadable = false;

    explicit ReplacementFixture(QByteArray response)
        : fake(std::move(response))
    {
    }

    bool build(const QString& seededSubscriberId)
    {
        if (!secureDir.isValid() || !settingsDir.isValid())
            return false;
        if (storeUnreadable)
            secureStore = std::make_unique<UnreachableSecureStore>();
        else
            secureStore = std::make_unique<SecureStoreFile>(secureDir.path());
        pairingStore = std::make_unique<PairingStore>(*secureStore);

        if (!seededSubscriberId.isEmpty()) {
            DevicePairing seeded;
            seeded.subscriberId = seededSubscriberId;
            seeded.serverBaseUrl = QStringLiteral("http://127.0.0.1:%1").arg(fake.port());
            seeded.registrationUrl =
                QStringLiteral("http://127.0.0.1:%1/api/notifications/native/register").arg(fake.port());
            seeded.pairingToken = QStringLiteral("tok-old");
            seeded.deviceId = QStringLiteral("dev-old");
            seeded.deviceSecret = QStringLiteral("secret-old");
            if (!pairingStore->save(seeded))
                return false;
        }

        settingsStore = std::make_unique<SettingsStore>(settingsDir.filePath(QStringLiteral("settings.ini")));
        http = std::make_unique<HttpClient>(manager);
        client = std::make_unique<NativeRegistrationClient>(*http);
        pinSink = std::make_unique<HttpClientPinSink>(*http);
        executor = std::make_unique<NetworkExecutor>(3000);
        service = std::make_unique<DeviceRegistrationService>(*client, *pairingStore, *settingsStore, *pinSink);
        controller = std::make_unique<PairingController>(*service, *pairingStore, *settingsStore, *pinSink,
                                                          *executor);
        controller->setAccountReplacementHandlers([this]() { ++purgeCalls; return purgeSucceeds; },
                                                   [this]() { ++escalateCalls; });
        return true;
    }

    QString linkFor(const QString& subscriberId) const
    {
        return QStringLiteral("kypost://native-pair?sub=%1&srv=http://127.0.0.1:%2&pt=tok-new")
            .arg(subscriberId)
            .arg(fake.port());
    }
};

const char* const kRegisterOk =
    R"({"ok":true,"deviceId":"dev-new","deviceSecret":"secret-new","transport":"unifiedpush"})";

} // namespace

// THE LEAK THIS EXISTS TO CLOSE. No table in this schema carries a subscriber
// column, so cached mail, contacts, groups, photos and cursors are per-device.
// Pairing a different account left the previous one's mail in the inbox,
// readable by whoever just paired -- hand over a laptop, let the next person
// pair, and they are reading the previous owner's mail.
namespace {

// docs/device-approval.md's 202, with a poll interval of 1 s so the tests
// run in seconds. expiresAt is far enough out not to matter unless a test
// sets its own.
QByteArray pendingRegistration(int port, const QString& expiresAt = QString(),
                               const QString& scheme = QStringLiteral("http"))
{
    const QString expires = expiresAt.isEmpty()
        ? QDateTime::currentDateTimeUtc().addSecs(300).toString(Qt::ISODate)
        : expiresAt;
    return httpResponse(202, "Accepted",
                        QStringLiteral(R"({"ok":false,"synced":false,"status":"pending",)"
                                       R"("error":"approve this device on the web page that showed the pairing code",)"
                                       R"("deviceId":"dev-pending","deviceSecret":"pending-secret",)"
                                       R"("approvalCode":"048213","expiresAt":"%1",)"
                                       R"("statusEndpoint":"%3://127.0.0.1:%2/api/notifications/native/pairing-status",)"
                                       R"("pollIntervalSeconds":1})")
                            .arg(expires)
                            .arg(port)
                            .arg(scheme)
                            .toUtf8());
}

const QByteArray kStillPending = httpResponse(
    202, "Accepted", R"({"status":"pending","approvalCode":"048213","pollIntervalSeconds":1})");

int statusRequests(const QList<QByteArray>& requests)
{
    int n = 0;
    for (const QByteArray& request : requests)
        n += request.startsWith("GET /api/notifications/native/pairing-status") ? 1 : 0;
    return n;
}

int statusRequests(const FakeRelayServer& fake)
{
    return statusRequests(fake.receivedRequests());
}

QString httpsLinkFor(quint16 port)
{
    return QStringLiteral("kypost://native-pair?sub=sub-new&srv=https://127.0.0.1:%1&pt=tok-new").arg(port);
}

// The paired account's pin, on another origin: the approval wait must leave
// it in force rather than suspend it.
const QUrl kPairedOrigin(QStringLiteral("https://paired.example"));
const QByteArray kPairedPin(32, '\x02');

} // namespace

// 202 then 200: the code shows while the server waits, nothing is stored
// meanwhile, and the approval finishes the pairing exactly as a 200 from
// register does -- under the deviceId the approval names.
void PairingControllerTest::approvalThenApprovedCompletesPairing()
{
    ReplacementFixture f(kStillPending);
    f.fake.setResponseForPath("/native/register", pendingRegistration(f.fake.port()));
    QVERIFY(f.build(QString()));
    QSignalSpy pairedFromLinkSpy(f.controller.get(), &PairingController::pairedFromLink);

    QVERIFY(f.controller->pairFromDeepLink(QUrl(f.linkFor(QStringLiteral("sub-new")))));
    f.controller->confirmPendingPair();
    QTRY_COMPARE_WITH_TIMEOUT(f.controller->pairingState(), QStringLiteral("approval"), 5000);
    QCOMPARE(f.controller->approvalCode(), QStringLiteral("048213"));
    QCOMPARE(f.controller->approvalServer(), QStringLiteral("http://127.0.0.1:%1").arg(f.fake.port()));
    QVERIFY(!f.controller->isPaired());
    QVERIFY2(!f.pairingStore->load().has_value(), "a pending device is not a pairing");
    QVERIFY(!f.pairingStore->hasPushKeys());

    QTRY_VERIFY_WITH_TIMEOUT(statusRequests(f.fake) >= 1, 5000);
    QCOMPARE(f.controller->pairingState(), QStringLiteral("approval"));
    const QByteArray poll = f.fake.receivedRequests().last();
    QVERIFY(poll.contains("X-Kypost-Device-Id: dev-pending"));
    QVERIFY(poll.contains("X-Kypost-Device-Secret: pending-secret"));

    f.fake.setResponse(httpResponse(200, "OK",
                                    R"({"ok":true,"synced":true,"status":"approved","deviceId":"dev-merged",)"
                                    R"("deliveryMode":"pull","transport":"unifiedpush"})"));
    QTRY_COMPARE_WITH_TIMEOUT(f.controller->pairingState(), QStringLiteral("paired"), 8000);
    QVERIFY(f.controller->isPaired());
    QCOMPARE(f.controller->deviceId(), QStringLiteral("dev-merged"));
    QCOMPARE(pairedFromLinkSpy.count(), 1);
    QVERIFY(f.controller->approvalCode().isEmpty());

    const std::optional<DevicePairing> stored = f.pairingStore->load();
    QVERIFY(stored.has_value());
    QCOMPARE(stored->subscriberId, QStringLiteral("sub-new"));
    QCOMPARE(stored->deviceId, QStringLiteral("dev-merged"));
    QCOMPARE(stored->deviceSecret, QStringLiteral("pending-secret"));
    QVERIFY(f.pairingStore->hasPushKeys());
    QCOMPARE(f.settingsStore->deliveryMode(), QStringLiteral("pull"));

    // Polling stopped with the wait.
    const int polls = statusRequests(f.fake);
    QTest::qWait(1500);
    QCOMPARE(statusRequests(f.fake), polls);
}

// 410 (denied, expired or unknown) goes back to the start with the reason,
// and leaves the account that was already here exactly as it was.
void PairingControllerTest::approvalDeniedReturnsToTheStart()
{
    ReplacementFixture f(httpResponse(410, "Gone",
                                      R"({"status":"rejected","error":"this pairing was denied or has expired; )"
                                      R"(scan a new pairing code"})"));
    f.fake.setResponseForPath("/native/register", pendingRegistration(f.fake.port()));
    QVERIFY(f.build(QStringLiteral("sub-previous")));

    QVERIFY(f.controller->pairFromDeepLink(QUrl(f.linkFor(QStringLiteral("sub-new")))));
    f.controller->confirmPendingPair();
    QTRY_COMPARE_WITH_TIMEOUT(f.controller->pairingState(), QStringLiteral("approval"), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(f.controller->pairingState(), QStringLiteral("idle"), 5000);

    QCOMPARE(f.controller->pairingError(), QStringLiteral("Not approved: denied or timed out. Scan a new code."));
    QVERIFY(f.controller->approvalCode().isEmpty());
    QCOMPARE(f.purgeCalls, 0);
    const std::optional<DevicePairing> after = f.pairingStore->load();
    QVERIFY(after.has_value());
    QCOMPARE(after->subscriberId, QStringLiteral("sub-previous"));
    QCOMPARE(after->deviceSecret, QStringLiteral("secret-old"));

    // A new link is accepted again.
    QVERIFY(f.controller->pairFromDeepLink(QUrl(f.linkFor(QStringLiteral("sub-new")))));
    QCOMPARE(f.controller->pairingState(), QStringLiteral("confirm"));
}

// Cancel stops the polling at once and nothing of the pending device
// survives: the next poll would have answered "approved".
void PairingControllerTest::cancelApprovalStopsPollingAndWipesTheCredential()
{
    ReplacementFixture f(httpResponse(200, "OK",
                                      R"({"ok":true,"synced":true,"status":"approved","deviceId":"dev-pending",)"
                                      R"("deliveryMode":"pull","transport":"unifiedpush"})"));
    f.fake.setResponseForPath("/native/register", pendingRegistration(f.fake.port()));
    QVERIFY(f.build(QString()));

    QVERIFY(f.controller->pairFromDeepLink(QUrl(f.linkFor(QStringLiteral("sub-new")))));
    f.controller->confirmPendingPair();
    QTRY_COMPARE_WITH_TIMEOUT(f.controller->pairingState(), QStringLiteral("approval"), 5000);

    // Before the first poll (due in 1 s), whose answer would be an approval.
    f.controller->cancelApproval();
    QCOMPARE(f.controller->pairingState(), QStringLiteral("idle"));
    QVERIFY(f.controller->pairingError().isEmpty());
    QVERIFY(f.controller->approvalCode().isEmpty());
    QVERIFY(f.controller->approvalServer().isEmpty());

    QTest::qWait(2500);
    QCOMPARE(statusRequests(f.fake), 0);
    QCOMPARE(f.controller->pairingState(), QStringLiteral("idle"));
    QVERIFY(!f.controller->isPaired());
    QVERIFY(!f.pairingStore->load().has_value());
    QVERIFY(!f.pairingStore->hasPushKeys());
}

// Cancel while a poll is out, then pair again before that poll times out:
// the new wait must still poll, and the stale answer must not touch it.
void PairingControllerTest::aNewWaitStillPollsWhileACancelledPollIsOut()
{
    ReplacementFixture f(kStillPending);
    f.fake.setResponseForPath("/native/register", pendingRegistration(f.fake.port()));
    f.fake.holdNextForPath("/native/pairing-status");
    QVERIFY(f.build(QString()));

    QVERIFY(f.controller->pairFromDeepLink(QUrl(f.linkFor(QStringLiteral("sub-new")))));
    f.controller->confirmPendingPair();
    QTRY_COMPARE_WITH_TIMEOUT(f.controller->pairingState(), QStringLiteral("approval"), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(statusRequests(f.fake), 1, 5000); // held: no answer until the 3 s timeout
    f.controller->cancelApproval();

    f.fake.setResponse(httpResponse(200, "OK",
                                    R"({"ok":true,"synced":true,"status":"approved","deviceId":"dev-pending",)"
                                    R"("deliveryMode":"pull","transport":"unifiedpush"})"));
    QVERIFY(f.controller->pairFromDeepLink(QUrl(f.linkFor(QStringLiteral("sub-new")))));
    f.controller->confirmPendingPair();
    QTRY_COMPARE_WITH_TIMEOUT(f.controller->pairingState(), QStringLiteral("approval"), 5000);
    // The new wait's first poll is due at 1 s, while the held one is still out.
    QTRY_COMPARE_WITH_TIMEOUT(f.controller->pairingState(), QStringLiteral("paired"), 2500);
    QVERIFY(f.controller->isPaired());

    // The held poll is still out; receivers must outlive shutdown() (NetworkExecutor.h).
    f.executor->shutdown();
}

// The server forgets a pending device at expiresAt; the app stops there too
// and says so, rather than polling a device that no longer exists.
void PairingControllerTest::approvalExpiresAtTheServersDeadline()
{
    ReplacementFixture f(kStillPending);
    QVERIFY(f.build(QString()));
    f.fake.setResponseForPath(
        "/native/register",
        pendingRegistration(f.fake.port(), QDateTime::currentDateTimeUtc().addSecs(2).toString(Qt::ISODate)));

    QVERIFY(f.controller->pairFromDeepLink(QUrl(f.linkFor(QStringLiteral("sub-new")))));
    f.controller->confirmPendingPair();
    QTRY_COMPARE_WITH_TIMEOUT(f.controller->pairingState(), QStringLiteral("approval"), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(f.controller->pairingState(), QStringLiteral("idle"), 8000);
    QCOMPARE(f.controller->pairingError(), QStringLiteral("Not approved: denied or timed out. Scan a new code."));
    QVERIFY(f.controller->approvalCode().isEmpty());
    QVERIFY(!f.pairingStore->load().has_value());
    const int polls = statusRequests(f.fake);
    QVERIFY(polls >= 1);
    QTest::qWait(1500);
    QCOMPARE(statusRequests(f.fake), polls);
}

// THE INTERCEPTION THIS CLOSES. A link without a pin registers trust on first
// use, and the polls then run on fresh connections. Checked only against the
// system CAs, a poll would accept any locally trusted issuer on the same
// origin and hand it the pending secret -- the credential the device keeps
// once approved. Each poll is pinned to the key the registration saw instead:
// a server under another trusted issuer is refused at the handshake, before
// the credential is written, and the wait ends with nothing kept. The paired
// account's pin stays in force throughout, and its mismatch banner is not
// raised for a key it never pinned.
void PairingControllerTest::approvalPollToAnotherTrustedIssuerIsRefusedBeforeTheCredentialIsSent()
{
    if (!QSslSocket::supportsSsl())
        QSKIP("no TLS backend available in this build");
    const TestTls::SslDefaultsGuard sslDefaults;
    TestTls::trustRoots({ TestTls::certOf(TestTls::kTrustedRootPem), TestTls::certOf(TestTls::kTrustedRootBPem) });
    TestTls::TlsRelayServer relay(kStillPending);
    QVERIFY(relay.isListening());
    relay.setResponseForPath("/native/register",
                             pendingRegistration(relay.serverPort(), QString(), QStringLiteral("https")));

    ReplacementFixture f(kStillPending);
    QVERIFY(f.build(QString()));
    f.pinSink->setPin(kPairedPin, kPairedOrigin);

    QVERIFY(f.controller->pairFromDeepLink(QUrl(httpsLinkFor(relay.serverPort()))));
    f.controller->confirmPendingPair();
    QTRY_COMPARE_WITH_TIMEOUT(f.controller->pairingState(), QStringLiteral("approval"), 5000);
    QCOMPARE(f.pinSink->pinState().spkiSha256, kPairedPin);
    QCOMPARE(f.pinSink->pinState().origin, kPairedOrigin);

    // Under the registration's issuer a poll goes through, credential and all.
    QTRY_VERIFY_WITH_TIMEOUT(statusRequests(relay.receivedRequests()) >= 1, 5000);
    QVERIFY(relay.receivedRequests().last().contains("X-Kypost-Device-Secret: pending-secret"));

    relay.serveChainB();
    const int served = relay.receivedRequests().size();
    QTRY_COMPARE_WITH_TIMEOUT(f.controller->pairingState(), QStringLiteral("failed"), 8000);
    QCOMPARE(f.controller->pairingError(),
             QStringLiteral("The server's certificate changed while waiting for approval, so pairing was "
                            "stopped. Scan a new code."));
    QCOMPARE(relay.receivedRequests().size(), served);
    for (const QByteArray& request : relay.receivedRequests().mid(served))
        QVERIFY(!request.contains("X-Kypost-Device-Secret"));
    QVERIFY(f.controller->approvalCode().isEmpty());
    QVERIFY(!f.pairingStore->load().has_value());
    QVERIFY(!f.controller->certificateMismatch());
    QCOMPARE(f.pinSink->pinState().spkiSha256, kPairedPin);

    // The wait is over: no further polls.
    QTest::qWait(1500);
    QCOMPARE(relay.receivedRequests().size(), served);
}

// The other half: the same server, still under the registration's issuer,
// approves, and the pairing is stored and pinned to that issuer's key.
void PairingControllerTest::approvalOverTlsCompletesPinnedToTheRegistrationsIssuer()
{
    if (!QSslSocket::supportsSsl())
        QSKIP("no TLS backend available in this build");
    const TestTls::SslDefaultsGuard sslDefaults;
    TestTls::trustRoots({ TestTls::certOf(TestTls::kTrustedRootPem), TestTls::certOf(TestTls::kTrustedRootBPem) });
    TestTls::TlsRelayServer relay(kStillPending);
    QVERIFY(relay.isListening());
    relay.setResponseForPath("/native/register",
                             pendingRegistration(relay.serverPort(), QString(), QStringLiteral("https")));

    ReplacementFixture f(kStillPending);
    QVERIFY(f.build(QString()));
    f.pinSink->setPin(kPairedPin, kPairedOrigin);

    QVERIFY(f.controller->pairFromDeepLink(QUrl(httpsLinkFor(relay.serverPort()))));
    f.controller->confirmPendingPair();
    QTRY_COMPARE_WITH_TIMEOUT(f.controller->pairingState(), QStringLiteral("approval"), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(statusRequests(relay.receivedRequests()) >= 1, 5000);

    relay.setResponse(httpResponse(200, "OK",
                                   R"({"ok":true,"synced":true,"status":"approved","deviceId":"dev-pending",)"
                                   R"("deliveryMode":"pull","transport":"unifiedpush"})"));
    QTRY_COMPARE_WITH_TIMEOUT(f.controller->pairingState(), QStringLiteral("paired"), 8000);

    const QByteArray pinA = HttpClient::pinnedSpkiFromChain(
        { TestTls::certOf(TestTls::kServedLeafPem), TestTls::certOf(TestTls::kServedIssuerPem) });
    const std::optional<DevicePairing> stored = f.pairingStore->load();
    QVERIFY(stored.has_value());
    QCOMPARE(stored->deviceSecret, QStringLiteral("pending-secret"));
    QCOMPARE(stored->certificateSpkiSha256, QString::fromLatin1(pinA.toBase64()));
    QCOMPARE(f.pinSink->pinState().spkiSha256, pinA);
}

// A token with the signed `direct` claim gets 200 from register, now with
// "status":"approved". That is today's path: paired at once, no wait, no poll.
void PairingControllerTest::aDirectRegistrationPairsAtOnceWithoutPolling()
{
    ReplacementFixture f(httpResponse(200, "OK",
                                      R"({"ok":true,"synced":true,"status":"approved","deviceId":"dev-direct",)"
                                      R"("deviceSecret":"direct-secret","devices":1,"deliveryMode":"push",)"
                                      R"("transport":"unifiedpush"})"));
    QVERIFY(f.build(QString()));
    QSignalSpy pairedFromLinkSpy(f.controller.get(), &PairingController::pairedFromLink);
    QStringList states;
    connect(f.controller.get(), &PairingController::pairingStateChanged, f.controller.get(),
            [&]() { states << f.controller->pairingState(); });

    QVERIFY(f.controller->pairFromDeepLink(QUrl(f.linkFor(QStringLiteral("sub-new")))));
    f.controller->confirmPendingPair();
    QTRY_COMPARE_WITH_TIMEOUT(f.controller->pairingState(), QStringLiteral("paired"), 5000);

    QCOMPARE(states, (QStringList { QStringLiteral("confirm"), QStringLiteral("working"), QStringLiteral("paired") }));
    QCOMPARE(f.controller->deviceId(), QStringLiteral("dev-direct"));
    QCOMPARE(pairedFromLinkSpy.count(), 1);
    QCOMPARE(f.pairingStore->load()->deviceSecret, QStringLiteral("direct-secret"));
    QTest::qWait(1500);
    QCOMPARE(statusRequests(f.fake), 0);
    QCOMPARE(f.fake.connectionCount(), 1);
}

void PairingControllerTest::pairingADifferentAccountPurgesThePreviousAccountsCachedData()
{
    ReplacementFixture f(httpResponse(200, "OK", kRegisterOk));
    QVERIFY(f.build(QStringLiteral("sub-previous")));

    QVERIFY(f.controller->pairFromDeepLink(QUrl(f.linkFor(QStringLiteral("sub-new")))));
    f.controller->confirmPendingPair();
    QTRY_COMPARE_WITH_TIMEOUT(f.controller->pairingState(), QStringLiteral("paired"), 5000);

    QCOMPARE(f.purgeCalls, 1);
    QCOMPARE(f.escalateCalls, 0);
}

// Re-pairing the SAME account is not a replacement, and treating it as one
// would delete the user's mail every time they re-paired to fix push.
void PairingControllerTest::pairingTheSameAccountAgainPurgesNothing()
{
    ReplacementFixture f(httpResponse(200, "OK", kRegisterOk));
    QVERIFY(f.build(QStringLiteral("sub-same")));

    QVERIFY(f.controller->pairFromDeepLink(QUrl(f.linkFor(QStringLiteral("sub-same")))));
    f.controller->confirmPendingPair();
    QTRY_COMPARE_WITH_TIMEOUT(f.controller->pairingState(), QStringLiteral("paired"), 5000);

    QCOMPARE(f.purgeCalls, 0);
    QCOMPARE(f.escalateCalls, 0);
}

// F1: "Remove pairing" keeps the cache and empties the pairing store, and the
// next pairing used to read that as a first pairing: the previous account's
// mail stayed readable and its queued contact edits went to the new account.
// The unpair records whose cache it is (owner decision Q3: the purge then
// keeps contacts for the user to decide about).
void PairingControllerTest::unpairingThenPairingAnotherAccountStillPurges()
{
    ReplacementFixture f(httpResponse(200, "OK", kRegisterOk));
    QVERIFY(f.build(QStringLiteral("sub-previous")));
    f.controller->removePairing();
    QVERIFY(!f.pairingStore->load().has_value());
    QCOMPARE(f.settingsStore->unpairedCacheOwner()->first, QStringLiteral("sub-previous"));

    QVERIFY(f.controller->pairFromDeepLink(QUrl(f.linkFor(QStringLiteral("sub-new")))));
    f.controller->confirmPendingPair();
    QTRY_COMPARE_WITH_TIMEOUT(f.controller->pairingState(), QStringLiteral("paired"), 5000);
    QCOMPARE(f.purgeCalls, 1);
    QCOMPARE(f.escalateCalls, 0);
}

void PairingControllerTest::unpairingThenPairingTheSameAccountPurgesNothing()
{
    ReplacementFixture f(httpResponse(200, "OK", kRegisterOk));
    QVERIFY(f.build(QStringLiteral("sub-same")));
    f.controller->removePairing();

    QVERIFY(f.controller->pairFromDeepLink(QUrl(f.linkFor(QStringLiteral("sub-same")))));
    f.controller->confirmPendingPair();
    QTRY_COMPARE_WITH_TIMEOUT(f.controller->pairingState(), QStringLiteral("paired"), 5000);
    QCOMPARE(f.purgeCalls, 0);
    QVERIFY(!f.settingsStore->unpairedCacheOwner().has_value());
}

// DECISIONS.md: a confirmed link that would re-pin the same account to a
// different key is never silent. Keyed on the link's own pin, not on whether a
// request has already failed this session.
void PairingControllerTest::aLinkThatRepinsThisAccountSaysSo()
{
    ReplacementFixture f(httpResponse(200, "OK", kRegisterOk));
    QVERIFY(f.build(QStringLiteral("sub-same")));
    DevicePairing held = *f.pairingStore->load();
    held.certificateSpkiSha256 = QString::fromLatin1(QByteArray(32, 'a').toBase64());
    QVERIFY(f.pairingStore->save(held));
    const QString pin = QString::fromLatin1(QByteArray(32, 'b').toBase64());

    QVERIFY(f.controller->pairFromDeepLink(QUrl(f.linkFor(QStringLiteral("sub-same")) + QStringLiteral("&pin=sha256/") + pin)));
    QVERIFY(f.controller->pendingPairRepins());
    f.controller->cancelPendingPair();

    // The pin it already holds: no re-pin, nothing to say.
    QVERIFY(f.controller->pairFromDeepLink(QUrl(f.linkFor(QStringLiteral("sub-same")) + QStringLiteral("&pin=sha256/")
                                               + held.certificateSpkiSha256)));
    QVERIFY(!f.controller->pendingPairRepins());
    f.controller->cancelPendingPair();

    // Another account is a replacement, not a re-pin.
    QVERIFY(f.controller->pairFromDeepLink(QUrl(f.linkFor(QStringLiteral("sub-other")) + QStringLiteral("&pin=sha256/") + pin)));
    QVERIFY(!f.controller->pendingPairRepins());
}

// ORDER IS THE DESIGN. Purging first would mean a replacement that failed --
// offline, a rejected token, a locked keyring -- had already deleted the mail
// and contacts of the account that was working a moment earlier. Nothing is
// destroyed until the replacement is proven.
void PairingControllerTest::aFailedRegistrationDestroysNothing()
{
    ReplacementFixture f(httpResponse(401, "Unauthorized", R"({"error":"invalid or expired pairing token"})"));
    QVERIFY(f.build(QStringLiteral("sub-previous")));

    QVERIFY(f.controller->pairFromDeepLink(QUrl(f.linkFor(QStringLiteral("sub-new")))));
    f.controller->confirmPendingPair();
    QTRY_COMPARE_WITH_TIMEOUT(f.controller->pairingState(), QStringLiteral("failed"), 5000);

    QVERIFY2(f.purgeCalls == 0, "a failed replacement must not have deleted the working account's data");
    QCOMPARE(f.escalateCalls, 0);

    // And the account that was working is still the one on the device.
    const std::optional<DevicePairing> after = f.pairingStore->load();
    QVERIFY(after.has_value());
    QCOMPARE(after->subscriberId, QStringLiteral("sub-previous"));
    QCOMPARE(after->deviceSecret, QStringLiteral("secret-old"));
}

// There is no acceptable state in which two accounts' data coexist on a device
// whose schema cannot tell them apart. If the purge leaves anything behind,
// the new pairing is REFUSED and the device is wiped -- proceeding would be
// the mixing this whole path exists to prevent.
void PairingControllerTest::aFailedPurgeRefusesTheNewPairingAndEscalates()
{
    ReplacementFixture f(httpResponse(200, "OK", kRegisterOk));
    QVERIFY(f.build(QStringLiteral("sub-previous")));
    f.purgeSucceeds = false;

    QVERIFY(f.controller->pairFromDeepLink(QUrl(f.linkFor(QStringLiteral("sub-new")))));
    f.controller->confirmPendingPair();
    QTRY_COMPARE_WITH_TIMEOUT(f.controller->pairingState(), QStringLiteral("failed"), 5000);

    QCOMPARE(f.purgeCalls, 1);
    QCOMPARE(f.escalateCalls, 1);

    // The new account was NOT persisted: finishPair() is never reached, so
    // nothing from this registration was written.
    const std::optional<DevicePairing> after = f.pairingStore->load();
    if (after.has_value())
        QVERIFY2(after->subscriberId != QStringLiteral("sub-new"),
                 "a refused replacement must not leave the new account paired");
}

// The store cannot be read, so KyPost cannot tell whether this pairing would
// REPLACE an existing account -- and a replacement is what triggers the purge
// of the previous account's cached mail.
//
// MEASURED, because the first version of this comment claimed more than was
// true: without the loadChecked() guard the pairing is refused ANYWAY, before
// the request, by credentialGateEnabled() failing closed on the unreadable
// read and beginPair() then being unable to reseal. So the purge is not
// actually skipped today, and there is no live leak here.
//
// What the guard changes is which reason wins, and the old one was
// "Unlock KyPost first, then try again." -- addressed to a user who has no
// lock to unlock and whose keyring is what is actually broken. It also stops
// the protection being emergent: "an unreadable store cannot skip the purge"
// is now something this code says, rather than something that happens to fall
// out of an unrelated fail-closed check two layers down.
void PairingControllerTest::anUnreadableSecretStoreRefusesToPairRatherThanSkipThePurge()
{
    ReplacementFixture f(httpResponse(200, "OK", kRegisterOk));
    f.storeUnreadable = true;
    QVERIFY(f.build(QString())); // nothing can be seeded: this store takes no writes

    QVERIFY(f.controller->pairFromDeepLink(QUrl(f.linkFor(QStringLiteral("sub-new")))));
    f.controller->confirmPendingPair();
    QTRY_COMPARE_WITH_TIMEOUT(f.controller->pairingState(), QStringLiteral("failed"), 5000);

    // Refused BEFORE the request: no registration was minted server-side, so
    // there is no burnt one-shot device secret to explain to anyone.
    QVERIFY2(f.fake.receivedRequest().isEmpty(),
             "the registration request went out before the store had been read");

    // And it did not quietly take the "first pairing on this device" path.
    QCOMPARE(f.purgeCalls, 0);
    QCOMPARE(f.escalateCalls, 0);

    // The failure says which system component is at fault, not "pairing
    // failed" -- the user can act on a keyring that is not running.
    QVERIFY(f.controller->pairingError().contains(QStringLiteral("secret store")));
}

// The endpoint path is a bearer capability for pushing to this device.
void PairingControllerTest::pushStatusShowsOnlyTheDistributorHost()
{
    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    SettingsStore settingsStore(settingsDir.filePath(QStringLiteral("settings.ini")));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    NativeRegistrationClient client(http);
    HttpClientPinSink pinSink(http);
    NetworkExecutor executor(3000);
    DeviceRegistrationService service(client, pairingStore, settingsStore, pinSink);
    PairingController controller(service, pairingStore, settingsStore, pinSink, executor);
    QSignalSpy spy(&controller, &PairingController::pushStatusChanged);

    controller.setDeviceToken(QStringLiteral("https://ntfy.example/upSECRETTOPIC?up=1"));
    QCOMPARE(controller.pushServerHost(), QStringLiteral("ntfy.example"));
    QCOMPARE(spy.size(), 1);
    controller.setDeviceToken(QStringLiteral("https://ntfy.example/upOTHERTOPIC"));
    QCOMPARE(spy.size(), 1); // same host, nothing to repaint

    QVERIFY(!controller.distributorActive());
    controller.setDistributorActive(true);
    controller.setDistributorActive(true);
    QVERIFY(controller.distributorActive());
    QCOMPARE(spy.size(), 2);
}

QTEST_GUILESS_MAIN(PairingControllerTest)
#include "PairingControllerTest.moc"
