#include "domain/MfaChallengeTracker.h"

#include <QRandomGenerator>

#include <algorithm>

bool MfaChallengeTracker::add(const MfaChallenge& challenge, qint64 nowMs)
{
    prune(nowMs);
    if (challenge.issuedAtMs > nowMs + kMaxFutureSkewMs)
        return false;
    // Counted from whichever is earlier, so a device clock running behind the
    // relay's cannot stretch the window past what the relay will accept.
    const qint64 expiresAtMs = std::min(challenge.issuedAtMs, nowMs) + kLifetimeMs;
    if (nowMs >= expiresAtMs)
        return false;
    for (const Entry& e : std::as_const(m_entries)) {
        if (e.pending.challenge.challengeId == challenge.challengeId)
            return false;
    }

    QStringList choices = challenge.decoyDigits;
    choices.append(challenge.matchDigits);
    std::shuffle(choices.begin(), choices.end(), *QRandomGenerator::global());
    m_entries.append({ Pending{ challenge, expiresAtMs, choices }, false });

    const auto pendingCount = [this] {
        return std::count_if(m_entries.cbegin(), m_entries.cend(), [](const Entry& e) { return !e.answered; });
    };
    while (pendingCount() > kMaxPending) {
        auto oldest = m_entries.end();
        for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
            if (!it->answered
                && (oldest == m_entries.end()
                    || it->pending.challenge.issuedAtMs < oldest->pending.challenge.issuedAtMs))
                oldest = it;
        }
        m_entries.erase(oldest);
    }
    return true;
}

void MfaChallengeTracker::markAnswered(const QString& challengeId)
{
    for (Entry& e : m_entries) {
        if (e.pending.challenge.challengeId == challengeId)
            e.answered = true;
    }
}

std::optional<MfaChallengeTracker::Pending> MfaChallengeTracker::pending(const QString& challengeId, qint64 nowMs)
{
    prune(nowMs);
    for (const Entry& e : std::as_const(m_entries)) {
        if (!e.answered && e.pending.challenge.challengeId == challengeId)
            return e.pending;
    }
    return std::nullopt;
}

QList<MfaChallengeTracker::Pending> MfaChallengeTracker::allPending(qint64 nowMs)
{
    prune(nowMs);
    QList<Pending> out;
    for (const Entry& e : std::as_const(m_entries)) {
        if (!e.answered)
            out.append(e.pending);
    }
    std::stable_sort(out.begin(), out.end(), [](const Pending& a, const Pending& b) {
        return a.challenge.issuedAtMs > b.challenge.issuedAtMs;
    });
    return out;
}

void MfaChallengeTracker::clear()
{
    m_entries.clear();
}

void MfaChallengeTracker::prune(qint64 nowMs)
{
    m_entries.removeIf([nowMs](const Entry& e) { return nowMs >= e.pending.expiresAtMs; });
}
