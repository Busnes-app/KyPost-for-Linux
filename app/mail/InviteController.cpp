#include "mail/InviteController.h"

#include "domain/PairingStore.h"
#include "domain/ReplyAll.h"
#include "net/NetworkExecutor.h"
#include "net/PgpBootstrapClient.h"
#include "net/RelayAuth.h"
#include "net/RelayMailSource.h"

#include <KLocalizedString>

#include <QCryptographicHash>
#include <QLocale>

namespace {

// Cards are held for whichever reader asked; more than this many readers is
// not a thing, so the oldest just goes.
constexpr int kMaxCards = 16;

struct LoadedInvite
{
    std::optional<CalendarInvite> invite;
};

QString answerLabel(RsvpAnswer answer)
{
    switch (answer) {
    case RsvpAnswer::Accepted:
        return i18n("Accepted");
    case RsvpAnswer::Tentative:
        return i18n("Tentative");
    case RsvpAnswer::Declined:
        return i18n("Declined");
    }
    return {};
}

// "You %1." / "You had %1; answer again."
QString answeredVerb(RsvpAnswer answer)
{
    switch (answer) {
    case RsvpAnswer::Accepted:
        return i18nc("you %1 the invitation", "accepted");
    case RsvpAnswer::Tentative:
        return i18nc("you %1 the invitation", "answered maybe");
    case RsvpAnswer::Declined:
        return i18nc("you %1 the invitation", "declined");
    }
    return {};
}

// The event across revisions. One account at a time (clear() on a pairing
// change), so the attendee is not part of it.
QString answerKey(const CalendarEvent& event)
{
    return event.uid + QChar(0) + event.organizer.toLower();
}

QByteArray digest(const QString& value)
{
    return QCryptographicHash::hash(value.toUtf8(), QCryptographicHash::Sha256);
}

QByteArray whenDigest(const CalendarEvent& event)
{
    return digest(QStringLiteral("%1|%2|%3").arg(event.begin.toMSecsSinceEpoch()).arg(event.end.toMSecsSinceEpoch()).arg(event.allDay));
}

// When the invite happens, in `zone`. All-day ends are exclusive, so the last
// day shown is the day before `end`.
QString whenText(const CalendarEvent& event, const QTimeZone& zone)
{
    const QLocale locale;
    const QDateTime begin = event.begin.toTimeZone(zone);
    const QDateTime end = event.end.toTimeZone(zone);
    if (event.allDay) {
        const QDate last = end.date().addDays(-1);
        if (last <= begin.date())
            return locale.toString(begin.date(), QLocale::ShortFormat);
        return i18nc("an all-day event's first and last day", "%1 – %2", locale.toString(begin.date(), QLocale::ShortFormat),
                     locale.toString(last, QLocale::ShortFormat));
    }
    const QString start = locale.toString(begin, QLocale::ShortFormat);
    if (end == begin)
        return start;
    if (end.date() == begin.date())
        return i18nc("an event's start and end time", "%1 – %2", start, locale.toString(end.time(), QLocale::ShortFormat));
    return i18nc("an event's start and end time", "%1 – %2", start, locale.toString(end, QLocale::ShortFormat));
}

// The relay answered and refused: nothing went out, so asking again is safe.
// A timeout, a 5xx other than 503, an unreadable 200: it may have been sent.
bool refusedBeforeSending(const SendMailResult& result)
{
    if (!result.error)
        return false;
    switch (*result.error) {
    case NetworkError::InvalidUrl:
    case NetworkError::Unauthorized:
    case NetworkError::Conflict:
    case NetworkError::RateLimited:
    case NetworkError::ServiceUnavailable:
    case NetworkError::CertificateMismatch:
        return true;
    case NetworkError::Server:
        // 400 (the relay's own calendarReply check), 404, 413: all refused
        // before any SMTP.
        return result.statusCode >= 400 && result.statusCode < 500;
    default:
        return false;
    }
}

} // namespace

InviteController::InviteController(NetworkExecutor& executor, PairingStore& pairingStore, Clock clock, QTimeZone zone,
                                   QObject* parent)
    : QObject(parent)
    , m_executor(executor)
    , m_pairingStore(pairingStore)
    , m_clock(clock ? std::move(clock) : Clock([] { return QDateTime::currentDateTimeUtc(); }))
    , m_zone(std::move(zone))
{
}

QVariantMap InviteController::states() const
{
    QVariantMap out;
    for (auto it = m_cards.constBegin(); it != m_cards.constEnd(); ++it) {
        const QString key = rsvpKey(it->event);
        const auto phase = m_phases.constFind(key);
        out.insert(QString::number(it.key()),
                   QVariantMap{ { QStringLiteral("answerable"), phase == m_phases.constEnd() },
                                { QStringLiteral("sending"), phase != m_phases.constEnd() && *phase == Phase::Sending },
                                { QStringLiteral("message"), m_messages.value(key) },
                                { QStringLiteral("reload"), m_conflicts.contains(key) } });
    }
    return out;
}

QVariantMap InviteController::confirmation() const
{
    if (!m_pending || m_appLocked)
        return {};
    return { { QStringLiteral("token"), m_pending->token },
             { QStringLiteral("title"), i18n("Send RSVP unencrypted?") },
             { QStringLiteral("text"), m_pending->text },
             { QStringLiteral("sendLabel"), i18n("Send unencrypted") } };
}

const InviteController::Card* InviteController::liveCard(int token) const
{
    const auto it = m_cards.constFind(token);
    if (it == m_cards.constEnd() || !m_pairingStore.stillCurrent(it->owner))
        return nullptr;
    return &it.value();
}

int InviteController::load(const QString& mailbox, const QString& messageId, const QVariantList& attachments,
                           const QString& sender, bool phishing)
{
    if (m_appLocked)
        return 0;
    QVector<MailAttachmentInfo> listed;
    for (const QVariant& value : attachments) {
        const QVariantMap entry = value.toMap();
        MailAttachmentInfo info;
        info.index = entry.value(QStringLiteral("index")).toInt();
        info.name = entry.value(QStringLiteral("name")).toString();
        info.mimeType = entry.value(QStringLiteral("mimeType")).toString();
        info.size = entry.value(QStringLiteral("size")).toInt();
        info.calendarMethod = entry.value(QStringLiteral("calendarMethod")).toString();
        listed.append(info);
    }
    const std::optional<MailAttachmentInfo> part = inviteAttachment(listed);
    const std::optional<DevicePairing> pairing = m_pairingStore.load();
    if (!part || !pairing)
        return 0;

    const int request = m_nextRequest++;
    const PairingIdentity owner = identityOf(*pairing);
    const RelayEndpoint endpoint{ QUrl(pairing->serverBaseUrl), { pairing->deviceId, pairing->deviceSecret } };
    const int index = part->index;
    const QString listedMethod = part->calendarMethod;
    const QTimeZone zone = m_zone;
    m_executor.run(
        this,
        [endpoint, mailbox, messageId, index, zone](HttpClient& http) {
            // Held in memory only: an invite is never written to disk.
            const DownloadAttachmentResult download =
                RelayMailSource(http).downloadAttachment(endpoint.serverBaseUrl, endpoint.auth, mailbox, messageId, index);
            LoadedInvite out;
            if (!download.error && download.data.size() <= kMaxInviteBytes)
                out.invite = parseCalendarInvite(download.data, zone);
            return out;
        },
        [this, request, owner, listedMethod, sender, phishing, mailbox, messageId, index](const LoadedInvite& loaded) {
            // A failed or unreadable invite is just no card; one landing under
            // the lock waits for the next open.
            if (m_appLocked || !loaded.invite)
                return;
            // The previous account's mail, shown to the next: never.
            if (!m_pairingStore.stillCurrent(owner))
                return;
            const CalendarInvite& invite = *loaded.invite;
            const CalendarEvent& event = invite.primary();
            const bool organizerIsSender =
                event.organizer.compare(addressFromHeader(sender), Qt::CaseInsensitive) == 0;

            const int token = m_nextToken++;
            m_cards.insert(token,
                           Card{ mailbox, messageId, index, event, invite.offersRsvp(listedMethod), organizerIsSender, owner });
            m_cardOrder.append(token);
            while (m_cardOrder.size() > kMaxCards)
                m_cards.remove(m_cardOrder.takeFirst());
            // Opening the invite is the user's deliberate retry of an unsure
            // send (Android's rule). A confirmed one stays sent.
            const QString key = rsvpKey(event);
            if (m_phases.value(key, Phase::Asking) == Phase::Unsure)
                m_phases.remove(key);
            m_conflicts.remove(key);

            // A newer revision of an event already answered says what changed
            // and asks again (the web card's rule).
            QString note;
            QString again;
            const auto prior = m_answered.constFind(answerKey(event));
            if (invite.method == QLatin1String("REQUEST") && event.canReply && prior != m_answered.constEnd()) {
                if (event.sequence > prior->sequence || (event.sequence == prior->sequence && event.stamp > prior->stamp)) {
                    QStringList changed;
                    if (prior->when != whenDigest(event))
                        changed.append(i18nc("changed since you answered", "time"));
                    if (prior->place != digest(event.location))
                        changed.append(i18nc("changed since you answered", "place"));
                    if (prior->title != digest(event.summary))
                        changed.append(i18nc("changed since you answered", "title"));
                    again = (changed.isEmpty() ? i18n("This invitation was updated since you answered.")
                                               : i18n("Changed since you answered: %1.",
                                                      QLocale().createSeparatedList(changed)))
                        + QLatin1Char(' ') + i18n("You had %1; answer again.", answeredVerb(prior->answer));
                } else {
                    note = i18n("You %1.", answeredVerb(prior->answer));
                }
            }
            const bool cancelled = invite.isCancelled();
            const bool joins = !cancelled && event.conference.isValid();

            QStringList details{ whenText(event, m_zone) };
            if (!event.rrule.isEmpty())
                details.append(i18n("Repeats"));
            if (!event.location.isEmpty())
                details.append(i18n("Where: %1", event.location));
            if (!event.organizer.isEmpty())
                details.append(i18n("Organizer: %1", event.organizer));
            QStringList notices;
            if (phishing)
                notices.append(i18n("This message is flagged as phishing. Check the invite before you add it."));
            // The organizer is the sender's own claim; say when From disagrees.
            if (!event.organizer.isEmpty() && !organizerIsSender)
                notices.append(i18n("The organizer (%1) is not the sender of this email. Anyone can name any organizer.",
                                    event.organizer));
            if (!again.isEmpty())
                notices.append(again);
            else if (invite.isUpdate() && prior == m_answered.constEnd())
                notices.append(i18n("This invite was updated. Check your calendar."));
            if (!event.unknownZone.isEmpty())
                notices.append(i18n("Time zone \"%1\" isn't recognised, so this time is read as your own zone and may "
                                    "be wrong.",
                                    event.unknownZone));

            emit changed();
            emit inviteLoaded(request,
                              QVariantMap{
                                  { QStringLiteral("token"), token },
                                  { QStringLiteral("eyebrow"), invite.method.isEmpty()
                                        ? i18n("Calendar invite")
                                        : i18n("Calendar invite · %1", invite.method) },
                                  { QStringLiteral("title"),
                                    event.summary.isEmpty() ? i18n("Untitled event") : event.summary },
                                  { QStringLiteral("details"), details },
                                  { QStringLiteral("notices"), notices },
                                  { QStringLiteral("note"), note },
                                  { QStringLiteral("cancelled"), cancelled },
                                  { QStringLiteral("joinUrl"),
                                    joins ? event.conference.toString(QUrl::FullyEncoded) : QString() },
                                  { QStringLiteral("joinHost"),
                                    joins ? event.conference.host(QUrl::FullyEncoded) : QString() },
                                  { QStringLiteral("offersRsvp"), invite.offersRsvp(listedMethod) },
                              });
        });
    return request;
}

void InviteController::forget(int token)
{
    if (!m_cards.remove(token))
        return;
    m_cardOrder.removeAll(token);
    // An in-flight lookup finds no card and releases its revision itself.
    if (m_pending && m_pending->token == token) {
        const QString key = m_pending->key;
        m_pending.reset();
        release(key);
        return;
    }
    emit changed();
}

void InviteController::requestRsvp(int token, const QString& partstat)
{
    const Card* card = liveCard(token);
    const std::optional<RsvpAnswer> answer = rsvpAnswerFromPartstat(partstat);
    if (m_appLocked || !card || !card->offersRsvp || !answer)
        return;
    const QString key = rsvpKey(card->event);
    // Process-wide: two readers on the same invite cannot both answer.
    if (m_phases.contains(key))
        return;
    if (m_pending) {
        m_messages.insert(key, i18n("Answer the other RSVP first."));
        emit changed();
        return;
    }
    const std::optional<DevicePairing> pairing = m_pairingStore.load();
    if (!pairing || identityOf(*pairing) != card->owner)
        return;
    m_phases.insert(key, Phase::Sending);
    m_messages.remove(key);
    m_conflicts.remove(key);
    emit changed();

    const PairingIdentity owner = card->owner;
    const RelayEndpoint endpoint{ QUrl(pairing->serverBaseUrl), { pairing->deviceId, pairing->deviceSecret } };
    m_executor.run(
        this,
        [endpoint, mailbox = card->mailbox, messageId = card->messageId, index = card->index,
         partstat = rsvpPartstat(*answer)](HttpClient& http) {
            return RelayMailSource(http).answerInvite(endpoint.serverBaseUrl, endpoint.auth, mailbox, messageId, index,
                                                      partstat);
        },
        [this, token, key, owner, event = card->event, answer = *answer](const CalendarRsvpResult& result) {
            // clear() already dropped the previous account's state.
            const bool ours = m_pairingStore.stillCurrent(owner);
            if (!ours)
                return;
            using Outcome = CalendarRsvpResult::Outcome;
            const bool byMail = result.outcome == Outcome::Off || result.outcome == Outcome::NoEvent;
            // Locked or left meanwhile: nothing to ask about, say nothing.
            if (byMail && (m_appLocked || !liveCard(token))) {
                m_phases.remove(key);
                emit changed();
                return;
            }
            if (byMail) {
                m_phases.insert(key, Phase::Asking);
                askByMail(token, key, answer, result.outcome == Outcome::NoEvent);
                return;
            }
            answered(key, result, event, answer);
        });
}

void InviteController::answered(const QString& key, const CalendarRsvpResult& result, const CalendarEvent& event,
                                RsvpAnswer answer)
{
    using Outcome = CalendarRsvpResult::Outcome;
    if (result.outcome == Outcome::Answered) {
        m_phases.insert(key, Phase::Sent);
        m_messages.insert(key, i18n("Answered through KyCalendar."));
        remember(event, answer);
    } else {
        // Shown, never mailed: the user may try again.
        m_phases.remove(key);
        if (result.outcome == Outcome::Unconfirmed)
            m_messages.insert(key, i18n("KyCalendar did not confirm the answer; try again."));
        else if (result.outcome == Outcome::Conflict) {
            m_conflicts.insert(key);
            m_messages.insert(key, i18n("The event changed since this invitation. Reload it and answer again."));
        } else
            m_messages.insert(key, result.detail.isEmpty() ? i18n("KyCalendar could not be reached; try again.")
                                                           : result.detail);
    }
    emit changed();
}

void InviteController::askByMail(int token, const QString& key, RsvpAnswer answer, bool noEvent)
{
    const Card* card = liveCard(token);
    const std::optional<DevicePairing> pairing = m_pairingStore.load();
    if (!card || !pairing || identityOf(*pairing) != card->owner) {
        release(key);
        return;
    }
    const PairingIdentity owner = card->owner;
    const RelayEndpoint endpoint{ QUrl(pairing->serverBaseUrl), { pairing->deviceId, pairing->deviceSecret } };
    m_executor.run(
        this,
        [endpoint](HttpClient& http) { return PgpBootstrapClient(http).fetch(endpoint.serverBaseUrl, endpoint.auth); },
        [this, token, key, owner, answer, noEvent](const PgpBootstrapResult& bootstrap) {
            // Locked or replaced during the lookup: release quietly, there is
            // nothing to say behind the cover or to the next account. One
            // confirmation at a time: another card's lookup may have got there
            // first.
            const Card* asked = liveCard(token);
            if (m_appLocked) {
                release(key);
                return;
            }
            if (!m_pairingStore.stillCurrent(owner) || !asked) {
                release(key);
                return;
            }
            if (m_pending) {
                release(key);
                m_messages.insert(key, i18n("Answer the other RSVP first."));
                emit changed();
                return;
            }
            const QString attendee = bootstrap.ok ? rsvpAttendee(asked->event, bootstrap.addresses) : QString();
            if (attendee.isEmpty()) {
                release(key);
                m_messages.insert(key, i18n("Couldn't confirm your account address, so no RSVP was sent. Try again."));
                emit changed();
                return;
            }
            // Answering as an alias must also send from it, or the organizer
            // sees a stranger reply. Compared with the relay's own default
            // From (suggestedUserIDs[0]), not with the first of `addresses`:
            // when the login is not an address, that list may start with an
            // alias the relay would not send from unasked.
            const QString sendAs =
                attendee.compare(bootstrap.primaryAddress, Qt::CaseInsensitive) == 0 ? QString() : attendee;
            // NATIVE-RSVP.md "Mail fallback": why it is mail, then where it goes.
            const QString why = noEvent ? i18n("KyCalendar has no event for this invitation")
                                        : i18n("KyCalendar is not in use for this mailbox");
            QString text = i18nc("%1 is why the answer goes by mail", "%1, so your answer goes to %2 as an "
                                 "unencrypted, unsigned email.", why, asked->event.organizer)
                + QStringLiteral("\n\n")
                + i18n("Reply \"%1\" as %2. The organizer's calendar can't read an encrypted reply. The reply "
                       "carries your answer, your address and the event's ID.",
                       answerLabel(answer), attendee);
            if (!asked->organizerIsSender)
                text += QStringLiteral("\n\n")
                    + i18n("The organizer is not the sender of this email, so this reply goes to an address the "
                           "sender chose.");
            m_pending = Pending{ token, key, answer, attendee, sendAs, text };
            emit changed();
        });
}

void InviteController::cancelRsvp(int token)
{
    if (!m_pending || m_pending->token != token)
        return;
    const QString key = m_pending->key;
    m_pending.reset();
    release(key);
}

void InviteController::confirmRsvp(int token)
{
    if (!m_pending || m_pending->token != token)
        return;
    const Pending pending = *m_pending;
    m_pending.reset();
    const auto card = m_cards.constFind(token);
    const std::optional<DevicePairing> pairing = m_pairingStore.load();
    if (m_appLocked || card == m_cards.constEnd() || !pairing) {
        release(pending.key);
        return;
    }
    // Sent only for the account that loaded the card and was asked: the
    // pairing whose credentials carry it must be that same one.
    if (identityOf(*pairing) != card->owner) {
        release(pending.key);
        return;
    }
    const std::optional<RsvpMail> mail =
        rsvpMail(card->event, pending.attendee, pending.answer, m_clock(), pending.sendAs);
    if (!mail) {
        release(pending.key);
        return;
    }
    m_phases.insert(pending.key, Phase::Sending);
    emit changed();

    const PairingIdentity owner = card->owner;
    const RelayEndpoint endpoint{ QUrl(pairing->serverBaseUrl), { pairing->deviceId, pairing->deviceSecret } };
    const QString key = pending.key;
    const CalendarEvent event = card->event;
    const RsvpAnswer answer = pending.answer;
    m_executor.run(
        this,
        [endpoint, mail = *mail](HttpClient& http) {
            return RelayMailSource(http).sendCalendarReply(endpoint.serverBaseUrl, endpoint.auth, mail.to, mail.from,
                                                           mail.subject, mail.body, mail.ics);
        },
        [this, owner, key, event, answer](const SendMailResult& result) {
            // The account went away under it: its answer is not the next
            // account's to see on the same invite, and nobody is left to tell.
            if (m_pairingStore.stillCurrent(owner))
                finish(key, result, event, answer);
        });
}

void InviteController::remember(const CalendarEvent& event, RsvpAnswer answer)
{
    m_answered.insert(answerKey(event), Answered{ event.sequence, event.stamp, whenDigest(event),
                                                  digest(event.summary), digest(event.location), answer });
}

void InviteController::finish(const QString& key, const SendMailResult& result, const CalendarEvent& event,
                              RsvpAnswer answer)
{
    const QString reason = result.detail.isEmpty() ? i18n("unknown error") : result.detail;
    if (!result.error && result.ok && result.calendarReplySent) {
        m_phases.insert(key, Phase::Sent);
        m_messages.insert(key, i18n("Your answer was sent to %1.", event.organizer));
        remember(event, answer);
    } else if (!result.error && result.ok) {
        // A relay that ignored the field: the organizer got only the body.
        // Never retried, never counted as an RSVP.
        m_phases.insert(key, Phase::Unsure);
        m_messages.insert(key, i18n("The organizer got your answer as ordinary mail; their calendar may not "
                                    "update."));
        remember(event, answer);
    } else if (refusedBeforeSending(result)) {
        m_phases.remove(key);
        m_messages.insert(key, i18n("RSVP not sent: %1", reason));
    } else {
        m_phases.insert(key, Phase::Unsure);
        m_messages.insert(key, i18n("Your answer may have been sent (%1). Check Sent before answering again.", reason));
    }
    emit changed();
}

void InviteController::release(const QString& key)
{
    const auto phase = m_phases.constFind(key);
    if (phase != m_phases.constEnd() && *phase == Phase::Asking)
        m_phases.remove(key);
    emit changed();
}

void InviteController::clear()
{
    m_cards.clear();
    m_cardOrder.clear();
    m_phases.clear();
    m_messages.clear();
    m_conflicts.clear();
    m_answered.clear();
    m_pending.reset();
    emit changed();
}

void InviteController::setAppLocked(bool locked)
{
    m_appLocked = locked;
    if (locked && m_pending) {
        const QString key = m_pending->key;
        m_pending.reset();
        release(key);
        return;
    }
    emit changed();
}
