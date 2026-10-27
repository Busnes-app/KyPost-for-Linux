#pragma once

#include <QByteArray>
#include <QList>
#include <QSslCertificate>

class QNetworkRequest;

// The one TLS profile every app-owned HTTPS request uses; HttpClient applies
// it to every verb. docs/STRONG-CRYPTO.md is the user-facing statement.

// Literal public-key floor for one certificate: RSA, RSA-PSS, DSA and DH need
// 2048 actual bits, EC 224, and Ed25519/Ed448 pass. Unknown key types and DER
// that does not parse exactly are refused. Trust, names and signatures stay
// Qt/OpenSSL's job; this only rejects keys they would otherwise accept.
bool tlsCertificateKeyAllowed(const QByteArray& certificateDer);

// Every certificate in `chain` passes. An empty chain does not.
bool tlsChainKeysAllowed(const QList<QSslCertificate>& chain);

// QtNetwork's runtime TLS library is OpenSSL >= 3, the backend whose options
// applyStrongTlsPolicy() relies on. Not the libcrypto core/ links.
bool strongTlsBackendAvailable();

// TLS >= 1.2, VerifyPeer, OpenSSL SECLEVEL=2, trust anchors filtered by the
// key floor (an empty list stays empty), no session resumption, and HTTP/1.1
// only, so QNetworkReply::encrypted fires before any request byte on Qt 5 too.
void applyStrongTlsPolicy(QNetworkRequest& request);

// Startup: installs the OS trust store as the default anchor list, replacing
// Qt's on-demand root loading, which would bypass the anchor filter. Returns
// the number of anchors found; zero means every HTTPS request will fail.
int useSystemTrustAnchors();
