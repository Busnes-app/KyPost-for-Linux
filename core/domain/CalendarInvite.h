#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QTimeZone>
#include <QVector>

#include <optional>

struct MailAttachmentInfo;

// Calendar invites and RSVP (plan item L17). A port of kypost-android's
// mail/CalendarInvite.kt and mail/RsvpTracker.kt's rsvpKey: the parser, the
// part to read, and the RFC 5546 REPLY. Wording shown to the user lives in
// app/mail/InviteController.cpp; the REPLY's own subject and body are wire
// content and stay here, in English, as Android sends them.

// Sender-controlled input: an invite past either bound is refused whole,
// before any parsing.
inline constexpr int kMaxInviteBytes = 256 * 1024;
inline constexpr int kMaxInviteEvents = 50;
inline constexpr int kMaxInviteDescriptionChars = 16000;

// One VEVENT. All-day events carry midnight in the zone they were parsed
// for, end exclusive.
struct CalendarEvent
{
    // Verbatim, for the REPLY; empty when missing or unusable.
    QString uid;
    int sequence = 0;
    QString summary;
    QString location;
    QString description;
    // The bare address from `mailto:`; never the sender-chosen CN. Empty
    // when unusable.
    QString organizer;
    QDateTime begin;
    QDateTime end;
    bool allDay = false;
    QString rrule;
    // A DTSTART or DTEND TZID that resolved to nothing, so the floating
    // fallback was used; empty otherwise.
    QString unknownZone;
    // Bare addresses of every ATTENDEE that has a usable one.
    QStringList attendees;
    // An override's RECURRENCE-ID line restated without TZID, or empty for a
    // whole series.
    QString recurrenceId;
    // An iTIP REPLY needs a UID, an organizer, a SEQUENCE read back exactly,
    // and a RECURRENCE-ID copied whole if there is one.
    bool canReply = false;
};

struct CalendarInvite
{
    // Uppercase iTIP METHOD, or empty when absent or not a plain token.
    QString method;
    // The series master first; an invite without one does not parse.
    QVector<CalendarEvent> events;

    const CalendarEvent& primary() const { return events.first(); }
    bool isCancelled() const { return method == QLatin1String("CANCEL"); }
    // A REQUEST with a raised SEQUENCE re-sends an invite the user may
    // already have.
    bool isUpdate() const { return method == QLatin1String("REQUEST") && primary().sequence > 0; }
    // listedMethod is the relay's `calendarMethod` for the part (KyPost-Server
    // #353). A relay without it predates `calendarReply` (#354), ignores the
    // field and would deliver the RSVP as plain mail, so it gets no buttons.
    bool offersRsvp(const QString& listedMethod) const
    {
        return method == QLatin1String("REQUEST") && primary().canReply && !listedMethod.isEmpty();
    }
};

// Parses the subset of RFC 5545 an invite card needs. nullopt on anything it
// cannot vouch for. Floating times and all-day dates are read in localZone;
// an Outlook TZID such as "Pacific Standard Time" goes through Qt's
// Windows-zone table.
std::optional<CalendarInvite> parseCalendarInvite(const QByteArray& bytes, const QTimeZone& localZone);

// The listed part to download, or nullopt. A text/calendar part (the iMIP
// body part) beats the `.ics` file Google attaches beside it; both hold the
// same event. Past kMaxInviteBytes, or empty, nothing is read.
std::optional<MailAttachmentInfo> inviteAttachment(const QVector<MailAttachmentInfo>& listed);

// An RSVP answer; the enum's wire form is its iTIP PARTSTAT.
enum class RsvpAnswer
{
    Accepted,
    Tentative,
    Declined,
};
QString rsvpPartstat(RsvpAnswer answer);
// "Accepted", "Tentative", "Declined": the REPLY subject's prefix.
QString rsvpSubjectPrefix(RsvpAnswer answer);
std::optional<RsvpAnswer> rsvpAnswerFromPartstat(const QString& partstat);

// True for an address this module will write into a REPLY. A CR, LF or any
// other control or format character is refused: it could inject an ics line.
bool isRsvpAddress(const QString& address);

// The user's attendee address: the one of accountAddresses (primary first,
// then aliases, as the relay's bootstrap lists them) the invite names, else
// the primary. Empty when no usable address is known.
QString rsvpAttendee(const CalendarEvent& event, const QStringList& accountAddresses);

// One answerable revision: a raised SEQUENCE is a new invite to answer. The
// organizer is part of it, unlike Android's: a copy of a real invite naming
// another organizer must not mark the real one answered.
QString rsvpKey(const CalendarEvent& event);

// What POST /api/mail/send carries for one RSVP. Never encrypted or signed:
// the organizer's calendar must read it, and the relay refuses the
// combination with 400 anyway.
struct RsvpMail
{
    QString to;      // the ORGANIZER
    QString from;    // the alias answered as, or empty for the primary
    QString subject; // "Accepted: <summary>"
    QString body;    // plain text
    QString ics;     // RFC 5546 METHOD:REPLY, CRLF, folded at 75 octets
};

// RFC 5546 §3.2.3 REPLY: same UID, SEQUENCE and RECURRENCE-ID, this
// attendee's PARTSTAT, a fresh DTSTAMP, addressed to the ORGANIZER. nullopt
// unless event.canReply and attendee is an isRsvpAddress().
std::optional<RsvpMail> rsvpMail(const CalendarEvent& event, const QString& attendee, RsvpAnswer answer,
                                 const QDateTime& now, const QString& sendAs = {});
