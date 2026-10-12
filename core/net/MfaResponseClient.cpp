#include "net/MfaResponseClient.h"

#include "net/HttpClient.h"

#include <QJsonDocument>
#include <QJsonObject>

MfaResponseClient::MfaResponseClient(HttpClient& httpClient)
    : m_httpClient(httpClient)
{
}

MfaResponseResult MfaResponseClient::respond(const RelayEndpoint& endpoint, const QString& challengeId,
                                             bool approve, const QString& matchDigits) const
{
    QJsonObject body;
    body[QStringLiteral("challengeId")] = challengeId;
    body[QStringLiteral("approve")] = approve;
    body[QStringLiteral("matchDigits")] = approve ? matchDigits : QString();

    const HttpClient::HttpResult result =
        m_httpClient.post(joinUrlPath(endpoint.serverBaseUrl, QStringLiteral("api/mfa/push/respond")), {}, body,
                          endpoint.auth.headerItems());

    const QJsonObject reply = QJsonDocument::fromJson(result.body).object();
    MfaResponseResult out;
    out.status = reply.value(QStringLiteral("status")).toString();

    if (!result.error.has_value()) {
        // Never report a decision the relay did not confirm.
        out.outcome = reply.value(QStringLiteral("ok")).toBool() ? MfaResponseOutcome::Success
                                                                  : MfaResponseOutcome::Failure;
        return out;
    }
    switch (result.statusCode) {
    case 409:
        out.outcome = MfaResponseOutcome::AlreadyAnswered;
        break;
    case 429:
        out.outcome = out.status == QLatin1String("locked") ? MfaResponseOutcome::Locked
                                                            : MfaResponseOutcome::RateLimited;
        break;
    case 401:
        out.outcome = MfaResponseOutcome::Expired;
        break;
    case 403:
        out.outcome = MfaResponseOutcome::Forbidden;
        break;
    default:
        out.outcome = MfaResponseOutcome::Failure;
        break;
    }
    return out;
}
