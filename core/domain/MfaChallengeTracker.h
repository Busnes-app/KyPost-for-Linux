#pragma once

#include "models/MfaChallenge.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <optional>

// Sign-in challenges this device may still answer. Memory only: a challenge
// lives five minutes, and a file would be one more thing to wipe.
class MfaChallengeTracker
{
public:
    // mfa.go's challengeTTL. The relay sends no expiry; it is issuedAt + this.
    static constexpr qint64 kLifetimeMs = 5 * 60 * 1000;
    // Tolerated clock skew for an issuedAt ahead of this device's clock.
    static constexpr qint64 kMaxFutureSkewMs = 2 * 60 * 1000;
    static constexpr int kMaxPending = 8;

    struct Pending
    {
        MfaChallenge challenge;
        qint64 expiresAtMs = 0;
        QStringList choices; // match + decoys, shuffled once on arrival
    };

    // False for a duplicate (pending or already answered), an expired
    // challenge, or one issued implausibly far in the future. Past the cap,
    // the oldest pending challenge is evicted.
    bool add(const MfaChallenge& challenge, qint64 nowMs);

    // Answered challenges stay remembered until they expire, so a replayed
    // push cannot reopen a decision.
    void markAnswered(const QString& challengeId);

    std::optional<Pending> pending(const QString& challengeId, qint64 nowMs);
    // Newest first.
    QList<Pending> allPending(qint64 nowMs);

    void clear();

private:
    struct Entry
    {
        Pending pending;
        bool answered = false;
    };

    void prune(qint64 nowMs);

    QList<Entry> m_entries;
};
