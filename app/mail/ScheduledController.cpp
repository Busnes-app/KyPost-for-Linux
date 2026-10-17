#include "mail/ScheduledController.h"

#include "domain/PairingStore.h"
#include "domain/SendLater.h"
#include "net/NetworkExecutor.h"
#include "net/RelayAuth.h"
#include "net/RelayMailSource.h"
#include "pgp/PgpMimeWriter.h"

#include <KLocalizedString>

#include <QLocale>
#include <QTimeZone>
#include <QVariantMap>

namespace {

QString statusText(const QString& status)
{
    if (status == QStringLiteral("scheduled"))
        return i18n("Waiting");
    if (status == QStringLiteral("sending"))
        return i18n("Sending");
    if (status == QStringLiteral("returning"))
        return i18n("Going back to Drafts");
    if (status == QStringLiteral("returned"))
        return i18n("Returned to Drafts");
    if (status == QStringLiteral("sent") || status == QStringLiteral("queued"))
        return i18n("Sent");
    if (status == QStringLiteral("uncertain"))
        return i18n("Delivery not confirmed");
    if (status == QStringLiteral("failed"))
        return i18n("Failed");
    return i18n("Unknown state");
}

QString whenText(const QDateTime& at, const QDateTime& now)
{
    const QDateTime local = at.toLocalTime();
    const QString time = QLocale().toString(local.time(), QLocale::ShortFormat);
    return local.date() == now.toLocalTime().date() ? time : QLocale().toString(local, QStringLiteral("ddd ")) + time;
}

} // namespace

ScheduledController::ScheduledController(NetworkExecutor& executor, PairingStore& pairingStore, Clock clock,
                                         QObject* parent)
    : QObject(parent)
    , m_executor(executor)
    , m_pairingStore(pairingStore)
    , m_clock(clock ? std::move(clock) : Clock([] { return QDateTime::currentDateTime(); }))
{
}

QVariantList ScheduledController::presets() const
{
    const QDateTime now = m_clock();
    QVariantList out;
    for (const SendLaterChoice& choice : sendLaterChoices(now)) {
        const QString when = whenText(choice.at, now);
        QString label;
        switch (choice.preset) {
        case SendLaterPreset::LaterToday:
            label = i18n("Later today, %1", when);
            break;
        case SendLaterPreset::ThisEvening:
            label = i18n("This evening, %1", when);
            break;
        case SendLaterPreset::Tomorrow:
            label = i18n("Tomorrow, %1", when);
            break;
        case SendLaterPreset::NextWeek:
            label = i18n("Next week, %1", when);
            break;
        }
        out.append(QVariantMap{ { QStringLiteral("label"), label },
                                { QStringLiteral("atMs"), choice.at.toMSecsSinceEpoch() } });
    }
    return out;
}

qint64 ScheduledController::customTime(const QString& date, const QString& time) const
{
    const QDateTime now = m_clock();
    const QDate day = QDate::fromString(date.trimmed(), QStringLiteral("yyyy-MM-dd"));
    const QTime clock = QTime::fromString(time.trimmed(), QStringLiteral("H:mm"));
    // QDateTime reads an invalid time as midnight.
    if (!day.isValid() || !clock.isValid())
        return 0;
    const QDateTime at(day, clock, now.timeZone());
    return sendLaterOffered(at, now) ? at.toMSecsSinceEpoch() : 0;
}

void ScheduledController::refresh()
{
    if (m_busy)
        return;
    m_message.clear();
    load();
}

void ScheduledController::load()
{
    const std::optional<DevicePairing> pairing = m_pairingStore.load();
    if (m_appLocked || m_busy || !pairing)
        return;
    m_requestedBy = identityOf(*pairing);
    const RelayEndpoint endpoint{ QUrl(pairing->serverBaseUrl), { pairing->deviceId, pairing->deviceSecret } };
    m_busy = true;
    emit changed();
    m_executor.run(
        this,
        [endpoint](HttpClient& http) { return RelayMailSource(http).listScheduled(endpoint.serverBaseUrl, endpoint.auth); },
        [this](const ScheduledListResult& result) { applyList(result); });
}

void ScheduledController::cancel(const QString& id)
{
    const std::optional<DevicePairing> pairing = m_pairingStore.load();
    if (m_appLocked || m_busy || !pairing)
        return;
    m_requestedBy = identityOf(*pairing);
    const RelayEndpoint endpoint{ QUrl(pairing->serverBaseUrl), { pairing->deviceId, pairing->deviceSecret } };
    m_busy = true;
    emit changed();
    m_executor.run(
        this,
        [endpoint, id](HttpClient& http) {
            return RelayMailSource(http).cancelScheduled(endpoint.serverBaseUrl, endpoint.auth, id);
        },
        [this](const CancelScheduledResult& result) { applyCancel(result); });
}

void ScheduledController::clear()
{
    m_items.clear();
    m_message.clear();
    emit changed();
}

void ScheduledController::setAppLocked(bool locked)
{
    m_appLocked = locked;
    if (locked)
        clear();
}

void ScheduledController::applyList(const ScheduledListResult& result)
{
    m_busy = false;
    // A reply for an account this device no longer holds, or landing under
    // the lock, is not shown.
    if (m_appLocked || !m_pairingStore.stillCurrent(m_requestedBy)) {
        emit changed();
        return;
    }
    if (result.error.has_value()) {
        m_message = result.unsupported ? i18n("This server can't schedule mail.")
                                       : i18n("Could not load scheduled mail. Try again.");
        emit changed();
        return;
    }
    const QDateTime now = m_clock();
    m_items.clear();
    for (const ScheduledSend& send : result.items) {
        // An encrypted message's real subject is inside the ciphertext.
        const QString subject = send.subject == kOuterPlaceholderSubject ? i18n("Encrypted message")
            : send.subject.isEmpty()                                   ? i18n("(no subject)")
                                                                       : send.subject;
        m_items.append(QVariantMap{
            { QStringLiteral("id"), send.id },
            { QStringLiteral("subject"), subject },
            { QStringLiteral("to"), send.to.join(QStringLiteral(", ")) },
            { QStringLiteral("when"), send.sendAt.isValid() ? whenText(send.sendAt, now) : i18n("Unknown time") },
            { QStringLiteral("status"), statusText(send.status) },
            { QStringLiteral("reason"), send.reason },
            // The relay answers 409 for the rest.
            { QStringLiteral("cancellable"), send.status == QStringLiteral("scheduled") },
        });
    }
    emit changed();
}

void ScheduledController::applyCancel(const CancelScheduledResult& result)
{
    m_busy = false;
    if (m_appLocked || !m_pairingStore.stillCurrent(m_requestedBy)) {
        emit changed();
        return;
    }
    if (result.ok)
        m_message = result.returned ? i18n("Returned to Drafts.") : i18n("Canceled. It will be saved to Drafts shortly.");
    else if (result.gone)
        m_message = i18n("That scheduled message is no longer on the server.");
    else if (result.claimed)
        m_message = i18n("It is already being sent, so it can no longer be canceled.");
    else
        m_message = i18n("Could not cancel it. Try again.");
    // Either way the list has moved on; the message stays.
    load();
}
