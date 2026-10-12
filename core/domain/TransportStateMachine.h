#pragma once

#include "models/PushNotification.h"

#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QVector>

#include <functional>

class PushRepository;

enum class TransportTier { Distributor, Polling };

// Owns the two-tier push-delivery fallback decision and the poll timer behind
// it: distributor present -> KUnifiedPush; otherwise -> 90s polling against
// the relay.
//
// Linux_QT_Client_Plan.md's "Transport state machine (shared)" section
// originally specified a third tier between those two -- an embedded ntfy
// subscriber that made the client its own distributor. It was cut (2026-07-26)
// because both of its justifications had expired. It existed primarily as
// "the whole UT v1 story" (that plan section's own words) for an Ubuntu Touch
// target that has since been deferred, and secondarily as a universal
// fallback for desktops with no distributor installed -- a case the Polling
// tier below already covers without handing a third party anything. Since the
// subscriber tier was foreground-only, its entire remaining benefit over
// Polling was notification latency while the app was already open on screen,
// paid for with the sender and subject of every mail going in the clear to
// ntfy.sh (the backend POSTs {"title","body"} to whatever URL is registered
// as the deviceToken) plus a topic string that doubled as a bearer secret.
// Distributor-tier users are unaffected: that path never used any of it.
//
// This class never links KUnifiedPush itself (core/ links only
// Qt6::Core/Network/Sql) -- "distributor available" is externally-reported
// state from app-layer code, not detected here.
class TransportStateMachine : public QObject
{
    Q_OBJECT
public:
    // pollIntervalMs is a constructor-overridable knob, defaulted to the plan
    // doc's real 90s cadence, so tests don't have to wait out 90 real seconds
    // to exercise the polling tier's timer-driven fetch path.
    // now: the clock the push-silence backstop reads, injectable for tests.
    TransportStateMachine(PushRepository& pushRepository, QObject* parent = nullptr,
                           int pollIntervalMs = 90000,
                           std::function<qint64()> now = &QDateTime::currentMSecsSinceEpoch);

    // Distributor silence after which one pull is made anyway, as Android.
    static constexpr qint64 kPushSilenceMs = 3LL * 60 * 60 * 1000;

    // Called by app-layer code when the KUnifiedPush distributor path is
    // confirmed working (endpoint acquired, registered) or lost (no
    // distributor installed, or the connector reports failure). true ->
    // Distributor tier; false -> Polling.
    void setDistributorAvailable(bool available);

    TransportTier currentTier() const;

    // A message arrived through the distributor.
    void notePushReceived();

    // Backstop for a distributor that stopped delivering: in the Distributor
    // tier, once kPushSilenceMs pass with no push, pull once and emit
    // pushSilenceBackstop. Runs on a timer and when the app comes forward.
    // True if it pulled.
    bool checkPushSilence();

signals:
    void tierChanged(TransportTier tier);
    // Emitted after each PushRepository::pullOnce() call, polling or backstop
    // (whether or not it returned anything) -- the UI wires this to a
    // "last checked at" indicator.
    void pollTick(QVector<PushNotification> delivered);
    // The backstop pulled; mail may be stale too.
    void pushSilenceBackstop();

private slots:
    void onPollTimer();

private:
    void enterTier(TransportTier tier);

    PushRepository& m_pushRepository;
    QTimer m_pollTimer; // 90s interval, per the plan doc's stated cadence
    QTimer m_silenceTimer; // Distributor tier only
    std::function<qint64()> m_now;
    qint64 m_lastHeardMs = 0; // last push, distributor registration, or backstop
    bool m_distributorAvailable = false;
    TransportTier m_tier = TransportTier::Polling;
};

// Both types cross a signal boundary above (tierChanged, pollTick) --
// QSignalSpy/queued connections need them registered as Qt metatypes to
// capture the arguments, which plain "enum class"/QVector<CustomStruct>
// don't get for free.
Q_DECLARE_METATYPE(TransportTier)
Q_DECLARE_METATYPE(QVector<PushNotification>)
