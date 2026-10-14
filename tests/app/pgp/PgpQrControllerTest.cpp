#include "pgp/PgpQrController.h"

#include "domain/DevicePairing.h"
#include "domain/PairingStore.h"
#include "domain/PgpQrRepository.h"
#include "net/HttpClient.h"
#include "net/NetworkExecutor.h"
#include "net/PgpQrClient.h"
#include "pgp/OpenPgpDecryptor.h"
#include "stores/SecureStoreFile.h"

#include "../../core/net/FakeRelayServer.h"
#include "../../core/pgp/GnupgFixture.h"

#include <QJsonDocument>
#include <QJsonObject>

#include <QNetworkAccessManager>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class PgpQrControllerTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void refreshMyQrCodeWithoutPairingSetsNotPairedError();
    void refreshMyQrCodeSuccessPopulatesUrlAndExpiresAt();
    void myQrCodeIsNotOfferedForAnAccountThisDeviceNoLongerHas();
    void refreshMyQrCodeNoPgpIdentitySetsFriendlyMessage();
    void myQrImageDataUrlIsEmptyBeforeRefreshAndPopulatedAfter();
    void scanQrPayloadRejectsNonPgpQrUrl();
    void scanQrPayloadRejectsAPathThatMerelyContainsTheKeyRoute();
    void scanQrPayloadRejectsNonHttpScheme();
    void scanQrPayloadRejectsLinkLocalMetadataHost();
    void scanQrPayloadSuccessPopulatesScanResult();
    void scanQrPayloadSuccessPopulatesContactCardFields();
    void scanQrPayloadWithNoContactCardReturnsAllEmptyFields();
    void scanQrPayload404SetsFriendlyMessage();
    void clearScanResultResetsFields();

    // Behaviour that only exists once the fetches are asynchronous.
    void refreshMyQrCodeReturnsWithoutBlocking();
    void repeatedScansWhileOneIsInFlightAreCoalesced();
    void scanRejectsDotSegmentPathsThatResolveOffTheKeyEndpoint();

    // The in-person check, both sides of it.
    void myQrCodeCarriesTheFingerprintOfTheKeyThisDeviceHolds();
    void myQrCodeWithoutTheKeyShowsTheServersFingerprintAndNoFp();
    void aScanWhoseKeyMatchesTheCodeIsConfirmed();
    void aScanWhoseKeyDiffersFromTheCodeIsRefused();
    void aScanWithAMalformedFingerprintIsRefused();
    void aScanWithoutAFingerprintIsNotConfirmed();
    void aKeyThisDeviceCannotReadIsRefused();

private:
    static void savePairing(PairingStore& pairingStore, quint16 port);
    // A key-pickup response carrying the fixture's real public key, under a
    // "fingerprint" field that names some other key.
    QByteArray keyBody(const QByteArray& contactCard = {}) const;

    // The throwaway keyring: the scanned person's key, and on the showing
    // side the key "this device" holds. GNUPGHOME points at it throughout.
    GnupgFixture m_keyring;
    QByteArray m_publicKey;
    QString m_fingerprint;
};

// Every test in this file must keep away from the developer's own keyring.
void PgpQrControllerTest::initTestCase()
{
    if (!OpenPgpDecryptor::engineAvailable())
        QSKIP("no usable gpg on this system -- the in-person key check is NOT covered");
    QVERIFY(m_keyring.build(QStringLiteral("Ada <ada@example.com>")));
    qputenv("GNUPGHOME", m_keyring.path().toUtf8());
    m_publicKey = m_keyring.exportPublicKey(QStringLiteral("ada@example.com"));
    m_fingerprint = m_keyring.fingerprintOf(QStringLiteral("ada@example.com"));
    QVERIFY(!m_publicKey.isEmpty());
    QCOMPARE(m_fingerprint.size(), 40);
}

void PgpQrControllerTest::cleanupTestCase()
{
    GnupgFixture::killAgent(m_keyring.path());
}

QByteArray PgpQrControllerTest::keyBody(const QByteArray& contactCard) const
{
    QJsonObject body{ { QStringLiteral("name"), QStringLiteral("Ada") },
                      { QStringLiteral("fingerprint"), QStringLiteral("ABCD1234") },
                      { QStringLiteral("publicKey"), QString::fromUtf8(m_publicKey) } };
    if (!contactCard.isEmpty())
        body.insert(QStringLiteral("contactCard"), QJsonDocument::fromJson(contactCard).object());
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

void PgpQrControllerTest::savePairing(PairingStore& pairingStore, quint16 port)
{
    DevicePairing pairing;
    pairing.subscriberId = QStringLiteral("sub-1");
    pairing.deviceSecret = QStringLiteral("secret-1");
    pairing.serverBaseUrl = QStringLiteral("http://127.0.0.1:%1").arg(port);
    pairing.registrationUrl = QStringLiteral("http://127.0.0.1:%1/api/notifications/native/register").arg(port);
    pairing.pairingToken = QStringLiteral("pair-tok");
    pairing.deviceId = QStringLiteral("device-1");
    pairing.deviceName = QStringLiteral("My Linux Desktop");
    QVERIFY(pairingStore.save(pairing));
}

void PgpQrControllerTest::refreshMyQrCodeWithoutPairingSetsNotPairedError()
{
    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore); // never saved -- not paired

    QNetworkAccessManager manager;
    HttpClient http(manager);
    PgpQrClient client(http);
    PgpQrRepository repository(client, pairingStore);
    NetworkExecutor executor(3000);
    PgpQrController controller(repository, executor);

    controller.refreshMyQrCode();

    // The request is now dispatched off this thread; isBusy is set before
    // the call returns and cleared by the completion handler, so it is the
    // barrier for "the answer has landed".
    QTRY_VERIFY_WITH_TIMEOUT(!controller.isBusy(), 5000);

    QCOMPARE(controller.lastError(), QStringLiteral("Not paired"));
    QCOMPARE(controller.myQrUrl(), QString());
}

void PgpQrControllerTest::refreshMyQrCodeSuccessPopulatesUrlAndExpiresAt()
{
    const QByteArray body =
        R"({"token":"tok-1","expiresAt":"2026-07-17T12:02:00Z","url":"https://example.com/api/pgp/qr/key?t=tok-1"})";
    FakeRelayServer fake(httpResponse(200, "OK", body));

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);
    savePairing(pairingStore, fake.port());

    QNetworkAccessManager manager;
    HttpClient http(manager);
    PgpQrClient client(http);
    PgpQrRepository repository(client, pairingStore);
    NetworkExecutor executor(3000);
    PgpQrController controller(repository, executor);

    QSignalSpy dataSpy(&controller, &PgpQrController::myQrDataChanged);
    controller.refreshMyQrCode();

    // The request is now dispatched off this thread; isBusy is set before
    // the call returns and cleared by the completion handler, so it is the
    // barrier for "the answer has landed".
    QTRY_VERIFY_WITH_TIMEOUT(!controller.isBusy(), 5000);

    QCOMPARE(controller.lastError(), QString());
    QCOMPARE(controller.myQrUrl(), QStringLiteral("https://example.com/api/pgp/qr/key?t=tok-1"));
    QCOMPARE(controller.myQrExpiresAt(), QStringLiteral("2026-07-17T12:02:00Z"));
    QVERIFY(dataSpy.count() >= 1);
}

// The QR encodes a key-exchange token for THIS account. After the account is
// replaced, the screen keeps showing the previous one's -- nothing re-fetches
// it and nothing clears it -- so a correspondent who scans it exchanges keys
// with the account this device just removed, believing it is the current one.
//
// Same shape as the other stale-reply sites: what is on screen belongs to an
// account that is gone, and nothing about it says so.
void PgpQrControllerTest::myQrCodeIsNotOfferedForAnAccountThisDeviceNoLongerHas()
{
    const QByteArray body =
        R"({"token":"tok-1","expiresAt":"2026-07-17T12:02:00Z","url":"https://example.com/api/pgp/qr/key?t=tok-1"})";
    FakeRelayServer fake(httpResponse(200, "OK", body));

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);
    savePairing(pairingStore, fake.port());

    QNetworkAccessManager manager;
    HttpClient http(manager);
    PgpQrClient client(http);
    PgpQrRepository repository(client, pairingStore);
    NetworkExecutor executor(3000);
    PgpQrController controller(repository, executor);

    controller.refreshMyQrCode();
    QTRY_VERIFY_WITH_TIMEOUT(!controller.isBusy(), 5000);
    QVERIFY(!controller.myQrUrl().isEmpty());

    // A different account on this device.
    DevicePairing replacement;
    replacement.subscriberId = QStringLiteral("sub-2");
    replacement.deviceSecret = QStringLiteral("secret-2");
    replacement.serverBaseUrl = QStringLiteral("http://127.0.0.1:%1").arg(fake.port());
    replacement.deviceId = QStringLiteral("dev-2");
    QVERIFY(pairingStore.save(replacement));

    QVERIFY2(controller.myQrUrl().isEmpty(),
             "the previous account's key-exchange QR is still on offer");
    QVERIFY2(controller.myQrExpiresAt().isEmpty(),
             "the previous account's QR expiry is still shown");
}

void PgpQrControllerTest::refreshMyQrCodeNoPgpIdentitySetsFriendlyMessage()
{
    FakeRelayServer fake(httpResponse(400, "Bad Request", "no pgp identity configured\n", "text/plain"));

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);
    savePairing(pairingStore, fake.port());

    QNetworkAccessManager manager;
    HttpClient http(manager);
    PgpQrClient client(http);
    PgpQrRepository repository(client, pairingStore);
    NetworkExecutor executor(3000);
    PgpQrController controller(repository, executor);

    controller.refreshMyQrCode();

    // The request is now dispatched off this thread; isBusy is set before
    // the call returns and cleared by the completion handler, so it is the
    // barrier for "the answer has landed".
    QTRY_VERIFY_WITH_TIMEOUT(!controller.isBusy(), 5000);

    QCOMPARE(controller.lastError(), QStringLiteral("You haven't set up PGP encryption yet"));
}

void PgpQrControllerTest::myQrImageDataUrlIsEmptyBeforeRefreshAndPopulatedAfter()
{
    const QByteArray body =
        R"({"token":"tok-1","expiresAt":"2026-07-17T12:02:00Z","url":"https://example.com/api/pgp/qr/key?t=tok-1"})";
    FakeRelayServer fake(httpResponse(200, "OK", body));

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);
    savePairing(pairingStore, fake.port());

    QNetworkAccessManager manager;
    HttpClient http(manager);
    PgpQrClient client(http);
    PgpQrRepository repository(client, pairingStore);
    NetworkExecutor executor(3000);
    PgpQrController controller(repository, executor);

    // Nothing fetched yet -- no URL to encode.
    QCOMPARE(controller.myQrImageDataUrl(), QString());

    controller.refreshMyQrCode();

    // The request is now dispatched off this thread; isBusy is set before
    // the call returns and cleared by the completion handler, so it is the
    // barrier for "the answer has landed".
    QTRY_VERIFY_WITH_TIMEOUT(!controller.isBusy(), 5000);

    const QString dataUrl = controller.myQrImageDataUrl();
    QVERIFY(dataUrl.startsWith(QStringLiteral("data:image/png;base64,")));
    // A real, non-trivial PNG payload was actually encoded, not a stub.
    QVERIFY(dataUrl.length() > 100);
}

void PgpQrControllerTest::scanQrPayloadRejectsNonPgpQrUrl()
{
    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);

    QNetworkAccessManager manager;
    HttpClient http(manager);
    PgpQrClient client(http);
    PgpQrRepository repository(client, pairingStore);
    NetworkExecutor executor(3000);
    PgpQrController controller(repository, executor);

    // No FakeRelayServer at all -- an invalid payload must be rejected
    // before any network call is attempted.
    controller.scanQrPayload(QStringLiteral("https://example.com/totally/unrelated"));

    // The request is now dispatched off this thread; isBusy is set before
    // the call returns and cleared by the completion handler, so it is the
    // barrier for "the answer has landed".
    QTRY_VERIFY_WITH_TIMEOUT(!controller.isBusy(), 5000);

    QCOMPARE(controller.lastError(), QStringLiteral("That QR code isn't a PGP key-exchange code"));
    QCOMPARE(controller.scannedFingerprint(), QString());
}

// The route check used contains(), so any path with "/api/pgp/qr/key"
// somewhere inside it passed -- and the same predicate validates every
// redirect hop, so a permitted host could 302 the QR's capability token onto
// whatever path it liked. The token is the query parameter "t"; the path ENDS
// at the route.
void PgpQrControllerTest::scanQrPayloadRejectsAPathThatMerelyContainsTheKeyRoute()
{
    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);

    QNetworkAccessManager manager;
    HttpClient http(manager);
    PgpQrClient client(http);
    PgpQrRepository repository(client, pairingStore);
    NetworkExecutor executor(3000);
    PgpQrController controller(repository, executor);

    // No FakeRelayServer: each of these must be refused before any request is
    // made, so a fetch reaching the network at all would hang rather than pass.
    const QStringList nearMisses = {
        QStringLiteral("https://example.com/api/pgp/qr/keyanything?t=tok-1"),
        QStringLiteral("https://example.com/api/pgp/qr/key-backup?t=tok-1"),
        QStringLiteral("https://example.com/api/pgp/qr/key/admin?t=tok-1"),
        QStringLiteral("https://example.com/api/pgp/qr/key/../../internal/admin?t=tok-1"),
    };

    for (const QString& url : nearMisses) {
        controller.scanQrPayload(url);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.isBusy(), 5000);
        QVERIFY2(controller.lastError() == QStringLiteral("That QR code isn't a PGP key-exchange code"),
                 qPrintable(QStringLiteral("accepted %1").arg(url)));
        QCOMPARE(controller.scannedFingerprint(), QString());
    }
}

void PgpQrControllerTest::scanQrPayloadRejectsNonHttpScheme()
{
    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);

    QNetworkAccessManager manager;
    HttpClient http(manager);
    PgpQrClient client(http);
    PgpQrRepository repository(client, pairingStore);
    NetworkExecutor executor(3000);
    PgpQrController controller(repository, executor);

    // A file:// QR payload must never reach HttpClient/QNetworkAccessManager
    // -- doing so would let a scanned QR code read local files back as if
    // they were key material.
    controller.scanQrPayload(QStringLiteral("file:///etc/passwd#/api/pgp/qr/key"));

    // The request is now dispatched off this thread; isBusy is set before
    // the call returns and cleared by the completion handler, so it is the
    // barrier for "the answer has landed".
    QTRY_VERIFY_WITH_TIMEOUT(!controller.isBusy(), 5000);

    QCOMPARE(controller.lastError(), QStringLiteral("That QR code isn't a PGP key-exchange code"));
    QCOMPARE(controller.scannedFingerprint(), QString());
}

void PgpQrControllerTest::scanQrPayloadRejectsLinkLocalMetadataHost()
{
    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);

    QNetworkAccessManager manager;
    HttpClient http(manager);
    PgpQrClient client(http);
    PgpQrRepository repository(client, pairingStore);
    NetworkExecutor executor(3000);
    PgpQrController controller(repository, executor);

    // 169.254.169.254 is the cloud-metadata address on AWS/Azure/DigitalOcean
    // -- must be rejected before any request is attempted, same as the
    // file:// case above.
    controller.scanQrPayload(QStringLiteral("http://169.254.169.254/api/pgp/qr/key"));

    // The request is now dispatched off this thread; isBusy is set before
    // the call returns and cleared by the completion handler, so it is the
    // barrier for "the answer has landed".
    QTRY_VERIFY_WITH_TIMEOUT(!controller.isBusy(), 5000);

    QCOMPARE(controller.lastError(), QStringLiteral("That QR code isn't a PGP key-exchange code"));
    QCOMPARE(controller.scannedFingerprint(), QString());
}

void PgpQrControllerTest::scanQrPayloadSuccessPopulatesScanResult()
{
    FakeRelayServer fake(httpResponse(200, "OK", keyBody()));

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);

    QNetworkAccessManager manager;
    HttpClient http(manager);
    PgpQrClient client(http);
    PgpQrRepository repository(client, pairingStore);
    NetworkExecutor executor(3000);
    PgpQrController controller(repository, executor);

    QSignalSpy scanSpy(&controller, &PgpQrController::scanResultChanged);
    const QString qrUrl = QStringLiteral("http://127.0.0.1:%1/api/pgp/qr/key?t=tok-1").arg(fake.port());
    controller.scanQrPayload(qrUrl);

    // The request is now dispatched off this thread; isBusy is set before
    // the call returns and cleared by the completion handler, so it is the
    // barrier for "the answer has landed".
    QTRY_VERIFY_WITH_TIMEOUT(!controller.isBusy(), 5000);

    QCOMPARE(controller.lastError(), QString());
    QCOMPARE(controller.scannedName(), QStringLiteral("Ada"));
    // Computed here from the key, never the server's "fingerprint" field.
    QCOMPARE(controller.scannedFingerprint(), m_fingerprint);
    QCOMPARE(controller.scannedPublicKey(), QString::fromUtf8(m_publicKey));
    QVERIFY(scanSpy.count() >= 1);
}

void PgpQrControllerTest::scanQrPayloadSuccessPopulatesContactCardFields()
{
    const QByteArray body = keyBody(R"({
            "fn":"Ada Lovelace","org":"Analytical Engines Ltd","notes":"Pioneer of computing",
            "emails":[{"label":"work","value":"ada@example.com"}],
            "phones":[{"label":"mobile","value":"+1-555-0100"}],
            "addresses":[{"label":"home","street":"12 Torrington Street","city":"London","country":"UK"}],
            "department":"Engineering","pronouns":"she/her",
            "ims":[{"service":"Matrix","label":"work","value":"@ada:example.org"}],
            "websites":[{"label":"blog","value":"https://ada.example.com"}],
            "relations":[{"label":"spouse","name":"William King"}],
            "events":[{"label":"anniversary","date":"2026-06-01"}],
            "customFields":[{"label":"Employee ID","value":"42"}]
        })");
    FakeRelayServer fake(httpResponse(200, "OK", body));

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);

    QNetworkAccessManager manager;
    HttpClient http(manager);
    PgpQrClient client(http);
    PgpQrRepository repository(client, pairingStore);
    NetworkExecutor executor(3000);
    PgpQrController controller(repository, executor);

    const QString qrUrl = QStringLiteral("http://127.0.0.1:%1/api/pgp/qr/key?t=tok-1").arg(fake.port());
    controller.scanQrPayload(qrUrl);

    // The request is now dispatched off this thread; isBusy is set before
    // the call returns and cleared by the completion handler, so it is the
    // barrier for "the answer has landed".
    QTRY_VERIFY_WITH_TIMEOUT(!controller.isBusy(), 5000);

    const QVariantMap fields = controller.scannedContactCardFields();
    QCOMPARE(fields.value(QStringLiteral("org")).toString(), QStringLiteral("Analytical Engines Ltd"));
    QCOMPARE(fields.value(QStringLiteral("notes")).toString(), QStringLiteral("Pioneer of computing"));
    const QVariantList emails = fields.value(QStringLiteral("emails")).toList();
    QCOMPARE(emails.size(), 1);
    QCOMPARE(emails.first().toMap().value(QStringLiteral("value")).toString(), QStringLiteral("ada@example.com"));

    const QVariantList phones = fields.value(QStringLiteral("phones")).toList();
    QCOMPARE(phones.size(), 1);
    QCOMPARE(phones.first().toMap().value(QStringLiteral("value")).toString(), QStringLiteral("+1-555-0100"));

    const QVariantList addresses = fields.value(QStringLiteral("addresses")).toList();
    QCOMPARE(addresses.size(), 1);
    QCOMPARE(addresses.first().toMap().value(QStringLiteral("street")).toString(),
              QStringLiteral("12 Torrington Street"));
    QCOMPARE(addresses.first().toMap().value(QStringLiteral("country")).toString(), QStringLiteral("UK"));

    QCOMPARE(fields.value(QStringLiteral("department")).toString(), QStringLiteral("Engineering"));
    QCOMPARE(fields.value(QStringLiteral("pronouns")).toString(), QStringLiteral("she/her"));

    const QVariantList ims = fields.value(QStringLiteral("ims")).toList();
    QCOMPARE(ims.size(), 1);
    QCOMPARE(ims.first().toMap().value(QStringLiteral("value")).toString(), QStringLiteral("@ada:example.org"));

    const QVariantList websites = fields.value(QStringLiteral("websites")).toList();
    QCOMPARE(websites.size(), 1);
    QCOMPARE(websites.first().toMap().value(QStringLiteral("value")).toString(), QStringLiteral("https://ada.example.com"));

    const QVariantList relations = fields.value(QStringLiteral("relations")).toList();
    QCOMPARE(relations.size(), 1);
    QCOMPARE(relations.first().toMap().value(QStringLiteral("name")).toString(), QStringLiteral("William King"));

    const QVariantList events = fields.value(QStringLiteral("events")).toList();
    QCOMPARE(events.size(), 1);
    QCOMPARE(events.first().toMap().value(QStringLiteral("date")).toString(), QStringLiteral("2026-06-01"));

    const QVariantList customFields = fields.value(QStringLiteral("customFields")).toList();
    QCOMPARE(customFields.size(), 1);
    QCOMPARE(customFields.first().toMap().value(QStringLiteral("value")).toString(), QStringLiteral("42"));
}

void PgpQrControllerTest::scanQrPayloadWithNoContactCardReturnsAllEmptyFields()
{
    FakeRelayServer fake(httpResponse(200, "OK", keyBody()));

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);

    QNetworkAccessManager manager;
    HttpClient http(manager);
    PgpQrClient client(http);
    PgpQrRepository repository(client, pairingStore);
    NetworkExecutor executor(3000);
    PgpQrController controller(repository, executor);

    const QString qrUrl = QStringLiteral("http://127.0.0.1:%1/api/pgp/qr/key?t=tok-1").arg(fake.port());
    controller.scanQrPayload(qrUrl);

    // The request is now dispatched off this thread; isBusy is set before
    // the call returns and cleared by the completion handler, so it is the
    // barrier for "the answer has landed".
    QTRY_VERIFY_WITH_TIMEOUT(!controller.isBusy(), 5000);

    const QVariantMap fields = controller.scannedContactCardFields();
    QCOMPARE(fields.value(QStringLiteral("org")).toString(), QString());
    QVERIFY(fields.value(QStringLiteral("emails")).toList().isEmpty());
    QVERIFY(fields.value(QStringLiteral("phones")).toList().isEmpty());
    QVERIFY(fields.value(QStringLiteral("addresses")).toList().isEmpty());
    QVERIFY(fields.value(QStringLiteral("ims")).toList().isEmpty());
    QVERIFY(fields.value(QStringLiteral("customFields")).toList().isEmpty());
}

void PgpQrControllerTest::scanQrPayload404SetsFriendlyMessage()
{
    FakeRelayServer fake(httpResponse(404, "Not Found", "no pgp identity configured\n", "text/plain"));

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);

    QNetworkAccessManager manager;
    HttpClient http(manager);
    PgpQrClient client(http);
    PgpQrRepository repository(client, pairingStore);
    NetworkExecutor executor(3000);
    PgpQrController controller(repository, executor);

    const QString qrUrl = QStringLiteral("http://127.0.0.1:%1/api/pgp/qr/key?t=tok-1").arg(fake.port());
    controller.scanQrPayload(qrUrl);

    // The request is now dispatched off this thread; isBusy is set before
    // the call returns and cleared by the completion handler, so it is the
    // barrier for "the answer has landed".
    QTRY_VERIFY_WITH_TIMEOUT(!controller.isBusy(), 5000);

    QCOMPARE(controller.lastError(), QStringLiteral("This person hasn't set up PGP encryption yet"));
}

void PgpQrControllerTest::clearScanResultResetsFields()
{
    FakeRelayServer fake(httpResponse(200, "OK", keyBody()));

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);

    QNetworkAccessManager manager;
    HttpClient http(manager);
    PgpQrClient client(http);
    PgpQrRepository repository(client, pairingStore);
    NetworkExecutor executor(3000);
    PgpQrController controller(repository, executor);

    const QString qrUrl = QStringLiteral("http://127.0.0.1:%1/api/pgp/qr/key?t=tok-1").arg(fake.port());
    controller.scanQrPayload(qrUrl);

    // The request is now dispatched off this thread; isBusy is set before
    // the call returns and cleared by the completion handler, so it is the
    // barrier for "the answer has landed".
    QTRY_VERIFY_WITH_TIMEOUT(!controller.isBusy(), 5000);
    QVERIFY(!controller.scannedFingerprint().isEmpty());

    controller.clearScanResult();

    QCOMPARE(controller.scannedName(), QString());
    QCOMPARE(controller.scannedFingerprint(), QString());
    QCOMPARE(controller.scannedPublicKey(), QString());
    QCOMPARE(controller.lastError(), QString());
    QVERIFY(controller.scannedContactCardFields().value(QStringLiteral("org")).toString().isEmpty());
}


// The endpoint gate was `path().contains("/api/pgp/qr/key")`. Qt resolves dot
// segments when it puts the request on the wire but keeps them in
// QUrl::path(), so ".../api/pgp/qr/key/../../../../internal/admin" satisfied
// the check while actually requesting "/internal/admin" -- and
// isSafeQrTarget deliberately allows loopback over plain http, where the
// certificate pin never engages.
void PgpQrControllerTest::scanRejectsDotSegmentPathsThatResolveOffTheKeyEndpoint()
{
    FakeRelayServer fake(httpResponse(200, "OK", "{}"));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    SecureStoreFile secureStore(dir.path());
    PairingStore pairingStore(secureStore);
    QNetworkAccessManager manager;
    HttpClient http(manager);
    PgpQrClient client(http);
    PgpQrRepository repository(client, pairingStore);
    NetworkExecutor executor(3000);
    PgpQrController controller(repository, executor);

    controller.scanQrPayload(QStringLiteral("http://127.0.0.1:%1/api/pgp/qr/key/../../../../internal/admin")
                                  .arg(fake.port()));

    // The request is now dispatched off this thread; isBusy is set before
    // the call returns and cleared by the completion handler, so it is the
    // barrier for "the answer has landed".
    QTRY_VERIFY_WITH_TIMEOUT(!controller.isBusy(), 5000);

    // Refused before any request went out.
    QVERIFY(!controller.lastError().isEmpty());
    QVERIFY(fake.receivedRequest().isEmpty());
    QVERIFY(controller.scannedFingerprint().isEmpty());
}

// The point of the conversion: the QR fetch hands control straight back
// instead of sitting inside HttpClient's nested event loop for the length of
// a round trip. PgpMyQrCode.qml calls this from Component.onCompleted, so
// under the old shape opening the screen froze the UI until the relay
// answered.
void PgpQrControllerTest::refreshMyQrCodeReturnsWithoutBlocking()
{
    // Accepts the connection and never answers, so the request runs until
    // the executor's transfer timeout.
    FakeRelayServer fake(QByteArray{});

    QTemporaryDir secureDir;
    QVERIFY(secureDir.isValid());
    SecureStoreFile secureStore(secureDir.path());
    PairingStore pairingStore(secureStore);
    savePairing(pairingStore, fake.port());

    QNetworkAccessManager manager;
    HttpClient http(manager);
    PgpQrClient client(http);
    PgpQrRepository repository(client, pairingStore);
    NetworkExecutor executor(1500);
    PgpQrController controller(repository, executor);

    QElapsedTimer timer;
    timer.start();
    controller.refreshMyQrCode();
    const qint64 elapsed = timer.elapsed();

    QVERIFY2(elapsed < 200, qPrintable(QStringLiteral("refreshMyQrCode() blocked for %1 ms").arg(elapsed)));
    QVERIFY(controller.isBusy());

    QTRY_VERIFY_WITH_TIMEOUT(!controller.isBusy(), 10000);
    QVERIFY(!controller.lastError().isEmpty());
}

// A camera decodes the same QR many times a second, so without coalescing
// every frame would start another fetch of the same key.
//
// Asserted as "ten calls cost exactly what one call costs" rather than as an
// absolute connection count: against a server that accepts and never
// answers, a single request does not map to a single TCP connection --
// Qt retries an idempotent GET after the transfer timeout, which was
// measured here as 3 connections for 1 request. Comparing the two runs
// asserts the property (the extra calls were dropped) without depending on
// Qt's retry behaviour.
void PgpQrControllerTest::repeatedScansWhileOneIsInFlightAreCoalesced()
{
    const auto connectionsForScanCount = [this](int scans) {
        FakeRelayServer fake(QByteArray{});

        QTemporaryDir secureDir;
        [&] { QVERIFY(secureDir.isValid()); }();
        SecureStoreFile secureStore(secureDir.path());
        PairingStore pairingStore(secureStore);

        QNetworkAccessManager manager;
        HttpClient http(manager);
        PgpQrClient client(http);
        PgpQrRepository repository(client, pairingStore);
        NetworkExecutor executor(1500);
        PgpQrController controller(repository, executor);

        const QString url = QStringLiteral("http://127.0.0.1:%1/api/pgp/qr/key?t=tok").arg(fake.port());
        for (int i = 0; i < scans; ++i)
            controller.scanQrPayload(url);

        [&] { QVERIFY(controller.isBusy()); }();
        [&] { QTRY_VERIFY_WITH_TIMEOUT(!controller.isBusy(), 10000); }();
        return fake.connectionCount();
    };

    const int one = connectionsForScanCount(1);
    const int ten = connectionsForScanCount(10);
    QVERIFY(one > 0);
    QCOMPARE(ten, one);
}

namespace {

// The in-person tests share one shape; the scan side needs no pairing.
struct Rig
{
    QTemporaryDir secureDir;
    SecureStoreFile secureStore{ secureDir.path() };
    PairingStore pairingStore{ secureStore };
    QNetworkAccessManager manager;
    HttpClient http{ manager };
    PgpQrClient client{ http };
    PgpQrRepository repository{ client, pairingStore };
    NetworkExecutor executor{ 3000 };
    PgpQrController controller{ repository, executor };
};

const QByteArray kTokenBody =
    R"({"token":"tok-1","expiresAt":"2026-07-17T12:02:00Z","url":"https://example.com/api/pgp/qr/key?t=tok-1"})";
const QString kPickup = QStringLiteral("https://example.com/api/pgp/qr/key?t=tok-1");
// A well-formed fingerprint of a key nobody here holds.
const QString kOtherFingerprint = QStringLiteral("0123456789ABCDEF0123456789ABCDEF01234567");

QByteArray bootstrapNaming(const QString& fingerprint)
{
    const QJsonObject body{ { QStringLiteral("hasIdentity"), true },
                            { QStringLiteral("protection"), QStringLiteral("client") },
                            { QStringLiteral("fingerprint"), fingerprint } };
    return httpResponse(200, "OK", QJsonDocument(body).toJson(QJsonDocument::Compact));
}

QString mismatch()
{
    return QStringLiteral("This key doesn't match the code they showed you. Nothing was saved.");
}

} // namespace

void PgpQrControllerTest::myQrCodeCarriesTheFingerprintOfTheKeyThisDeviceHolds()
{
    FakeRelayServer fake(httpResponse(200, "OK", kTokenBody));
    fake.setResponseForPath("/api/pgp/bootstrap", bootstrapNaming(m_fingerprint));
    Rig rig;
    savePairing(rig.pairingStore, fake.port());

    rig.controller.refreshMyQrCode();
    QTRY_VERIFY_WITH_TIMEOUT(!rig.controller.isBusy(), 10000);

    QCOMPARE(rig.controller.lastError(), QString());
    QVERIFY(rig.controller.myFingerprintIsLocal());
    QCOMPARE(rig.controller.myFingerprint(), m_fingerprint);
    QCOMPARE(rig.controller.myQrUrl(), kPickup + QStringLiteral("#fp=") + m_fingerprint);
}

// The relay names a key this device holds no secret for. What is shown is
// only the relay's word, and the code must not vouch for it.
void PgpQrControllerTest::myQrCodeWithoutTheKeyShowsTheServersFingerprintAndNoFp()
{
    FakeRelayServer fake(httpResponse(200, "OK", kTokenBody));
    fake.setResponseForPath("/api/pgp/bootstrap", bootstrapNaming(kOtherFingerprint));
    Rig rig;
    savePairing(rig.pairingStore, fake.port());

    rig.controller.refreshMyQrCode();
    QTRY_VERIFY_WITH_TIMEOUT(!rig.controller.isBusy(), 10000);

    QCOMPARE(rig.controller.lastError(), QString());
    QVERIFY(!rig.controller.myFingerprintIsLocal());
    QCOMPARE(rig.controller.myFingerprint(), kOtherFingerprint);
    QCOMPARE(rig.controller.myQrUrl(), kPickup);
}

void PgpQrControllerTest::aScanWhoseKeyMatchesTheCodeIsConfirmed()
{
    FakeRelayServer fake(httpResponse(200, "OK", keyBody()));
    Rig rig;

    rig.controller.scanQrPayload(
        QStringLiteral("http://127.0.0.1:%1/api/pgp/qr/key?t=tok-1#fp=").arg(fake.port()) + m_fingerprint);
    QTRY_VERIFY_WITH_TIMEOUT(!rig.controller.isBusy(), 10000);

    QCOMPARE(rig.controller.lastError(), QString());
    QVERIFY(rig.controller.scannedVerified());
    QCOMPARE(rig.controller.scannedFingerprint(), m_fingerprint);
    QCOMPARE(rig.controller.scannedPublicKey(), QString::fromUtf8(m_publicKey));
    // The fragment is for this device, never for the server.
    QVERIFY(!fake.receivedRequest().contains("fp="));
}

void PgpQrControllerTest::aScanWhoseKeyDiffersFromTheCodeIsRefused()
{
    FakeRelayServer fake(httpResponse(200, "OK", keyBody()));
    Rig rig;

    rig.controller.scanQrPayload(
        QStringLiteral("http://127.0.0.1:%1/api/pgp/qr/key?t=tok-1#fp=").arg(fake.port()) + kOtherFingerprint);
    QTRY_VERIFY_WITH_TIMEOUT(!rig.controller.isBusy(), 10000);

    QCOMPARE(rig.controller.lastError(), mismatch());
    QVERIFY(!rig.controller.scannedVerified());
    QCOMPARE(rig.controller.scannedFingerprint(), QString());
    QCOMPARE(rig.controller.scannedPublicKey(), QString());
}

void PgpQrControllerTest::aScanWithAMalformedFingerprintIsRefused()
{
    FakeRelayServer fake(httpResponse(200, "OK", keyBody()));
    Rig rig;

    for (const QString& fp : { m_fingerprint.left(39), m_fingerprint.left(39) + QStringLiteral("Z"),
                               m_fingerprint + QStringLiteral("0"), QString() }) {
        rig.controller.scanQrPayload(
            QStringLiteral("http://127.0.0.1:%1/api/pgp/qr/key?t=tok-1#fp=").arg(fake.port()) + fp);
        QTRY_VERIFY_WITH_TIMEOUT(!rig.controller.isBusy(), 10000);

        QCOMPARE(rig.controller.lastError(), mismatch());
        QCOMPARE(rig.controller.scannedFingerprint(), QString());
        QCOMPARE(rig.controller.scannedPublicKey(), QString());
    }
    // Refused from the code alone; there was nothing to fetch.
    QCOMPARE(fake.connectionCount(), 0);
}

void PgpQrControllerTest::aScanWithoutAFingerprintIsNotConfirmed()
{
    FakeRelayServer fake(httpResponse(200, "OK", keyBody()));
    Rig rig;

    rig.controller.scanQrPayload(QStringLiteral("http://127.0.0.1:%1/api/pgp/qr/key?t=tok-1").arg(fake.port()));
    QTRY_VERIFY_WITH_TIMEOUT(!rig.controller.isBusy(), 10000);

    QCOMPARE(rig.controller.lastError(), QString());
    QVERIFY(!rig.controller.scannedVerified());
    QCOMPARE(rig.controller.scannedFingerprint(), m_fingerprint);
    QCOMPARE(rig.controller.scannedPublicKey(), QString::fromUtf8(m_publicKey));
}

// With no fingerprint of its own to show, the only one left is the server's.
void PgpQrControllerTest::aKeyThisDeviceCannotReadIsRefused()
{
    const QByteArray body =
        R"({"name":"Ada","fingerprint":"ABCD1234","publicKey":"-----BEGIN PGP PUBLIC KEY BLOCK-----"})";
    FakeRelayServer fake(httpResponse(200, "OK", body));
    Rig rig;

    rig.controller.scanQrPayload(QStringLiteral("http://127.0.0.1:%1/api/pgp/qr/key?t=tok-1").arg(fake.port()));
    QTRY_VERIFY_WITH_TIMEOUT(!rig.controller.isBusy(), 10000);

    QVERIFY(!rig.controller.lastError().isEmpty());
    QCOMPARE(rig.controller.scannedFingerprint(), QString());
    QCOMPARE(rig.controller.scannedPublicKey(), QString());
}

QTEST_GUILESS_MAIN(PgpQrControllerTest)
#include "PgpQrControllerTest.moc"

