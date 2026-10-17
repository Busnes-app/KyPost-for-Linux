#pragma once

#include "domain/DevicePairing.h"

#include <QDateTime>
#include <QObject>
#include <QVariantList>

#include <functional>

class NetworkExecutor;
class PairingStore;
struct CancelScheduledResult;
struct ScheduledListResult;

// The "Scheduled" QML singleton: the send-later presets, and the mail the relay
// is holding (GET /api/scheduled), with cancel. Cancel returns a message to
// Drafts; only a waiting one can be canceled.
class ScheduledController : public QObject
{
    Q_OBJECT
    // [{id, subject, to, when, status, reason, cancellable}], newest send time first.
    Q_PROPERTY(QVariantList items READ items NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    // The last load's or cancel's outcome, if worth saying.
    Q_PROPERTY(QString message READ message NOTIFY changed)

public:
    using Clock = std::function<QDateTime()>;

    ScheduledController(NetworkExecutor& executor, PairingStore& pairingStore, Clock clock = {},
                        QObject* parent = nullptr);

    QVariantList items() const { return m_items; }
    bool busy() const { return m_busy; }
    QString message() const { return m_message; }

    // [{label, atMs}] for the composer's "Send later".
    Q_INVOKABLE QVariantList presets() const;
    // A custom pick, "yyyy-MM-dd" and "HH:mm" in local time: ms since the
    // epoch, or 0 when it does not parse or is outside the picker's window.
    Q_INVOKABLE qint64 customTime(const QString& date, const QString& time) const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void cancel(const QString& id);

    // Pairing changed: the list belonged to the previous account.
    void clear();
    // Locked: the list is dropped and nothing is loaded or canceled.
    void setAppLocked(bool locked);

signals:
    void changed();

private:
    void load();
    void applyList(const ScheduledListResult& result);
    void applyCancel(const CancelScheduledResult& result);

    NetworkExecutor& m_executor;
    PairingStore& m_pairingStore;
    Clock m_clock;
    PairingIdentity m_requestedBy;
    QVariantList m_items;
    bool m_busy = false;
    bool m_appLocked = false;
    QString m_message;
};
