#pragma once

#include "security/SecureBytes.h"

#include <QByteArray>
#include <QString>

typedef struct evp_pkey_st EVP_PKEY;

class DeviceEnrollmentCrypto
{
public:
    DeviceEnrollmentCrypto() = default;
    ~DeviceEnrollmentCrypto();
    DeviceEnrollmentCrypto(const DeviceEnrollmentCrypto&) = delete;
    DeviceEnrollmentCrypto& operator=(const DeviceEnrollmentCrypto&) = delete;

    bool generate();
    bool isReady() const { return m_key != nullptr && m_publicKey.size() == 65; }
    QByteArray publicKeyBase64() const { return m_publicKey.toBase64(); }
    QString verificationCode(const QString& deviceId, qint64 bucket) const;
    // Opens exactly the server-recorded delivery version, never a fallback:
    // RFC 9180 v4 yields one armored key, v5 the raw keyring JSON. Anything else,
    // including retired v2/v3, is refused.
    SecureBytes openDelivery(const QByteArray& envelopeJson, int version, const QString& deviceId,
                             const QString& fingerprint) const;
    void clear();

private:
    friend class DeviceEnrollmentCryptoTest; // fixed public interoperability scalars stay test-only
    // RFC 9180 base mode, DHKEM(P-256)/HKDF-SHA256/AES-256-GCM, via OpenSSL.
    static SecureBytes hpkeOpen(EVP_PKEY* key, const QByteArray& encapsulated, const QByteArray& info,
                                const QByteArray& aad, const QByteArray& ciphertext);
    EVP_PKEY* m_key = nullptr;
    QByteArray m_publicKey;
};

// HPKE info for v4 (deviceEnvelopeAad) and v5 (deviceEnvelopeV3Aad); the names
// keep the v2/v3 AAD layout they share.
QByteArray deviceEnvelopeAad(const QString& deviceId, const QString& fingerprint, int version);
QByteArray deviceEnvelopeV3Aad(const QString& deviceId, const QString& activeFingerprint, int version);
QString deviceEnrollmentCode(const QByteArray& publicKey, const QString& deviceId, qint64 bucket);
QString formatEnrollmentCode(const QString& code);
