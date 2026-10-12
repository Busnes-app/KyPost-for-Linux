#pragma once

#include "net/RelayAuth.h"

#include <QString>
#include <QUrl>

class HttpClient;

// One per answer push_mfa_handlers.go's handlePushRespond can give.
enum class MfaResponseOutcome
{
    Success,         // 200 {"ok":true,"status"}
    AlreadyAnswered, // 409 -- another answer won; status says which
    Locked,          // 429 status "locked" -- wrong number, push is shut for this sign-in
    RateLimited,     // 429 otherwise -- device or account throttle; retryable
    Expired,         // 401 -- challenge gone, or this device's credentials refused
    Forbidden,       // 403 -- not this account's challenge, or push approval off
    Failure,         // anything else, including transport errors; retryable
};

struct MfaResponseResult
{
    MfaResponseOutcome outcome = MfaResponseOutcome::Failure;
    QString status; // body "status" when present
};

// POST {serverBaseUrl}/api/mfa/push/respond with device auth headers and
// {challengeId, approve, matchDigits}. matchDigits is whatever the user
// tapped; the relay checks it, so a deny sends it empty.
class MfaResponseClient
{
public:
    explicit MfaResponseClient(HttpClient& httpClient);

    MfaResponseResult respond(const RelayEndpoint& endpoint, const QString& challengeId, bool approve,
                              const QString& matchDigits) const;

private:
    HttpClient& m_httpClient;
};
