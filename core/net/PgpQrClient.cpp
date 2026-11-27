#include "net/PgpQrClient.h"

#include "net/ContactSyncClient.h"
#include "net/HttpClient.h"
#include "net/RelayAuth.h"
#include "pgp/PgpFingerprint.h"

#include <QJsonObject>

PgpQrClient::PgpQrClient(HttpClient& httpClient)
    : m_httpClient(httpClient)
{
}

PgpQrTokenResult PgpQrClient::fetchToken(const QUrl& serverBaseUrl, const RelayAuth& auth) const
{
    const HttpClient::HttpResult result = m_httpClient.get(
        joinUrlPath(serverBaseUrl, QStringLiteral("api/pgp/qr/token")), {}, auth.headerItems());

    PgpQrTokenResult out;
    out.statusCode = result.statusCode;

    if (result.error.has_value()) {
        out.error = result.error;
        out.detail = !result.detail.isEmpty()
            ? result.detail
            : QStringLiteral("PGP QR token fetch failed with status %1").arg(result.statusCode);
        return out;
    }

    QString errorString;
    const std::optional<QJsonObject> decoded = decodeJsonObject(result.body, &errorString);
    if (!decoded.has_value()) {
        out.error = NetworkError::Decoding;
        out.detail = QStringLiteral("Failed to decode PGP QR token response: %1").arg(errorString);
        return out;
    }

    const QJsonObject obj = *decoded;
    out.token = obj.value(QStringLiteral("token")).toString();
    out.expiresAt = obj.value(QStringLiteral("expiresAt")).toString();
    out.url = obj.value(QStringLiteral("url")).toString();
    return out;
}

PgpQrKeyResult PgpQrClient::fetchKey(const QUrl& qrUrl, const HttpClient::RedirectValidator& redirectValidator) const
{
    const HttpClient::HttpResult result = m_httpClient.get(qrUrl, {}, {}, redirectValidator);

    PgpQrKeyResult out;
    out.statusCode = result.statusCode;

    if (result.error.has_value()) {
        out.error = result.error;
        out.detail = !result.detail.isEmpty()
            ? result.detail
            : QStringLiteral("PGP QR key fetch failed with status %1").arg(result.statusCode);
        return out;
    }

    QString errorString;
    const std::optional<QJsonObject> decoded = decodeJsonObject(result.body, &errorString);
    if (!decoded.has_value()) {
        out.error = NetworkError::Decoding;
        out.detail = QStringLiteral("Failed to decode PGP QR key response: %1").arg(errorString);
        return out;
    }

    const QJsonObject obj = *decoded;
    out.name = obj.value(QStringLiteral("name")).toString();
    out.fingerprint = obj.value(QStringLiteral("fingerprint")).toString();
    out.publicKey = obj.value(QStringLiteral("publicKey")).toString();
    if (obj.value(QStringLiteral("contactCard")).isObject())
        out.contactCard = ContactWire::contactFromJson(obj.value(QStringLiteral("contactCard")).toObject());
    return out;
}

namespace {

// Full v4 or v6 primary fingerprints; reject non-ASCII before converting.
QString qrFingerprint(const QString& text)
{
    return QString::fromLatin1(text.toLatin1()) == text ? normalizedFingerprint(text.toLatin1()) : QString();
}

} // namespace

QString pgpQrPayload(const QString& pickupUrl, const QString& localFingerprint)
{
    const QString url = QUrl(pickupUrl).adjusted(QUrl::RemoveFragment).toString(QUrl::FullyEncoded);
    const QString fingerprint = qrFingerprint(localFingerprint);
    return fingerprint.isEmpty() ? url : url + QStringLiteral("#fp=") + fingerprint;
}

PgpQrCode parsePgpQrCode(const QString& text)
{
    const QUrl url(text);
    const QString fragment = url.fragment(QUrl::FullyDecoded);
    PgpQrCode code;
    code.keyUrl = url.adjusted(QUrl::RemoveFragment);
    code.hasFingerprint = fragment.startsWith(QStringLiteral("fp="));
    if (code.hasFingerprint)
        code.fingerprint = qrFingerprint(fragment.mid(3));
    return code;
}
