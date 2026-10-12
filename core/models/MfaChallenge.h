#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <optional>

// A push sign-in challenge, from the data map of push_mfa_handlers.go's
// dispatchPushChallenge. The same map arrives by UnifiedPush and by the pull
// queue; every value is a string on the wire.
struct MfaChallenge
{
    QString challengeId;  // 48 lowercase hex (24 random bytes, hex.EncodeToString)
    qint64 issuedAtMs = 0;
    QString matchDigits;  // two ASCII digits
    QStringList decoyDigits; // exactly two, distinct from each other and from matchDigits
    QString ipAddress;    // optional, controls stripped, at most kMaxContextLength
    QString userAgent;    // optional, same

    static constexpr int kMaxContextLength = 120;

    // nullopt unless data.type is "mfa_challenge" AND every required field is
    // well formed. A malformed challenge is dropped, never shown as mail and
    // never offered as a partial approval.
    static std::optional<MfaChallenge> fromPushData(const QJsonObject& data);

    static bool isChallengeType(const QJsonObject& data);

    bool operator==(const MfaChallenge&) const = default;
};
