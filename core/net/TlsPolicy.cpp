#include "net/TlsPolicy.h"

#include <QNetworkRequest>
#include <QSslConfiguration>
#include <QSslSocket>

#include <openssl/evp.h>
#include <openssl/x509.h>

#include <memory>

bool tlsCertificateKeyAllowed(const QByteArray& certificateDer)
{
    // DER, not QSslCertificate::publicKey(): Qt's key wrapper returns null for
    // RSA-PSS and EdDSA keys, and QtNetwork may load a different OpenSSL than
    // core/ links, so its native handles are not ours to read.
    const auto* begin = reinterpret_cast<const unsigned char*>(certificateDer.constData());
    const unsigned char* position = begin;
    const std::unique_ptr<X509, decltype(&X509_free)> cert(
        d2i_X509(nullptr, &position, certificateDer.size()), &X509_free);
    if (!cert || position != begin + certificateDer.size())
        return false;
    const EVP_PKEY* key = X509_get0_pubkey(cert.get());
    if (key == nullptr)
        return false;
    // Actual modulus bits. OpenSSL's SECLEVEL estimate rounds RSA-2047 up to
    // 112 bits, so level 2 alone accepts it.
    const int bits = EVP_PKEY_get_bits(key);
    if (EVP_PKEY_is_a(key, "RSA") || EVP_PKEY_is_a(key, "RSA-PSS") || EVP_PKEY_is_a(key, "DSA")
        || EVP_PKEY_is_a(key, "DH") || EVP_PKEY_is_a(key, "DHX"))
        return bits >= 2048;
    if (EVP_PKEY_is_a(key, "EC"))
        return bits >= 224;
    return EVP_PKEY_is_a(key, "ED25519") || EVP_PKEY_is_a(key, "ED448");
}

bool tlsChainKeysAllowed(const QList<QSslCertificate>& chain)
{
    if (chain.isEmpty())
        return false;
    for (const QSslCertificate& cert : chain) {
        if (!tlsCertificateKeyAllowed(cert.toDer()))
            return false;
    }
    return true;
}

bool strongTlsBackendAvailable()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 1, 0)
    if (QSslSocket::activeBackend() != QLatin1String("openssl")
        && !QSslSocket::setActiveBackend(QStringLiteral("openssl")))
        return false;
#endif
    return QSslSocket::supportsSsl()
        && QSslSocket::sslLibraryVersionString().startsWith(QLatin1String("OpenSSL "))
        && QSslSocket::sslLibraryVersionNumber() >= 0x30000000L;
}

void applyStrongTlsPolicy(QNetworkRequest& request)
{
    // Qt 5.15's HTTP/2 path has no pre-data encrypted signal.
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);

    QSslConfiguration tls = request.sslConfiguration();
    tls.setProtocol(QSsl::TlsV1_2OrLater);
    tls.setPeerVerifyMode(QSslSocket::VerifyPeer);
    // Exchange groups, DH parameters and signature hashes. Replaces any
    // inherited level, higher or lower: one fixed, tested profile.
    tls.setBackendConfigurationOption(QByteArrayLiteral("CipherString"), QByteArrayLiteral("DEFAULT@SECLEVEL=2"));
    // The server need not send its root, so the floor has to apply to the
    // anchors too. setCaCertificates also disables on-demand root loading.
    QList<QSslCertificate> anchors;
    for (const QSslCertificate& cert : tls.caCertificates()) {
        if (tlsCertificateKeyAllowed(cert.toDer()))
            anchors.append(cert);
    }
    tls.setCaCertificates(anchors);
    // A resumed session can expose no peer chain to check.
    tls.setSessionTicket(QByteArray());
    tls.setSslOption(QSsl::SslOptionDisableSessionSharing, true);
    tls.setSslOption(QSsl::SslOptionDisableSessionPersistence, true);
    tls.setSslOption(QSsl::SslOptionDisableSessionTickets, true);
    request.setSslConfiguration(tls);
}

int useSystemTrustAnchors()
{
    QSslConfiguration tls = QSslConfiguration::defaultConfiguration();
    tls.setCaCertificates(QSslConfiguration::systemCaCertificates());
    QSslConfiguration::setDefaultConfiguration(tls);
    return int(tls.caCertificates().size());
}
