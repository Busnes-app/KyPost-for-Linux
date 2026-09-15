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
    SecureBytes openEnvelope(const QByteArray& envelopeJson, const QString& deviceId,
                             const QString& fingerprint) const;
    // Explicit v3-only primitive. Returns authenticated raw keyring JSON in
    // zeroising memory; callers must validate and durably import the entire
    // ring before acknowledging enrollment. The legacy controller uses v2 only.
    SecureBytes openKeyringEnvelope(const QByteArray& envelopeJson, const QString& deviceId,
                                    const QString& activeFingerprint) const;
    void clear();

private:
    friend class DeviceEnrollmentCryptoTest; // fixed public interoperability scalars stay test-only
    SecureBytes open(const QByteArray& envelopeJson, const QString& deviceId,
                     const QString& fingerprint, int version) const;
    SecureBytes sharedSecret(const QByteArray& peerPoint) const;
    SecureBytes envelopeKey(const QByteArray& peerPoint, int version) const;
    EVP_PKEY* m_key = nullptr;
    QByteArray m_publicKey;
};

QByteArray deviceEnvelopeAad(const QString& deviceId, const QString& fingerprint);
QByteArray deviceEnvelopeV3Aad(const QString& deviceId, const QString& activeFingerprint);
QString deviceEnrollmentCode(const QByteArray& publicKey, const QString& deviceId, qint64 bucket);
QString formatEnrollmentCode(const QString& code);
