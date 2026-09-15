#include "pgp/DeviceEnrollmentCrypto.h"

#include <QTest>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <openssl/evp.h>
#include <openssl/core_names.h>
#include <openssl/param_build.h>
#include <openssl/bn.h>
#include <memory>

#include <algorithm>

class DeviceEnrollmentCryptoTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void matchesSharedEnvelopeVectors();
    void rejectsV3BindingAndFramingChanges();
    void v3LimitsCountUtf8Bytes();
    void matchesTheBrowserVerificationVector();
    void formatsTheCodeForReading();
    void generatesAValidEphemeralPublicPoint();
    void bindsTheEnvelopeToDeviceAndFingerprint();
    void rejectsMalformedEnvelopes();
private:
    void fixedDevice(DeviceEnrollmentCrypto& crypto);
    QJsonObject m_fixture;
};

namespace {
QByteArray decoded(const QJsonObject& object, const char* name)
{
    return QByteArray::fromBase64(object.value(QLatin1String(name)).toString().toLatin1());
}

QByteArray sealForTest(const QByteArray& plaintext, const QByteArray& key, const QByteArray& iv, const QByteArray& aad)
{
    std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    QByteArray ciphertext(plaintext.size() + 16, '\0');
    int count = 0, final = 0;
    if (!ctx || EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr,
            reinterpret_cast<const unsigned char*>(key.constData()),
            reinterpret_cast<const unsigned char*>(iv.constData())) != 1
        || EVP_EncryptUpdate(ctx.get(), nullptr, &count,
            reinterpret_cast<const unsigned char*>(aad.constData()), aad.size()) != 1
        || EVP_EncryptUpdate(ctx.get(), reinterpret_cast<unsigned char*>(ciphertext.data()), &count,
            reinterpret_cast<const unsigned char*>(plaintext.constData()), plaintext.size()) != 1
        || EVP_EncryptFinal_ex(ctx.get(), reinterpret_cast<unsigned char*>(ciphertext.data()) + count, &final) != 1
        || EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, 16, ciphertext.data() + count + final) != 1)
        return {};
    ciphertext.resize(count + final + 16);
    return ciphertext;
}
}

void DeviceEnrollmentCryptoTest::initTestCase()
{
    QFile file(QFINDTESTDATA("../../fixtures/device-envelope-v3.json"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    m_fixture = QJsonDocument::fromJson(file.readAll()).object();
    QCOMPARE(m_fixture.value("vectors").toArray().size(), 2);
}

void DeviceEnrollmentCryptoTest::fixedDevice(DeviceEnrollmentCrypto& crypto)
{
    crypto.clear();
    // These scalars are PUBLIC TEST DATA. Production can only generate fresh keys.
    const QByteArray scalar = decoded(m_fixture, "devicePrivateKey");
    crypto.m_publicKey = decoded(m_fixture, "devicePublicKey");
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

void DeviceEnrollmentCryptoTest::matchesSharedEnvelopeVectors()
{
    DeviceEnrollmentCrypto crypto;
    fixedDevice(crypto);
    for (const auto& entry : m_fixture.value("vectors").toArray()) {
        const auto vector = entry.toObject();
        const auto envelope = vector.value("envelope").toObject();
        const int version = envelope.value("v").toInt();
        const QString id = vector.value("deviceId").toString();
        const QString fingerprint = vector.value("fingerprint").toString();
        const auto point = decoded(envelope, "epk");
        auto shared = crypto.sharedSecret(point);
        QCOMPARE(shared.bytes(), decoded(vector, "sharedSecret"));
        auto key = crypto.envelopeKey(point, version);
        QCOMPARE(key.bytes(), decoded(vector, "aesKey"));
        const auto aad = version == 3 ? deviceEnvelopeV3Aad(id, fingerprint) : deviceEnvelopeAad(id, fingerprint);
        QCOMPARE(aad, decoded(vector, "aad"));
        const auto json = QJsonDocument(envelope).toJson(QJsonDocument::Compact);
        auto plaintext = version == 3 ? crypto.openKeyringEnvelope(json, id, fingerprint)
                                      : crypto.openEnvelope(json, id, fingerprint);
        QCOMPARE(plaintext.bytes(), vector.value("plaintext").toString().toUtf8());
        QCOMPARE(sealForTest(plaintext.bytes(), key.bytes(), decoded(envelope, "iv"), aad), decoded(envelope, "ct"));
        // Version selection is explicit, never a fallback after failed authentication.
        QVERIFY((version == 3 ? crypto.openEnvelope(json, id, fingerprint)
                             : crypto.openKeyringEnvelope(json, id, fingerprint)).isEmpty());
    }
}

void DeviceEnrollmentCryptoTest::rejectsV3BindingAndFramingChanges()
{
    DeviceEnrollmentCrypto crypto;
    fixedDevice(crypto);
    const auto vector = m_fixture.value("vectors").toArray().last().toObject();
    const auto envelope = vector.value("envelope").toObject();
    const auto json = QJsonDocument(envelope).toJson(QJsonDocument::Compact);
    const auto id = vector.value("deviceId").toString();
    const auto fingerprint = vector.value("fingerprint").toString();
    QVERIFY(crypto.openKeyringEnvelope(json, id + QLatin1Char(' '), fingerprint).isEmpty());
    QVERIFY(crypto.openKeyringEnvelope(json, id, QString(40, QLatin1Char('A'))).isEmpty());
    QVERIFY(crypto.openKeyringEnvelope(json, id, fingerprint.toLower()).isEmpty());
    QVERIFY(crypto.openKeyringEnvelope(json, QString(), fingerprint).isEmpty());
    QVERIFY(crypto.openKeyringEnvelope(json, id, fingerprint + QLatin1Char(' ')).isEmpty());
    for (const QJsonValue& version : {QJsonValue(2), QJsonValue(4), QJsonValue(3.5), QJsonValue("3")}) {
        auto bad = envelope;
        bad.insert("v", version);
        QVERIFY(crypto.openKeyringEnvelope(QJsonDocument(bad).toJson(), id, fingerprint).isEmpty());
    }
    for (const char* field : {"epk", "iv", "ct", "alg"}) {
        auto bad = envelope;
        bad.insert(QLatin1String(field), "unsupported");
        QVERIFY(crypto.openKeyringEnvelope(QJsonDocument(bad).toJson(), id, fingerprint).isEmpty());
        bad.insert(QLatin1String(field), 12);
        QVERIFY(crypto.openKeyringEnvelope(QJsonDocument(bad).toJson(), id, fingerprint).isEmpty());
    }
    auto reject = [&](const char* field, const QByteArray& bytes) {
        auto bad = envelope;
        bad.insert(QLatin1String(field), QString::fromLatin1(bytes.toBase64()));
        return crypto.openKeyringEnvelope(QJsonDocument(bad).toJson(), id, fingerprint).isEmpty();
    };
    auto ciphertext = decoded(envelope, "ct");
    ciphertext[ciphertext.size() - 1] ^= 1;
    QVERIFY(reject("ct", ciphertext));
    ciphertext = decoded(envelope, "ct");
    ciphertext[0] ^= 1;
    QVERIFY(reject("ct", ciphertext));
    QVERIFY(reject("ct", QByteArray(16, '\0')));
    QVERIFY(reject("iv", QByteArray(11, '\0')));
    QVERIFY(reject("epk", QByteArray(65, '\0')));
    QVERIFY(reject("epk", QByteArray(1, '\x04') + QByteArray(64, '\0'))); // off-curve
    auto unpadded = envelope;
    QString point = unpadded.value("epk").toString();
    point.chop(1);
    unpadded.insert("epk", point);
    QVERIFY(crypto.openKeyringEnvelope(QJsonDocument(unpadded).toJson(), id, fingerprint).isEmpty());
    // Separately catch a v2 domain in either HKDF or AAD.
    auto v2Key = crypto.envelopeKey(decoded(envelope, "epk"), 2);
    auto v3Key = crypto.envelopeKey(decoded(envelope, "epk"), 3);
    const auto plaintext = vector.value("plaintext").toString().toUtf8();
    QVERIFY(reject("ct", sealForTest(plaintext, v2Key.bytes(), decoded(envelope, "iv"), deviceEnvelopeV3Aad(id, fingerprint))));
    QVERIFY(reject("ct", sealForTest(plaintext, v3Key.bytes(), decoded(envelope, "iv"), deviceEnvelopeAad(id, fingerprint))));
    crypto.clear();
    QVERIFY(crypto.openKeyringEnvelope(json, id, fingerprint).isEmpty());
}

void DeviceEnrollmentCryptoTest::v3LimitsCountUtf8Bytes()
{
    DeviceEnrollmentCrypto crypto;
    fixedDevice(crypto);
    const auto vector = m_fixture.value("vectors").toArray().last().toObject();
    const auto id = vector.value("deviceId").toString();
    const auto fingerprint = vector.value("fingerprint").toString();
    QByteArray json = QJsonDocument(vector.value("envelope").toObject()).toJson(QJsonDocument::Compact);
    json.append(QByteArray(128 * 1024 - json.size(), ' '));
    QVERIFY(!crypto.openKeyringEnvelope(json, id, fingerprint).isEmpty());
    json.append(' ');
    QVERIFY(crypto.openKeyringEnvelope(json, id, fingerprint).isEmpty());
    QVERIFY(!deviceEnvelopeV3Aad(QString(65535, QLatin1Char('x')), fingerprint).isEmpty());
    QVERIFY(deviceEnvelopeV3Aad(QString(65536, QLatin1Char('x')), fingerprint).isEmpty());
    QVERIFY(deviceEnvelopeV3Aad(QString(32768, QChar(0x00e9)), fingerprint).isEmpty());
    QVERIFY(!deviceEnvelopeV3Aad(id, QString(64, QLatin1Char('A'))).isEmpty());
    QVERIFY(deviceEnvelopeV3Aad(id, QString(39, QLatin1Char('A'))).isEmpty());
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
    const QByteArray aad = deviceEnvelopeAad(
        QStringLiteral("device-123"), QStringLiteral("aa bb cc dd"));
    QVERIFY(aad.startsWith("kypost-device-envelope/v2"));
    QVERIFY(aad.endsWith("AABBCCDD"));
    QVERIFY(deviceEnvelopeAad(QStringLiteral("device-123"), QStringLiteral("not hex")).isEmpty());
}

void DeviceEnrollmentCryptoTest::rejectsMalformedEnvelopes()
{
    DeviceEnrollmentCrypto crypto;
    QVERIFY(crypto.generate());
    QVERIFY(crypto.openEnvelope("{}", QStringLiteral("device-123"),
                                QStringLiteral("AABBCCDD")).isEmpty());
}

QTEST_GUILESS_MAIN(DeviceEnrollmentCryptoTest)
#include "DeviceEnrollmentCryptoTest.moc"
