#include "pgp/DeviceEnrollmentCrypto.h"
#include "pgp/OpenPgpDecryptor.h"
#include "pgp/OpenPgpKeyImporter.h"
#include "GnupgFixture.h"

#include <QTest>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QScopeGuard>
#include <openssl/evp.h>
#include <openssl/core_names.h>
#include <openssl/param_build.h>
#include <openssl/bn.h>
#include <openssl/hpke.h>
#include <memory>

#include <algorithm>

class DeviceEnrollmentCryptoTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void refusesRetiredLegacyDeliveries();
    void ringInfoLimitsCountUtf8Bytes();
    void matchesTheBrowserVerificationVector();
    void formatsTheCodeForReading();
    void generatesAValidEphemeralPublicPoint();
    void bindsTheEnvelopeToDeviceAndFingerprint();
    void rejectsMalformedEnvelopes();
    void opensTheCfrgVector();
    void opensHpkeDeliveriesByRecordedVersion();
    void rejectsHpkeNegatives();
    void rejectsHpkeFramingChanges();
    void importsEveryDeliveryVersion();
private:
    void fixedDevice(DeviceEnrollmentCrypto& crypto);
    void loadKey(DeviceEnrollmentCrypto& crypto, const QByteArray& scalar, const QByteArray& publicKey);
    QJsonObject m_fixture;
    // Copied byte-identical from kypost-server 5c52b69d testdata/ (see AGENTS.md section 6j).
    QJsonObject m_hpke;
    QJsonObject m_cfrg;
};

namespace {
QByteArray decoded(const QJsonObject& object, const char* name)
{
    return QByteArray::fromBase64(object.value(QLatin1String(name)).toString().toLatin1());
}

// An honest-looking HPKE envelope for the PUBLIC test device key, labelled `version`.
QByteArray hpkeSealForTest(const QByteArray& publicKey, const QByteArray& info, const QByteArray& plaintext, int version)
{
    const OSSL_HPKE_SUITE suite = {OSSL_HPKE_KEM_ID_P256, OSSL_HPKE_KDF_ID_HKDF_SHA256, OSSL_HPKE_AEAD_ID_AES_GCM_256};
    std::unique_ptr<OSSL_HPKE_CTX, decltype(&OSSL_HPKE_CTX_free)> context(
        OSSL_HPKE_CTX_new(OSSL_HPKE_MODE_BASE, suite, OSSL_HPKE_ROLE_SENDER, nullptr, nullptr), &OSSL_HPKE_CTX_free);
    QByteArray enc(65, '\0');
    QByteArray ct(plaintext.size() + 16, '\0');
    size_t encSize = enc.size();
    size_t ctSize = ct.size();
    if (!context
        || OSSL_HPKE_encap(context.get(), reinterpret_cast<unsigned char*>(enc.data()), &encSize,
            reinterpret_cast<const unsigned char*>(publicKey.constData()), publicKey.size(),
            reinterpret_cast<const unsigned char*>(info.constData()), info.size()) != 1
        || OSSL_HPKE_seal(context.get(), reinterpret_cast<unsigned char*>(ct.data()), &ctSize, nullptr, 0,
            reinterpret_cast<const unsigned char*>(plaintext.constData()), plaintext.size()) != 1)
        return {};
    return QJsonDocument(QJsonObject{{"v", version}, {"alg", "HPKE-Base-P256-SHA256-A256GCM"},
                                     {"enc", QString::fromLatin1(enc.toBase64())},
                                     {"ct", QString::fromLatin1(ct.toBase64())}}).toJson(QJsonDocument::Compact);
}
}

void DeviceEnrollmentCryptoTest::initTestCase()
{
    QFile file(QFINDTESTDATA("../../fixtures/device-envelope-v3.json"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    m_fixture = QJsonDocument::fromJson(file.readAll()).object();
    QCOMPARE(m_fixture.value("vectors").toArray().size(), 2);
    for (auto [name, target] : {std::pair{"../../fixtures/device-envelope-hpke.json", &m_hpke},
                                std::pair{"../../fixtures/hpke-cfrg-p256-aes256.json", &m_cfrg}}) {
        QFile vectors(QFINDTESTDATA(name));
        QVERIFY(vectors.open(QIODevice::ReadOnly));
        *target = QJsonDocument::fromJson(vectors.readAll()).object();
    }
    QCOMPARE(m_hpke.value("vectors").toArray().size(), 2);
    QCOMPARE(m_hpke.value("negative").toArray().size(), 5);
}

void DeviceEnrollmentCryptoTest::fixedDevice(DeviceEnrollmentCrypto& crypto)
{
    // These scalars are PUBLIC TEST DATA. Production can only generate fresh keys.
    loadKey(crypto, decoded(m_fixture, "devicePrivateKey"), decoded(m_fixture, "devicePublicKey"));
}

void DeviceEnrollmentCryptoTest::loadKey(DeviceEnrollmentCrypto& crypto, const QByteArray& scalar,
                                         const QByteArray& publicKey)
{
    crypto.clear();
    crypto.m_publicKey = publicKey;
    std::unique_ptr<BIGNUM, decltype(&BN_clear_free)> number(BN_bin2bn(
        reinterpret_cast<const unsigned char*>(scalar.constData()), scalar.size(), nullptr), BN_clear_free);
    std::unique_ptr<OSSL_PARAM_BLD, decltype(&OSSL_PARAM_BLD_free)> builder(OSSL_PARAM_BLD_new(), OSSL_PARAM_BLD_free);
    QVERIFY(number && builder);
    QVERIFY(OSSL_PARAM_BLD_push_utf8_string(builder.get(), OSSL_PKEY_PARAM_GROUP_NAME, "prime256v1", 0) == 1);
    QVERIFY(OSSL_PARAM_BLD_push_BN(builder.get(), OSSL_PKEY_PARAM_PRIV_KEY, number.get()) == 1);
    QVERIFY(OSSL_PARAM_BLD_push_octet_string(builder.get(), OSSL_PKEY_PARAM_PUB_KEY,
        crypto.m_publicKey.constData(), crypto.m_publicKey.size()) == 1);
    std::unique_ptr<OSSL_PARAM, decltype(&OSSL_PARAM_free)> params(OSSL_PARAM_BLD_to_param(builder.get()), OSSL_PARAM_free);
    std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> ctx(EVP_PKEY_CTX_new_from_name(nullptr, "EC", nullptr), EVP_PKEY_CTX_free);
    QVERIFY(params && ctx);
    QVERIFY(EVP_PKEY_fromdata_init(ctx.get()) == 1);
    QVERIFY(EVP_PKEY_fromdata(ctx.get(), &crypto.m_key, EVP_PKEY_KEYPAIR, params.get()) == 1);
    QVERIFY(crypto.isReady());
}

void DeviceEnrollmentCryptoTest::refusesRetiredLegacyDeliveries()
{
    DeviceEnrollmentCrypto crypto;
    fixedDevice(crypto);
    // Authentic v2/v3 envelopes for this device key: retired, so no version opens them.
    for (const auto& entry : m_fixture.value("vectors").toArray()) {
        const auto vector = entry.toObject();
        const auto json = QJsonDocument(vector.value("envelope").toObject()).toJson(QJsonDocument::Compact);
        for (int version = 1; version <= 6; ++version)
            QVERIFY(crypto.openDelivery(json, version, vector.value("deviceId").toString(),
                                        vector.value("fingerprint").toString()).isEmpty());
    }
    // Nor does a well-formed HPKE envelope relabelled as v2/v3; the same seal as v4/v5 opens.
    for (const auto& entry : m_hpke.value("vectors").toArray()) {
        const auto vector = entry.toObject();
        const int version = vector.value("envelope").toObject().value("v").toInt();
        const auto id = vector.value("deviceId").toString();
        const auto fingerprint = vector.value("fingerprint").toString();
        const auto plaintext = vector.value("plaintext").toString().toUtf8();
        for (int label : {version, version - 2}) {
            const auto info = version == 5 ? deviceEnvelopeV3Aad(id, fingerprint, label)
                                           : deviceEnvelopeAad(id, fingerprint, label);
            const auto json = hpkeSealForTest(crypto.m_publicKey, info, plaintext, label);
            QVERIFY(!json.isEmpty());
            QCOMPARE(crypto.openDelivery(json, label, id, fingerprint).bytes(),
                     label == version ? plaintext : QByteArray());
        }
    }
}

void DeviceEnrollmentCryptoTest::ringInfoLimitsCountUtf8Bytes()
{
    const auto vector = m_hpke.value("vectors").toArray().last().toObject();
    const auto id = vector.value("deviceId").toString();
    const auto fingerprint = vector.value("fingerprint").toString();
    QVERIFY(!deviceEnvelopeV3Aad(QString(65535, QLatin1Char('x')), fingerprint, 5).isEmpty());
    QVERIFY(deviceEnvelopeV3Aad(QString(65536, QLatin1Char('x')), fingerprint, 5).isEmpty());
    QVERIFY(deviceEnvelopeV3Aad(QString(32768, QChar(0x00e9)), fingerprint, 5).isEmpty());
    QVERIFY(!deviceEnvelopeV3Aad(id, QString(64, QLatin1Char('A')), 5).isEmpty());
    QVERIFY(deviceEnvelopeV3Aad(id, QString(39, QLatin1Char('A')), 5).isEmpty());
}

void DeviceEnrollmentCryptoTest::matchesTheBrowserVerificationVector()
{
    QByteArray publicKey(65, '\x02');
    publicKey[0] = '\x04';
    std::fill(publicKey.begin() + 1, publicKey.begin() + 33, '\x01');
    QCOMPARE(deviceEnrollmentCode(publicKey, QStringLiteral("test-device"), 14000000),
             QStringLiteral("5R9K6FWA18A8YP"));
}

void DeviceEnrollmentCryptoTest::formatsTheCodeForReading()
{
    QCOMPARE(formatEnrollmentCode(QStringLiteral("5R9K6FWA18A8YP")),
             QStringLiteral("5R9K-6FW-A18A-8YP"));
}

void DeviceEnrollmentCryptoTest::generatesAValidEphemeralPublicPoint()
{
    DeviceEnrollmentCrypto crypto;
    QVERIFY(crypto.generate());
    const QByteArray point = QByteArray::fromBase64(crypto.publicKeyBase64());
    QCOMPARE(point.size(), 65);
    QCOMPARE(point.front(), '\x04');
    QVERIFY(!crypto.verificationCode(QStringLiteral("device-123"), 123456).isEmpty());
    crypto.clear();
    QVERIFY(!crypto.isReady());
}

void DeviceEnrollmentCryptoTest::bindsTheEnvelopeToDeviceAndFingerprint()
{
    const QByteArray info = deviceEnvelopeAad(
        QStringLiteral("device-123"), QStringLiteral("aa bb cc dd"), 4);
    QVERIFY(info.startsWith("kypost-device-envelope/v4"));
    QVERIFY(info.endsWith("AABBCCDD"));
    QVERIFY(deviceEnvelopeAad(QStringLiteral("device-123"), QStringLiteral("not hex"), 4).isEmpty());
}

void DeviceEnrollmentCryptoTest::rejectsMalformedEnvelopes()
{
    DeviceEnrollmentCrypto crypto;
    QVERIFY(crypto.generate());
    QVERIFY(crypto.openDelivery("{}", 4, QStringLiteral("device-123"),
                                QStringLiteral("AABBCCDD")).isEmpty());
}

void DeviceEnrollmentCryptoTest::opensTheCfrgVector()
{
    const QJsonObject vector = m_cfrg.value("vector").toObject();
    const auto hex = [&](const QJsonObject& object, const char* name) {
        return QByteArray::fromHex(object.value(QLatin1String(name)).toString().toLatin1());
    };
    for (const auto& [field, id] : {std::pair{"mode", 0}, {"kem_id", 16}, {"kdf_id", 1}, {"aead_id", 2}})
        QCOMPARE(vector.value(QLatin1String(field)).toInt(-1), id);
    DeviceEnrollmentCrypto crypto;
    loadKey(crypto, hex(vector, "skRm"), hex(vector, "pkRm"));
    const QJsonObject first = vector.value("encryptions").toArray().first().toObject();
    QCOMPARE(DeviceEnrollmentCrypto::hpkeOpen(crypto.m_key, hex(vector, "enc"), hex(vector, "info"),
                                              hex(first, "aad"), hex(first, "ct")).bytes(),
             hex(first, "pt"));
    QVERIFY(DeviceEnrollmentCrypto::hpkeOpen(crypto.m_key, hex(vector, "enc"), hex(vector, "info"),
                                             QByteArray(), hex(first, "ct")).isEmpty());
}

void DeviceEnrollmentCryptoTest::opensHpkeDeliveriesByRecordedVersion()
{
    DeviceEnrollmentCrypto crypto;
    fixedDevice(crypto);
    QCOMPARE(decoded(m_hpke, "devicePrivateKey"), decoded(m_fixture, "devicePrivateKey"));
    for (const auto& entry : m_hpke.value("vectors").toArray()) {
        const auto vector = entry.toObject();
        const auto envelope = vector.value("envelope").toObject();
        const int version = envelope.value("v").toInt();
        const QString id = vector.value("deviceId").toString();
        const QString fingerprint = vector.value("fingerprint").toString();
        QCOMPARE(version == 5 ? deviceEnvelopeV3Aad(id, fingerprint, 5) : deviceEnvelopeAad(id, fingerprint, 4),
                 decoded(vector, "info"));
        const auto json = QJsonDocument(envelope).toJson(QJsonDocument::Compact);
        QCOMPARE(crypto.openDelivery(json, version, id, fingerprint).bytes(),
                 vector.value("plaintext").toString().toUtf8());
        // The recorded version decides; no other version opens the same bytes.
        for (int other : {1, 2, 3, 4, 5, 6})
            QVERIFY(other == version || crypto.openDelivery(json, other, id, fingerprint).isEmpty());
    }
    crypto.clear();
    const auto v4 = m_hpke.value("vectors").toArray().first().toObject();
    QVERIFY(crypto.openDelivery(QJsonDocument(v4.value("envelope").toObject()).toJson(), 4,
                                v4.value("deviceId").toString(), v4.value("fingerprint").toString()).isEmpty());
}

void DeviceEnrollmentCryptoTest::rejectsHpkeNegatives()
{
    DeviceEnrollmentCrypto crypto;
    fixedDevice(crypto);
    for (const auto& entry : m_hpke.value("negative").toArray()) {
        const auto negative = entry.toObject();
        const auto envelope = negative.value("envelope").toObject();
        const auto json = QJsonDocument(envelope).toJson(QJsonDocument::Compact);
        // The delivery version claimed with it, and the honest one, both refuse.
        for (int version : {envelope.value("v").toInt(), 4, 5})
            QVERIFY2(crypto.openDelivery(json, version, negative.value("deviceId").toString(),
                                         negative.value("fingerprint").toString()).isEmpty(),
                     qPrintable(negative.value("name").toString()));
    }
}

void DeviceEnrollmentCryptoTest::rejectsHpkeFramingChanges()
{
    DeviceEnrollmentCrypto crypto;
    fixedDevice(crypto);
    const auto vector = m_hpke.value("vectors").toArray().last().toObject();
    const auto envelope = vector.value("envelope").toObject();
    const auto id = vector.value("deviceId").toString();
    const auto fingerprint = vector.value("fingerprint").toString();
    const auto opens = [&](const QJsonObject& candidate) {
        return !crypto.openDelivery(QJsonDocument(candidate).toJson(), 5, id, fingerprint).isEmpty();
    };
    QVERIFY(opens(envelope));
    QVERIFY(crypto.openDelivery(QJsonDocument(envelope).toJson(), 5, id, fingerprint.toLower()).isEmpty());
    auto bad = envelope;
    bad.insert("iv", "AAAAAAAAAAAAAAAA");
    QVERIFY(!opens(bad)); // exactly four keys
    for (const QJsonValue& version : {QJsonValue(4), QJsonValue("5"), QJsonValue(5.5)}) {
        bad = envelope;
        bad.insert("v", version);
        QVERIFY(!opens(bad));
    }
    for (const char* field : {"enc", "ct"}) {
        bad = envelope;
        QString value = bad.value(QLatin1String(field)).toString();
        value.chop(1); // drops the padding: not canonical base64
        bad.insert(QLatin1String(field), value);
        QVERIFY(!opens(bad));
        bad.remove(QLatin1String(field));
        QVERIFY(!opens(bad));
    }
    auto ciphertext = decoded(envelope, "ct");
    ciphertext.data()[ciphertext.size() - 1] ^= 1;
    bad = envelope;
    bad.insert("ct", QString::fromLatin1(ciphertext.toBase64()));
    QVERIFY(!opens(bad));
    bad.insert("ct", QString::fromLatin1(QByteArray(16, '\0').toBase64()));
    QVERIFY(!opens(bad));
    bad = envelope;
    bad.insert("enc", QString::fromLatin1(decoded(envelope, "enc").mid(1).prepend('\x02').toBase64()));
    QVERIFY(!opens(bad)); // compressed point marker
    // A ring envelope is capped at 128 KiB.
    QByteArray json = QJsonDocument(envelope).toJson(QJsonDocument::Compact);
    json.append(QByteArray(128 * 1024 - json.size(), ' '));
    QVERIFY(!crypto.openDelivery(json, 5, id, fingerprint).isEmpty());
    json.append(' ');
    QVERIFY(crypto.openDelivery(json, 5, id, fingerprint).isEmpty());
}

void DeviceEnrollmentCryptoTest::importsEveryDeliveryVersion()
{
    if (!OpenPgpDecryptor::engineAvailable())
        QSKIP("GnuPG is required to import deliveries");
    DeviceEnrollmentCrypto crypto;
    fixedDevice(crypto);
    const auto deliveries = m_hpke.value("vectors").toArray();
    QCOMPARE(deliveries.size(), 2);
    for (const auto& entry : deliveries) {
        const auto vector = entry.toObject();
        const auto envelope = vector.value("envelope").toObject();
        const int version = envelope.value("v").toInt();
        const auto fingerprint = vector.value("fingerprint").toString();
        const SecureBytes plaintext = crypto.openDelivery(QJsonDocument(envelope).toJson(), version,
                                                          vector.value("deviceId").toString(), fingerprint);
        QVERIFY(!plaintext.isEmpty());
        // What the server's GET envelope supplies; a test ring stands in for it here.
        PgpKeyringSnapshot expected;
        expected.activeFingerprint = fingerprint;
        if (version % 2 == 1) {
            const auto ring = QJsonDocument::fromJson(plaintext.bytes()).object();
            expected.materialGeneration = qint64(ring.value("materialGeneration").toDouble());
            for (const auto& value : ring.value("keyFingerprints").toArray())
                expected.keyFingerprints.append(value.toString());
        }
        QTemporaryDir target;
        QVERIFY(target.isValid());
        const auto home = GnupgFixture::emptyHome(target);
        const auto cleanup = qScopeGuard([&] { GnupgFixture::killAgent(home); });
        // The wrong shape for the version is refused before the keyring is touched.
        QCOMPARE(importDeviceDelivery(version % 2 == 1 ? version - 1 : version + 1, plaintext, expected, home),
                 PgpKeyringImportStatus::Rejected);
        // So is the same plaintext labelled as its retired legacy version.
        QCOMPARE(importDeviceDelivery(version - 2, plaintext, expected, home), PgpKeyringImportStatus::Rejected);
        QCOMPARE(importDeviceDelivery(version, plaintext, PgpKeyringSnapshot{}, home), PgpKeyringImportStatus::Rejected);
        QCOMPARE(importDeviceDelivery(version, plaintext, expected, home), PgpKeyringImportStatus::Imported);
        QVERIFY(GnupgFixture::fingerprintsIn(home).contains(fingerprint));
        QCOMPARE(importDeviceDelivery(version, plaintext, expected, home), PgpKeyringImportStatus::Unchanged);
        auto stale = expected;
        stale.materialGeneration += 1;
        stale.activeFingerprint = version % 2 == 1 ? fingerprint : QString(40, QLatin1Char('A'));
        QCOMPARE(importDeviceDelivery(version, plaintext, stale, home), PgpKeyringImportStatus::Rejected);
    }
}

QTEST_GUILESS_MAIN(DeviceEnrollmentCryptoTest)
#include "DeviceEnrollmentCryptoTest.moc"
