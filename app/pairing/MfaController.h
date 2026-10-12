#pragma once

#include "domain/MfaChallengeTracker.h"
#include "net/MfaResponseClient.h"

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <functional>

class NetworkExecutor;
class PairingStore;

// The "Mfa" QML singleton: holds the sign-in challenges this device may
// answer and sends the answer. Approve and deny are refused while the app is
// locked; with no app lock, tapping the matching number is the confirmation.
class MfaController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool open READ isOpen NOTIFY changed)
    Q_PROPERTY(bool hasChallenge READ hasChallenge NOTIFY changed)
    Q_PROPERTY(QDateTime issuedAt READ issuedAt NOTIFY changed)
    Q_PROPERTY(QString ipAddress READ ipAddress NOTIFY changed)
    Q_PROPERTY(QString userAgent READ userAgent NOTIFY changed)
    Q_PROPERTY(QStringList choices READ choices NOTIFY changed)
    Q_PROPERTY(int secondsRemaining READ secondsRemaining NOTIFY changed)
    Q_PROPERTY(int pendingCount READ pendingCount NOTIFY changed)
    Q_PROPERTY(bool inFlight READ inFlight NOTIFY changed)
    Q_PROPERTY(QString resultMessage READ resultMessage NOTIFY changed)
    Q_PROPERTY(bool resultIsError READ resultIsError NOTIFY changed)

public:
    using Clock = std::function<qint64()>;

    MfaController(NetworkExecutor& executor, PairingStore& pairingStore, Clock clock = {},
                  QObject* parent = nullptr);

    // From the push or pull path. True when it is new and answerable, which
    // is when the caller should notify.
    bool receive(const MfaChallenge& challenge);

    bool isOpen() const { return m_open; }
    bool hasChallenge() const { return m_current.has_value(); }
    QDateTime issuedAt() const;
    QString ipAddress() const;
    QString userAgent() const;
    QStringList choices() const;
    int secondsRemaining() const;
    int pendingCount() const { return m_pendingCount; }
    bool inFlight() const { return m_inFlight; }
    QString resultMessage() const { return m_resultMessage; }
    bool resultIsError() const { return m_resultIsError; }

    Q_INVOKABLE void show();
    Q_INVOKABLE void close();
    // digits must be one of choices(); it is sent as tapped, never corrected.
    Q_INVOKABLE void approve(const QString& digits);
    Q_INVOKABLE void deny();

    void setAppLocked(bool locked);
    // Pairing changed: every challenge belonged to the previous account.
    void clear();
    // Drops expired challenges and moves the countdown. Driven by a timer.
    void refresh();

signals:
    void changed();
    // The user asked to see the page (notification View action); the roots
    // raise the window.
    void showRequested();

private:
    void respond(bool approve, const QString& digits);
    void applyResult(const QString& challengeId, bool approve, const MfaResponseResult& result);
    void setResult(const QString& message, bool isError);
    qint64 now() const;

    NetworkExecutor& m_executor;
    PairingStore& m_pairingStore;
    Clock m_clock;
    MfaChallengeTracker m_tracker;
    std::optional<MfaChallengeTracker::Pending> m_current;
    int m_pendingCount = 0;
    QTimer m_timer;
    bool m_open = false;
    bool m_appLocked = false;
    bool m_inFlight = false;
    QString m_resultMessage;
    bool m_resultIsError = false;
};
