#pragma once

#include "security/SecureBytes.h"

#include <QByteArray>
#include <QString>

typedef struct evp_pkey_st EVP_PKEY;
class QJsonObject;

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
    SecureBytes openEnvelope(const QByteArray& envelopeJson, const QString& deviceId,
                             const QString& fingerprint) const;
    // Explicit v3-only primitive. Returns authenticated raw keyring JSON in
    // zeroising memory; callers must validate and durably import the entire
    // ring before acknowledging enrollment.
    SecureBytes openKeyringEnvelope(const QByteArray& envelopeJson, const QString& deviceId,
                                    const QString& activeFingerprint) const;
    // Opens exactly the server-recorded delivery version, never a fallback:
    // 2/4 (ECDH/HPKE) yield one armored key, 3/5 the raw keyring JSON.
    SecureBytes openDelivery(const QByteArray& envelopeJson, int version, const QString& deviceId,
                             const QString& fingerprint) const;
    void clear();

private:
    friend class DeviceEnrollmentCryptoTest; // fixed public interoperability scalars stay test-only
    SecureBytes open(const QByteArray& envelopeJson, const QString& deviceId,
                     const QString& fingerprint, int version) const;
    SecureBytes openEcdh(const QJsonObject& envelope, const QByteArray& aad, int version) const;
    // RFC 9180 base mode, DHKEM(P-256)/HKDF-SHA256/AES-256-GCM, via OpenSSL.
    static SecureBytes hpkeOpen(EVP_PKEY* key, const QByteArray& encapsulated, const QByteArray& info,
                                const QByteArray& aad, const QByteArray& ciphertext);
    SecureBytes sharedSecret(const QByteArray& peerPoint) const;
    SecureBytes envelopeKey(const QByteArray& peerPoint, int version) const;
    EVP_PKEY* m_key = nullptr;
    QByteArray m_publicKey;
};

// Also the HPKE info for v4 (deviceEnvelopeAad) and v5 (deviceEnvelopeV3Aad).
QByteArray deviceEnvelopeAad(const QString& deviceId, const QString& fingerprint, int version = 2);
QByteArray deviceEnvelopeV3Aad(const QString& deviceId, const QString& activeFingerprint, int version = 3);
QString deviceEnrollmentCode(const QByteArray& publicKey, const QString& deviceId, qint64 bucket);
QString formatEnrollmentCode(const QString& code);
