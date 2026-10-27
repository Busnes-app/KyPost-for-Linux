#include "pgp/DeviceEnrollmentCrypto.h"

#include <QJsonDocument>
#include <QJsonObject>

#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/hpke.h>
#include <openssl/params.h>

#include <algorithm>
#include <memory>

namespace {
constexpr auto kDomainPrefix = "kypost-device-envelope/v";
constexpr int kMaxRingEnvelopeBytes = 128 * 1024;
constexpr auto kHpkeAlgorithm = "HPKE-Base-P256-SHA256-A256GCM";
constexpr auto kCrockford = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
constexpr int kMaxEnvelopeBytes = 1024 * 1024;

QByteArray strictBase64(const QJsonValue& value)
{
    if (!value.isString())
        return {};
    const auto decoded = QByteArray::fromBase64Encoding(value.toString().toLatin1(),
                                                         QByteArray::AbortOnBase64DecodingErrors);
    if (decoded.decodingStatus != QByteArray::Base64DecodingStatus::Ok
        || QString::fromLatin1(decoded.decoded.toBase64()) != value.toString())
        return {};
    return decoded.decoded;
}
}

DeviceEnrollmentCrypto::~DeviceEnrollmentCrypto()
{
    clear();
}

void DeviceEnrollmentCrypto::clear()
{
    if (m_key != nullptr)
        EVP_PKEY_free(m_key);
    m_key = nullptr;
    m_publicKey.clear();
}

bool DeviceEnrollmentCrypto::generate()
{
    clear();
    std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> context(
        EVP_PKEY_CTX_new_from_name(nullptr, "EC", nullptr), &EVP_PKEY_CTX_free);
    if (!context || EVP_PKEY_keygen_init(context.get()) <= 0)
        return false;
    char group[] = "prime256v1";
    OSSL_PARAM parameters[] = {
        OSSL_PARAM_construct_utf8_string(OSSL_PKEY_PARAM_GROUP_NAME, group, 0),
        OSSL_PARAM_construct_end()
    };
    if (EVP_PKEY_CTX_set_params(context.get(), parameters) <= 0
        || EVP_PKEY_generate(context.get(), &m_key) <= 0)
        return false;
    size_t size = 0;
    if (EVP_PKEY_get_octet_string_param(m_key, OSSL_PKEY_PARAM_PUB_KEY, nullptr, 0, &size) <= 0
        || size != 65) {
        clear();
        return false;
    }
    m_publicKey.resize(static_cast<qsizetype>(size));
    if (EVP_PKEY_get_octet_string_param(m_key, OSSL_PKEY_PARAM_PUB_KEY,
                                         reinterpret_cast<unsigned char*>(m_publicKey.data()),
                                         size, &size) <= 0
        || static_cast<qsizetype>(size) != m_publicKey.size() || m_publicKey.front() != '\x04') {
        clear();
        return false;
    }
    return true;
}

QString DeviceEnrollmentCrypto::verificationCode(const QString& deviceId, qint64 bucket) const
{
    if (!isReady())
        return {};
    return deviceEnrollmentCode(m_publicKey, deviceId, bucket);
}

QString deviceEnrollmentCode(const QByteArray& publicKey, const QString& deviceId, qint64 bucket)
{
    if (publicKey.size() != 65 || publicKey.front() != '\x04')
        return {};
    const QByteArray id = deviceId.toUtf8();
    if (id.size() > 0xffff)
        return {};
    QByteArray preimage = publicKey;
    preimage.append(static_cast<char>((id.size() >> 8) & 0xff));
    preimage.append(static_cast<char>(id.size() & 0xff));
    preimage.append(id);
    for (int shift = 56; shift >= 0; shift -= 8)
        preimage.append(static_cast<char>((static_cast<quint64>(bucket) >> shift) & 0xff));
    unsigned char digest[32];
    unsigned int digestSize = 0;
    if (EVP_Digest(preimage.constData(), preimage.size(), digest, &digestSize, EVP_sha256(), nullptr) != 1
        || digestSize != sizeof(digest))
        return {};
    QString code;
    code.reserve(14);
    for (int character = 0; character < 14; ++character) {
        int value = 0;
        for (int offset = 0; offset < 5; ++offset) {
            const int bit = character * 5 + offset;
            value = (value << 1) | ((digest[bit / 8] >> (7 - bit % 8)) & 1);
        }
        code.append(QLatin1Char(kCrockford[value]));
    }
    return code;
}

QString formatEnrollmentCode(const QString& code)
{
    if (code.size() != 14)
        return code;
    return code.left(4) + QLatin1Char('-') + code.mid(4, 3) + QLatin1Char('-')
        + code.mid(7, 4) + QLatin1Char('-') + code.right(3);
}

QByteArray deviceEnvelopeAad(const QString& deviceId, const QString& fingerprint, int version)
{
    const QByteArray id = deviceId.toUtf8();
    // simplified() turns every whitespace run into one ' ', then those go.
    const QString normalized = fingerprint.toUpper().simplified().remove(QLatin1Char(' '));
    const QByteArray fp = normalized.toLatin1();
    if (id.size() > 0xffff || fp.isEmpty() || fp.size() > 0xffff
        || !std::all_of(fp.cbegin(), fp.cend(), [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F');
        }))
        return {};
    QByteArray aad(kDomainPrefix + QByteArray::number(version));
    aad.append(static_cast<char>((id.size() >> 8) & 0xff));
    aad.append(static_cast<char>(id.size() & 0xff));
    aad.append(id);
    aad.append(static_cast<char>((fp.size() >> 8) & 0xff));
    aad.append(static_cast<char>(fp.size() & 0xff));
    aad.append(fp);
    return aad;
}

QByteArray deviceEnvelopeV3Aad(const QString& deviceId, const QString& activeFingerprint, int version)
{
    const QByteArray id = deviceId.toUtf8();
    const QByteArray fingerprint = activeFingerprint.toUtf8();
    if (id.isEmpty() || id.size() > 0xffff
        || (fingerprint.size() != 40 && fingerprint.size() != 64)
        || !std::all_of(fingerprint.cbegin(), fingerprint.cend(), [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F');
        }))
        return {};
    // V5 binds exact UTF-8 bytes. V4's normalization must not leak into it.
    QByteArray aad(kDomainPrefix + QByteArray::number(version));
    aad.append(static_cast<char>((id.size() >> 8) & 0xff));
    aad.append(static_cast<char>(id.size() & 0xff));
    aad.append(id);
    aad.append(static_cast<char>((fingerprint.size() >> 8) & 0xff));
    aad.append(static_cast<char>(fingerprint.size() & 0xff));
    aad.append(fingerprint);
    return aad;
}

SecureBytes DeviceEnrollmentCrypto::openDelivery(const QByteArray& envelopeJson, int version,
                                                  const QString& deviceId, const QString& fingerprint) const
{
    // Only RFC 9180 v4 (one armored key) and v5 (the complete ring); v2/v3 are retired.
    const bool ring = version == 5;
    const int maximum = ring ? kMaxRingEnvelopeBytes : kMaxEnvelopeBytes;
    if ((version != 4 && !ring) || !isReady() || envelopeJson.isEmpty() || envelopeJson.size() > maximum)
        return {};
    const QJsonDocument document = QJsonDocument::fromJson(envelopeJson);
    if (!document.isObject())
        return {};
    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("v")) != QJsonValue(version)
        || object.value(QStringLiteral("alg")).toString() != QLatin1String(kHpkeAlgorithm))
        return {};
    // info comes from this device's own ID and the checked fingerprint, never the envelope.
    const QByteArray info = ring ? deviceEnvelopeV3Aad(deviceId, fingerprint, version)
                                 : deviceEnvelopeAad(deviceId, fingerprint, version);
    const QByteArray encapsulated = strictBase64(object.value(QStringLiteral("enc")));
    const QByteArray ciphertext = strictBase64(object.value(QStringLiteral("ct")));
    if (info.isEmpty() || object.size() != 4 || encapsulated.size() != 65 || encapsulated.front() != '\x04')
        return {};
    SecureBytes plaintext = hpkeOpen(m_key, encapsulated, info, {}, ciphertext);
    if (plaintext.isEmpty() || (!ring && !plaintext.bytes().startsWith("-----BEGIN PGP PRIVATE KEY BLOCK-----")))
        return {};
    return plaintext;
}

SecureBytes DeviceEnrollmentCrypto::hpkeOpen(EVP_PKEY* key, const QByteArray& encapsulated, const QByteArray& info,
                                             const QByteArray& aad, const QByteArray& ciphertext)
{
    if (key == nullptr || ciphertext.size() <= 16)
        return {};
    const OSSL_HPKE_SUITE suite = {OSSL_HPKE_KEM_ID_P256, OSSL_HPKE_KDF_ID_HKDF_SHA256, OSSL_HPKE_AEAD_ID_AES_GCM_256};
    std::unique_ptr<OSSL_HPKE_CTX, decltype(&OSSL_HPKE_CTX_free)> context(
        OSSL_HPKE_CTX_new(OSSL_HPKE_MODE_BASE, suite, OSSL_HPKE_ROLE_RECEIVER, nullptr, nullptr), &OSSL_HPKE_CTX_free);
    SecureBytes plaintext(QByteArray(ciphertext.size() - 16, '\0'));
    size_t size = static_cast<size_t>(plaintext.bytes().size());
    if (!context
        || OSSL_HPKE_decap(context.get(), reinterpret_cast<const unsigned char*>(encapsulated.constData()),
            encapsulated.size(), key, reinterpret_cast<const unsigned char*>(info.constData()), info.size()) != 1
        || OSSL_HPKE_open(context.get(), reinterpret_cast<unsigned char*>(plaintext.bytes().data()), &size,
            reinterpret_cast<const unsigned char*>(aad.constData()), aad.size(),
            reinterpret_cast<const unsigned char*>(ciphertext.constData()), ciphertext.size()) != 1)
        return {};
    plaintext.bytes().resize(static_cast<qsizetype>(size));
    return plaintext;
}
