#include "net/PgpDeviceEnrollmentClient.h"

#include "net/HttpClient.h"
#include "net/RelayAuth.h"

#include "FakeRelayServer.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QTest>

class PgpDeviceEnrollmentClientTest : public QObject
{
    Q_OBJECT

private slots:
    void advertisesEveryOpenableEnvelopeVersion();
    void readsTheRecordedVersionAndRingSnapshot();
    void aStaleOrLegacyDeliveryHasNoRingSnapshot();
    void acknowledgesExactlyTheDeliveredRing();
};

namespace {
const QString kFingerprint = QStringLiteral("5F117951610CAF01500FA059CDE63F0EBEC934A2");

QUrl urlOf(const FakeRelayServer& fake)
{
    return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(fake.port()));
}

QJsonObject delivery(int version, double generation, const QJsonValue& keyring)
{
    return QJsonObject{{"envelope", "{}"}, {"version", version}, {"fingerprint", kFingerprint.toLower()},
                       {"materialGeneration", generation}, {"keyring", keyring}};
}
}

void PgpDeviceEnrollmentClientTest::advertisesEveryOpenableEnvelopeVersion()
{
    FakeRelayServer fake(httpResponse(200, "OK", R"({"ok":true})"));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    const auto result = PgpDeviceEnrollmentClient(http).publishKey(urlOf(fake), {}, "BAAA");
    QCOMPARE(result.status, DeviceEnrollmentCallStatus::Ok);
    QVERIFY(fake.receivedRequest().startsWith("POST /api/pgp/device/enrollment-key "));
    const QJsonObject body = fake.receivedJsonBody();
    QCOMPARE(body.value("publicKey").toString(), QStringLiteral("BAAA"));
    QCOMPARE(body.value("envelopeVersions").toArray(), (QJsonArray{2, 3, 4, 5}));
}

void PgpDeviceEnrollmentClientTest::readsTheRecordedVersionAndRingSnapshot()
{
    const QJsonObject ring{{"version", 1}, {"materialGeneration", 7},
                           {"primaryFingerprints", QJsonArray{kFingerprint}},
                           {"keyFingerprints", QJsonArray{kFingerprint, QString(40, QLatin1Char('B'))}}};
    FakeRelayServer fake(httpResponse(200, "OK", QJsonDocument(delivery(5, 7, ring)).toJson()));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    const auto result = PgpDeviceEnrollmentClient(http).fetchEnvelope(urlOf(fake), {});
    QCOMPARE(result.status, DeviceEnrollmentCallStatus::Ok);
    QCOMPARE(result.version, 5);
    QVERIFY(result.keyring.has_value());
    QCOMPARE(result.keyring->activeFingerprint, kFingerprint);
    QCOMPARE(result.keyring->materialGeneration, qint64(7));
    QCOMPARE(result.keyring->keyFingerprints, (QStringList{kFingerprint, QString(40, QLatin1Char('B'))}));
}

void PgpDeviceEnrollmentClientTest::aStaleOrLegacyDeliveryHasNoRingSnapshot()
{
    const QJsonObject ring{{"materialGeneration", 8}, {"keyFingerprints", QJsonArray{kFingerprint}}};
    for (const QJsonObject& body : {delivery(3, 7, ring), delivery(3, 7.5, QJsonObject{{"materialGeneration", 7.5}}),
                                    delivery(2, 0, QJsonValue::Null)}) {
        FakeRelayServer fake(httpResponse(200, "OK", QJsonDocument(body).toJson()));
        QNetworkAccessManager manager;
        HttpClient http(manager);
        const auto result = PgpDeviceEnrollmentClient(http).fetchEnvelope(urlOf(fake), {});
        QCOMPARE(result.status, DeviceEnrollmentCallStatus::Ok);
        QCOMPARE(result.version, body.value("version").toInt());
        QVERIFY(!result.keyring.has_value());
    }
}

void PgpDeviceEnrollmentClientTest::acknowledgesExactlyTheDeliveredRing()
{
    FakeRelayServer fake(httpResponse(200, "OK", R"({"ok":true})"));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    PgpKeyringSnapshot held;
    held.activeFingerprint = kFingerprint;
    held.materialGeneration = 7;
    const auto result = PgpDeviceEnrollmentClient(http).acknowledgeKeyring(urlOf(fake), {}, 5, held);
    QCOMPARE(result.status, DeviceEnrollmentCallStatus::Ok);
    QVERIFY(fake.receivedRequest().startsWith("POST /api/pgp/device/enrollment-state "));
    QCOMPARE(fake.receivedJsonBody(),
             (QJsonObject{{"encryptionEnrolled", true}, {"envelopeVersion", 5},
                          {"materialGeneration", 7}, {"fingerprint", kFingerprint}}));
}

QTEST_GUILESS_MAIN(PgpDeviceEnrollmentClientTest)
#include "PgpDeviceEnrollmentClientTest.moc"
