#pragma once

#include "domain/CalendarInvite.h"
#include "domain/DevicePairing.h"

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QTimeZone>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <optional>

class NetworkExecutor;
class PairingStore;
struct CalendarRsvpResult;
struct SendMailResult;

// The "Invites" QML singleton (plan item L17): the calendar-invite card on the
// reader, and Accept/Maybe/Decline. An answer goes through KyCalendar first
// (POST /api/calendar/rsvp, KyPost-Server docs/NATIVE-RSVP.md); only when the
// relay says KyCalendar is off or has no such event, and the user confirms,
// does it go as an RFC 5546 REPLY through POST /api/mail/send
// `calendarReply`. Port of kypost-android's invite card and RsvpTracker;
// Mac's InviteCardModel is the second reference.
//
// A card is addressed by the token load() hands out, never by the event's
// UID: two invites may share a UID, and a sender-chosen copy must not be able
// to re-point another card's answer at its own organizer. RSVP state is keyed
// by rsvpKey (UID + RECURRENCE-ID + SEQUENCE) and is process-wide, so two
// readers on one invite cannot both answer.
//
// Every RSVP is confirmed, goes unencrypted and unsigned, and goes only for
// the account that loaded the card: the PairingIdentity captured then must
// still be current when the user confirms, and again when the answer lands.
class InviteController : public QObject
{
    Q_OBJECT
    // token (as a string) -> {answerable, sending, message, reload} for every
    // card held. `message` is the last outcome for that revision, from
    // whichever reader sent it; `reload` asks for the invite to be read again
    // (KyCalendar's 409 conflict).
    Q_PROPERTY(QVariantMap states READ states NOTIFY changed)
    // The RSVP waiting on the user's confirmation: {token, title, text,
    // sendLabel}, or {} when there is none.
    Q_PROPERTY(QVariantMap confirmation READ confirmation NOTIFY changed)

public:
    using Clock = std::function<QDateTime()>;

    InviteController(NetworkExecutor& executor, PairingStore& pairingStore, Clock clock = {},
                     QTimeZone zone = QTimeZone::systemTimeZone(), QObject* parent = nullptr);

    QVariantMap states() const;
    QVariantMap confirmation() const;

    // Reads the invite among `attachments` (MailApp's listing for this
    // message: {index, name, mimeType, size, calendarMethod}) into memory and
    // answers with inviteLoaded() under the returned request id, so each
    // reader takes only its own card; 0 when there is nothing to read, and a
    // message with no readable invite gets no answer. `sender` is the From
    // header, for the organizer check. Each load is a fresh open: an unsure
    // answer to the same invite may be retried.
    Q_INVOKABLE int load(const QString& mailbox, const QString& messageId, const QVariantList& attachments,
                         const QString& sender, bool phishing);
    // The reader no longer shows this card: it is dropped, and an answer
    // still being asked about (lookup or confirmation) is released.
    Q_INVOKABLE void forget(int token);
    // "ACCEPTED", "TENTATIVE" or "DECLINED". Claims the revision, looks the
    // account's addresses up, then raises `confirmation`.
    Q_INVOKABLE void requestRsvp(int token, const QString& partstat);
    Q_INVOKABLE void confirmRsvp(int token);
    Q_INVOKABLE void cancelRsvp(int token);

    // Pairing changed: every card, state and pending answer belonged to the
    // previous account.
    void clear();
    // Locked: a pending confirmation is withdrawn and its revision released;
    // nothing is loaded, asked or sent until unlocked.
    void setAppLocked(bool locked);

signals:
    void changed();
    // card: {token, eyebrow, title, details, notices, note, cancelled,
    // joinUrl, joinHost, offersRsvp}.
    void inviteLoaded(int request, const QVariantMap& card);

private:
    enum class Phase
    {
        Asking,  // the address lookup, then the confirmation
        Sending, // confirmed and on its way
        Sent,
        Unsure, // may have gone out, or went as plain mail
    };

    struct Card
    {
        QString mailbox;
        QString messageId;
        int index = 0;
        CalendarEvent event;
        bool offersRsvp = false;
        bool organizerIsSender = false;
        PairingIdentity owner;
    };

    struct Pending
    {
        int token = 0;
        QString key;
        RsvpAnswer answer = RsvpAnswer::Accepted;
        QString attendee;
        QString sendAs;
        QString text;
    };

    // The last answer to an event, kept in memory only and as digests, so a
    // newer revision can say what changed.
    struct Answered
    {
        int sequence = 0;
        QDateTime stamp;
        QByteArray when;
        QByteArray title;
        QByteArray place;
        RsvpAnswer answer = RsvpAnswer::Accepted;
    };

    void answered(const QString& key, const CalendarRsvpResult& result, const CalendarEvent& event, RsvpAnswer answer);
    // KyCalendar is off or has no event: look the address up, then ask.
    void askByMail(int token, const QString& key, RsvpAnswer answer, bool noEvent);
    void remember(const CalendarEvent& event, RsvpAnswer answer);
    // Only a definite refusal frees the key; anything else may have gone out.
    void finish(const QString& key, const SendMailResult& result, const CalendarEvent& event, RsvpAnswer answer);
    void release(const QString& key);
    const Card* liveCard(int token) const;

    NetworkExecutor& m_executor;
    PairingStore& m_pairingStore;
    Clock m_clock;
    QTimeZone m_zone;
    bool m_appLocked = false;
    int m_nextToken = 1;
    int m_nextRequest = 1;
    QHash<int, Card> m_cards;
    QList<int> m_cardOrder; // oldest first, for the cap
    QHash<QString, Phase> m_phases;
    QHash<QString, QString> m_messages;
    QSet<QString> m_conflicts;
    QHash<QString, Answered> m_answered;
    std::optional<Pending> m_pending;
};
