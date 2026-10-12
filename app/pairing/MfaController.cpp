#include "pairing/MfaController.h"

#include "domain/DevicePairing.h"
#include "domain/PairingStore.h"
#include "net/HttpClient.h"
#include "net/NetworkExecutor.h"

#include <KLocalizedString>

#include <QUrl>

MfaController::MfaController(NetworkExecutor& executor, PairingStore& pairingStore, Clock clock, QObject* parent)
    : QObject(parent)
    , m_executor(executor)
    , m_pairingStore(pairingStore)
    , m_clock(clock ? std::move(clock) : Clock([] { return QDateTime::currentMSecsSinceEpoch(); }))
{
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, &MfaController::refresh);
}

qint64 MfaController::now() const
{
    return m_clock();
}

bool MfaController::receive(const MfaChallenge& challenge)
{
    if (!m_tracker.add(challenge, now()))
        return false;
    refresh();
    return true;
}

QString MfaController::challengeId() const
{
    return m_current ? m_current->challenge.challengeId : QString();
}

QDateTime MfaController::issuedAt() const
{
    return m_current ? QDateTime::fromMSecsSinceEpoch(m_current->challenge.issuedAtMs) : QDateTime();
}

QString MfaController::ipAddress() const
{
    return m_current ? m_current->challenge.ipAddress : QString();
}

QString MfaController::userAgent() const
{
    return m_current ? m_current->challenge.userAgent : QString();
}

QStringList MfaController::choices() const
{
    return m_current ? m_current->choices : QStringList();
}

int MfaController::secondsRemaining() const
{
    if (!m_current)
        return 0;
    return int(std::max<qint64>(0, (m_current->expiresAtMs - now() + 999) / 1000));
}

void MfaController::refresh()
{
    const qint64 t = now();
    const QList<MfaChallengeTracker::Pending> all = m_tracker.allPending(t);
    // The shown challenge stays until answered or expired, so a newer arrival
    // cannot swap the numbers under the user's finger.
    std::optional<MfaChallengeTracker::Pending> next;
    if (m_current)
        next = m_tracker.pending(m_current->challenge.challengeId, t);
    if (!next && !all.isEmpty())
        next = all.first();
    m_current = next;
    m_pendingCount = int(all.size());
    if (m_current)
        m_timer.start();
    else
        m_timer.stop();
    emit changed();
}

void MfaController::show()
{
    m_open = true;
    if (!m_inFlight)
        setResult(QString(), false);
    refresh();
    emit showRequested();
}

void MfaController::close()
{
    m_open = false;
    if (!m_inFlight)
        setResult(QString(), false);
    emit changed();
}

void MfaController::approve(const QString& challengeId, const QString& digits)
{
    respond(challengeId, true, digits);
}

void MfaController::deny(const QString& challengeId)
{
    respond(challengeId, false, QString());
}

void MfaController::setAppLocked(bool locked)
{
    m_appLocked = locked;
}

void MfaController::clear()
{
    m_tracker.clear();
    m_open = false;
    setResult(QString(), false);
    refresh();
}

void MfaController::setResult(const QString& message, bool isError)
{
    m_resultMessage = message;
    m_resultIsError = isError;
    emit changed();
}

void MfaController::respond(const QString& requestedId, bool approve, const QString& digits)
{
    if (m_inFlight)
        return;
    if (m_appLocked) {
        setResult(i18n("Unlock KyPost to answer this sign-in request."), true);
        return;
    }
    // Look up exactly the challenge the user was shown, never "whatever is
    // current now": refresh() may already have replaced an expired one with
    // another pending challenge, which can share the same numbers.
    const std::optional<MfaChallengeTracker::Pending> target =
        requestedId.isEmpty() ? std::nullopt : m_tracker.pending(requestedId, now());
    refresh();
    if (!target) {
        setResult(i18n("This sign-in request is no longer available. It expired or was already answered."), true);
        return;
    }
    // Captured before anything below can re-enter the controller.
    const QString challengeId = target->challenge.challengeId;
    if (approve && !target->choices.contains(digits))
        return;

    const std::optional<DevicePairing> pairing = m_pairingStore.load();
    if (!pairing || pairing->deviceSecret.isEmpty()) {
        setResult(i18n("This device is not paired with a server."), true);
        return;
    }
    const PairingIdentity identity = identityOf(*pairing);
    const RelayEndpoint endpoint{ QUrl(pairing->serverBaseUrl), { pairing->deviceId, pairing->deviceSecret } };

    m_inFlight = true;
    setResult(QString(), false);

    m_executor.run(
        this,
        [endpoint, challengeId, approve, digits](HttpClient& http) {
            return MfaResponseClient(http).respond(endpoint, challengeId, approve, digits);
        },
        [this, identity, challengeId, approve](const MfaResponseResult& result) {
            m_inFlight = false;
            // An answer for an account this device no longer holds is not news.
            if (!m_pairingStore.stillCurrent(identity)) {
                emit changed();
                return;
            }
            applyResult(challengeId, approve, result);
        });
}

void MfaController::applyResult(const QString& challengeId, bool approve, const MfaResponseResult& result)
{
    bool terminal = true;
    switch (result.outcome) {
    case MfaResponseOutcome::Success:
        setResult(approve ? i18n("Sign-in approved.") : i18n("Sign-in denied."), false);
        break;
    case MfaResponseOutcome::AlreadyAnswered:
        setResult(i18n("This sign-in request was already answered."), true);
        break;
    case MfaResponseOutcome::Locked:
        setResult(i18n("That was not the number on the sign-in screen, so push approval is now locked for "
                       "this sign-in. Finish it with your authenticator app, or start again."),
                  true);
        break;
    case MfaResponseOutcome::Expired:
        setResult(i18n("Your server no longer accepts this sign-in request. It may have expired; start the "
                       "sign-in again. If this keeps happening, pair this device again."),
                  true);
        break;
    case MfaResponseOutcome::Forbidden:
        setResult(i18n("Your server does not allow this device to approve sign-ins for this account."), true);
        break;
    case MfaResponseOutcome::RateLimited:
        terminal = false;
        setResult(i18n("Your server is receiving too many requests. Wait a moment, then try again."), true);
        break;
    case MfaResponseOutcome::Failure:
        terminal = false;
        setResult(i18n("Could not reach your server. The request is still open; try again."), true);
        break;
    }
    if (terminal)
        m_tracker.markAnswered(challengeId);
    refresh();
}
