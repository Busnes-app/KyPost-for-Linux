#include "net/NativeRegistrationClient.h"

#include "net/HttpClient.h"

#include <QJsonObject>
#include <QSysInfo>

namespace {

bool isApprovalCode(const QString& code)
{
    if (code.size() != 6)
        return false;
    for (const QChar c : code) {
        if (c < QLatin1Char('0') || c > QLatin1Char('9'))
            return false;
    }
    return true;
}

// Seconds from a delta-seconds Retry-After; 0 for an HTTP-date or anything else.
int retryAfterSeconds(const QList<QPair<QString, QString>>& headers)
{
    for (const auto& header : headers) {
        if (header.first.compare(QLatin1String("Retry-After"), Qt::CaseInsensitive) != 0)
            continue;
        bool ok = false;
        const int seconds = header.second.trimmed().toInt(&ok);
        return ok && seconds > 0 ? seconds : 0;
    }
    return 0;
}

// Reconstructs {scheme}://{host}[:port]/api/notifications/native/pull from
// the registration endpoint's authority, for when the server response omits
// pullEndpoint. Mirrors kypost-for-Mac's resolvedPullEndpoint(srv:).
QString derivePullEndpoint(const QUrl& registrationEndpoint)
{
    QUrl pull;
    pull.setScheme(registrationEndpoint.scheme());
    pull.setHost(registrationEndpoint.host());
    if (registrationEndpoint.port() != -1)
        pull.setPort(registrationEndpoint.port());
    pull.setPath(QStringLiteral("/api/notifications/native/pull"));
    return pull.toString();
}

} // namespace

NativeRegistrationClient::NativeRegistrationClient(HttpClient& httpClient)
    : m_httpClient(httpClient)
{
}

NativeRegistrationResult NativeRegistrationClient::registerDevice(
    const QUrl& registrationEndpoint, const QString& subscriberId, const QString& pairingToken,
    const QString& deviceToken, const QString& deviceId, const QString& deviceName,
    const WebPushSubscription& pushKeys) const
{
    if (pushKeys.p256dh.size() != 65 || pushKeys.auth.size() != 16) {
        NativeRegistrationResult refused;
        refused.detail = QStringLiteral("No push encryption keys to register.");
        return refused;
    }

    const auto b64url = [](const QByteArray& raw) {
        return QString::fromLatin1(raw.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
    };

    QJsonObject body;
    body[QStringLiteral("subscriberId")] = subscriberId;
    body[QStringLiteral("pairingToken")] = pairingToken;
    body[QStringLiteral("deviceToken")] = deviceToken;
    if (!deviceId.isEmpty())
        body[QStringLiteral("deviceId")] = deviceId;
    if (!deviceName.isEmpty())
        body[QStringLiteral("deviceName")] = deviceName;
    body[QStringLiteral("platform")] = QStringLiteral("linux");
    body[QStringLiteral("transport")] = QStringLiteral("unifiedpush");
    // Shown on the web approval prompt next to deviceName. Display only.
    const QString osVersion = QSysInfo::prettyProductName().trimmed();
    if (!osVersion.isEmpty())
        body[QStringLiteral("osVersion")] = osVersion;
    body[QStringLiteral("p256dh")] = b64url(pushKeys.p256dh);
    body[QStringLiteral("auth")] = b64url(pushKeys.auth);

    // No query-param auth on this endpoint, unlike every other Relay
    // endpoint in this batch — every credential rides in the JSON body.
    const HttpClient::HttpResult result = m_httpClient.post(registrationEndpoint, {}, body);

    NativeRegistrationResult out;
    out.peerSpkiSha256 = result.peerSpkiSha256;
    out.certificatePinVerified = result.certificatePinVerified;
    out.statusCode = result.statusCode;

    if (result.error.has_value()) {
        switch (*result.error) {
        case NetworkError::Unauthorized:
            out.outcome = RegistrationOutcome::Unauthorized;
            return out;
        case NetworkError::ServiceUnavailable:
            out.outcome = RegistrationOutcome::BackendMisconfigured;
            return out;
        default:
            out.outcome = RegistrationOutcome::Failure;
            out.detail = !result.detail.isEmpty() ? result.detail
                                                    : QStringLiteral("Registration request failed with status %1")
                                                          .arg(result.statusCode);
            return out;
        }
    }

    QString errorString;
    const std::optional<QJsonObject> decoded = decodeJsonObject(result.body, &errorString);
    if (!decoded.has_value()) {
        out.outcome = RegistrationOutcome::Failure;
        out.detail = QStringLiteral("Failed to decode registration response: %1").arg(errorString);
        return out;
    }

    const QJsonObject json = *decoded;

    // 202: the server holds this device until the user approves it on the web
    // page that showed the code. The credential works only for the status
    // endpoint, so it is handed back to the caller and not persisted.
    if (result.statusCode == 202) {
        NativeRegistrationResponse pending;
        pending.deviceId = json.value(QStringLiteral("deviceId")).toString();
        pending.deviceSecret = json.value(QStringLiteral("deviceSecret")).toString();
        pending.approvalCode = json.value(QStringLiteral("approvalCode")).toString();
        pending.expiresAt = QDateTime::fromString(json.value(QStringLiteral("expiresAt")).toString(), Qt::ISODate);
        pending.statusEndpoint = json.value(QStringLiteral("statusEndpoint")).toString();
        pending.pollIntervalSeconds = json.value(QStringLiteral("pollIntervalSeconds")).toInt();
        if (json.value(QStringLiteral("status")).toString() != QLatin1String("pending") || pending.deviceId.isEmpty()
            || pending.deviceSecret.isEmpty() || !isApprovalCode(pending.approvalCode)) {
            out.outcome = RegistrationOutcome::Failure;
            out.detail = QStringLiteral("The server did not return a usable pending device.");
            return out;
        }
        out.outcome = RegistrationOutcome::PendingApproval;
        out.response = pending;
        return out;
    }

    NativeRegistrationResponse response;
    response.ok = json.value(QStringLiteral("ok")).toBool();
    response.synced = json.value(QStringLiteral("synced")).toBool();
    response.deviceId = json.value(QStringLiteral("deviceId")).toString();
    response.deviceSecret = json.value(QStringLiteral("deviceSecret")).toString();
    response.devices = json.value(QStringLiteral("devices")).toInt();
    response.deliveryMode = json.value(QStringLiteral("deliveryMode")).toString();
    response.pullEndpoint = json.value(QStringLiteral("pullEndpoint")).toString();
    if (response.pullEndpoint.isEmpty())
        response.pullEndpoint = derivePullEndpoint(registrationEndpoint);
    response.transport = json.value(QStringLiteral("transport")).toString();

    // A 200 carrying *any* JSON object used to count as Success: `ok` was
    // parsed and never read, and an empty deviceId/deviceSecret was persisted
    // as-is. Combined with the registration URL's path being unconstrained,
    // that let one deep link -- naming the user's own real mail server, so
    // the confirm dialog showed nothing amiss -- POST to an unrelated
    // same-origin endpoint (the relay's unauthenticated /api/health answers
    // POST with a JSON object) and overwrite the working device credential
    // with empty strings. Every later relay call then 401s while the UI
    // still reports "Paired", and the pairing token has been consumed.
    // Refuse before the caller can persist anything.
    if (!response.ok || response.deviceId.isEmpty() || response.deviceSecret.isEmpty()) {
        out.outcome = RegistrationOutcome::Failure;
        out.detail = QStringLiteral("The server did not return a usable device credential.");
        return out;
    }

    out.outcome = RegistrationOutcome::Success;
    out.response = response;
    return out;
}

PairingStatusResult NativeRegistrationClient::pairingStatus(const QUrl& statusEndpoint,
                                                            const RelayAuth& pendingAuth, const QByteArray& pin) const
{
    const HttpClient::HttpResult result =
        m_httpClient.getPinnedTo({ pin, statusEndpoint }, statusEndpoint, pendingAuth.headerItems());

    PairingStatusResult out;
    out.peerSpkiSha256 = result.peerSpkiSha256;
    out.certificatePinVerified = result.certificatePinVerified;

    if (result.error.has_value()) {
        switch (*result.error) {
        case NetworkError::CertificateMismatch:
            out.status = PairingStatusResult::Status::CertificateMismatch;
            return out;
        case NetworkError::RedirectRefused:
        case NetworkError::ResponseTooLarge:
            out.status = PairingStatusResult::Status::Refused;
            return out;
        default:
            break;
        }
        // Denied, expired and unknown are deliberately one answer.
        out.status = result.statusCode == 410 ? PairingStatusResult::Status::Rejected
                                              : PairingStatusResult::Status::Retry;
        out.retryAfterSeconds = retryAfterSeconds(result.headers);
        return out;
    }

    const std::optional<QJsonObject> decoded = decodeJsonObject(result.body);
    if (!decoded.has_value())
        return out; // Retry
    const QJsonObject& json = *decoded;
    const QString status = json.value(QStringLiteral("status")).toString();

    if (result.statusCode == 202 && status == QLatin1String("pending")) {
        out.status = PairingStatusResult::Status::Pending;
        out.response.expiresAt = QDateTime::fromString(json.value(QStringLiteral("expiresAt")).toString(), Qt::ISODate);
        out.response.pollIntervalSeconds = json.value(QStringLiteral("pollIntervalSeconds")).toInt();
        return out;
    }

    if (result.statusCode == 200 && status == QLatin1String("approved") && json.value(QStringLiteral("ok")).toBool()) {
        NativeRegistrationResponse& approved = out.response;
        approved.ok = true;
        approved.synced = json.value(QStringLiteral("synced")).toBool();
        approved.deviceId = json.value(QStringLiteral("deviceId")).toString();
        approved.deliveryMode = json.value(QStringLiteral("deliveryMode")).toString();
        approved.pullEndpoint = json.value(QStringLiteral("pullEndpoint")).toString();
        approved.transport = json.value(QStringLiteral("transport")).toString();
        if (!approved.deviceId.isEmpty())
            out.status = PairingStatusResult::Status::Approved;
    }
    return out; // Retry unless set above
}
