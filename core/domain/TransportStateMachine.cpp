#include "domain/TransportStateMachine.h"

#include "domain/PushRepository.h"

TransportStateMachine::TransportStateMachine(PushRepository& pushRepository, QObject* parent,
                                               int pollIntervalMs, std::function<qint64()> now)
    : QObject(parent)
    , m_pushRepository(pushRepository)
    , m_now(std::move(now))
{
    m_pollTimer.setInterval(pollIntervalMs);
    connect(&m_pollTimer, &QTimer::timeout, this, &TransportStateMachine::onPollTimer);
    // Checks, not pulls: a pull happens only once the silence is long enough.
    m_silenceTimer.setInterval(15 * 60 * 1000);
    connect(&m_silenceTimer, &QTimer::timeout, this, &TransportStateMachine::checkPushSilence);

    // m_tier already defaults to Polling (see header); there is no "previous"
    // tier to leave, so start its timer directly rather than going through
    // enterTier(), which only acts on an actual transition.
    m_pollTimer.start();
}

void TransportStateMachine::setDistributorAvailable(bool available)
{
    m_distributorAvailable = available;
    enterTier(available ? TransportTier::Distributor : TransportTier::Polling);
}

TransportTier TransportStateMachine::currentTier() const
{
    return m_tier;
}

void TransportStateMachine::notePushReceived()
{
    m_lastHeardMs = m_now();
}

bool TransportStateMachine::checkPushSilence()
{
    const qint64 now = m_now();
    if (m_tier != TransportTier::Distributor || now - m_lastHeardMs < kPushSilenceMs)
        return false;
    m_lastHeardMs = now;
    // Push may have delivered most of this backlog already; anything queued
    // before twice the threshold is too old to alert on (Android's bound).
    emit pollTick(m_pushRepository.pullOnce(now - 2 * kPushSilenceMs));
    emit pushSilenceBackstop();
    return true;
}

void TransportStateMachine::onPollTimer()
{
    const QVector<PushNotification> delivered = m_pushRepository.pullOnce();
    emit pollTick(delivered);
}

void TransportStateMachine::enterTier(TransportTier tier)
{
    if (tier == m_tier)
        return;
    m_tier = tier;

    if (tier == TransportTier::Polling) {
        m_pollTimer.start();
        m_silenceTimer.stop();
    } else {
        m_pollTimer.stop();
        // Registration is the last moment the relay was proven reachable.
        m_lastHeardMs = m_now();
        m_silenceTimer.start();
    }

    emit tierChanged(tier);
}
