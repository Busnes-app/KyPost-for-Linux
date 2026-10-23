#include "domain/CalendarInvite.h"

#include "net/RelayMailSource.h" // MailAttachmentInfo

#include <QHash>
#include <QLocale>
#include <QRegularExpression>

#include <algorithm>

namespace {

QRegularExpression whole(const QString& pattern)
{
    return QRegularExpression(QRegularExpression::anchoredPattern(pattern));
}

bool matches(const QRegularExpression& re, const QString& value)
{
    return re.match(value).hasMatch();
}

const QRegularExpression& nameRe()
{
    static const QRegularExpression re = whole(QStringLiteral("[A-Z0-9-]{1,64}"));
    return re;
}

const QRegularExpression& methodRe()
{
    static const QRegularExpression re = whole(QStringLiteral("[A-Z][A-Z0-9-]{0,31}"));
    return re;
}

const QRegularExpression& dateRe()
{
    static const QRegularExpression re = whole(QStringLiteral("\\d{8}"));
    return re;
}

const QRegularExpression& dateTimeRe()
{
    static const QRegularExpression re = whole(QStringLiteral("(\\d{8})T(\\d{6})(Z?)"));
    return re;
}

const QRegularExpression& durationRe()
{
    static const QRegularExpression re = whole(QStringLiteral(
        "([+-]?)P(?:(\\d{1,6})W|(?:(\\d{1,6})D)?(?:T(?:(\\d{1,6})H)?(?:(\\d{1,6})M)?(?:(\\d{1,6})S)?)?)"));
    return re;
}

const QRegularExpression& rruleRe()
{
    static const QRegularExpression re = whole(QStringLiteral("[A-Za-z0-9=;,:+\\-]{1,512}"));
    return re;
}

// Android's ADDRESS. \p{Cc} is wider than Java's ASCII-only \p{Cntrl}: C1
// controls are refused too.
const QRegularExpression& addressRe()
{
    static const QRegularExpression re = whole(QStringLiteral(
        "[^\\s@<>\"\\p{Cc}\\p{Cf}]{1,64}@[^\\s@<>\"\\p{Cc}\\p{Cf}]{1,190}"));
    return re;
}

// Qt's TZ backend reads /usr/share/zoneinfo/<id>, so a sender-chosen TZID
// reaches QTimeZone only shaped like an IANA name: no dot segments, no
// absolute path. Not in Android, whose ZoneId never touches the disk.
const QRegularExpression& ianaIdRe()
{
    static const QRegularExpression re = whole(QStringLiteral("[A-Za-z][A-Za-z0-9_+-]*(?:/[A-Za-z0-9_+-]+)*"));
    return re;
}

constexpr int kMaxTzidChars = 128;
constexpr char16_t kZwj = 0x200D;

bool isIsoControl(char32_t c)
{
    return c <= 0x1F || (c >= 0x7F && c <= 0x9F);
}

// RFC 5545 §3.1: unfolded as bytes, because a fold may split a UTF-8 sequence.
QByteArray unfold(const QByteArray& bytes)
{
    QByteArray out;
    out.reserve(bytes.size());
    const qsizetype n = bytes.size();
    qsizetype i = 0;
    while (i < n) {
        qsizetype lf = -1;
        if (bytes.at(i) == '\n')
            lf = i;
        else if (bytes.at(i) == '\r' && i + 1 < n && bytes.at(i + 1) == '\n')
            lf = i + 1;
        if (lf >= 0 && lf + 1 < n && (bytes.at(lf + 1) == ' ' || bytes.at(lf + 1) == '\t')) {
            i = lf + 2;
            continue;
        }
        out.append(bytes.at(i));
        ++i;
    }
    return out;
}

struct ContentLine
{
    QString name;
    QHash<QString, QString> params;
    QString value;
};

// `NAME *(;PARAM=value) : value`, with quoted parameter values that may hold
// `:` or `;`.
std::optional<ContentLine> contentLine(const QString& line)
{
    const qsizetype n = line.size();
    qsizetype i = 0;
    while (i < n && line.at(i) != QLatin1Char(';') && line.at(i) != QLatin1Char(':'))
        ++i;
    if (i == n || i == 0)
        return std::nullopt;
    ContentLine out;
    out.name = line.left(i).toUpper();
    if (!matches(nameRe(), out.name))
        return std::nullopt;
    while (line.at(i) == QLatin1Char(';')) {
        const qsizetype eq = line.indexOf(QLatin1Char('='), i);
        if (eq < 0)
            return std::nullopt;
        const QString param = line.mid(i + 1, eq - i - 1).toUpper();
        if (!matches(nameRe(), param))
            return std::nullopt;
        i = eq + 1;
        QString value;
        while (i < n && line.at(i) != QLatin1Char(';') && line.at(i) != QLatin1Char(':')) {
            if (line.at(i) == QLatin1Char('"')) {
                const qsizetype close = line.indexOf(QLatin1Char('"'), i + 1);
                if (close < 0)
                    return std::nullopt;
                value += line.mid(i + 1, close - i - 1);
                i = close + 1;
            } else {
                value += line.at(i++);
            }
        }
        if (i == n)
            return std::nullopt;
        if (!out.params.contains(param))
            out.params.insert(param, value);
    }
    out.value = line.mid(i + 1);
    return out;
}

// Drops what could reorder or hide text on the card: controls other than
// newline and tab, and format characters other than ZWJ. Walks code points, so
// a format character outside the BMP goes too, and never keeps half a
// surrogate pair at the limit.
QString visible(const QString& value, int limit)
{
    QString out;
    out.reserve(std::min<qsizetype>(value.size(), limit));
    qsizetype i = 0;
    while (i < value.size()) {
        const QChar c = value.at(i);
        char32_t cp = c.unicode();
        int units = 1;
        if (c.isHighSurrogate() && i + 1 < value.size() && value.at(i + 1).isLowSurrogate()) {
            cp = QChar::surrogateToUcs4(c, value.at(i + 1));
            units = 2;
        }
        const QChar::Category category = QChar::category(cp);
        const bool keep = cp == U'\n' || cp == U'\t' || cp == kZwj
            || !(isIsoControl(cp) || category == QChar::Other_Control || category == QChar::Other_Format);
        if (keep) {
            if (out.size() + units > limit)
                break;
            out += value.mid(i, units);
        }
        i += units;
    }
    return out;
}

// RFC 5545 §3.3.11 unescape, then visible().
QString text(const ContentLine* line, int limit)
{
    if (!line)
        return {};
    const QString& value = line->value;
    QString unescaped;
    unescaped.reserve(value.size());
    for (qsizetype i = 0; i < value.size(); ++i) {
        QChar c = value.at(i);
        if (c == QLatin1Char('\\') && i + 1 < value.size()) {
            const QChar next = value.at(++i);
            c = (next == QLatin1Char('n') || next == QLatin1Char('N')) ? QChar(QLatin1Char('\n')) : next;
        }
        unescaped.append(c);
    }
    return visible(unescaped, limit).trimmed();
}

QString mailtoAddress(const QString& value)
{
    QString address = value.trimmed();
    if (address.startsWith(QLatin1String("mailto:"), Qt::CaseInsensitive))
        address = address.mid(7);
    return matches(addressRe(), address) ? address : QString();
}

// IANA id as given, then its last three or two path segments
// (`/mozilla.org/20050126_1/America/New_York`), then Qt's Windows-name table.
std::optional<QTimeZone> resolveZone(const QString& tzid)
{
    if (tzid.size() > kMaxTzidChars)
        return std::nullopt;
    const QStringList segments = tzid.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    const QStringList candidates{ tzid, segments.mid(std::max<qsizetype>(0, segments.size() - 3)).join(QLatin1Char('/')),
                                  segments.mid(std::max<qsizetype>(0, segments.size() - 2)).join(QLatin1Char('/')) };
    for (const QString& candidate : candidates) {
        if (!matches(ianaIdRe(), candidate))
            continue;
        const QTimeZone zone(candidate.toLatin1());
        if (zone.isValid())
            return zone;
    }
    const QByteArray iana = QTimeZone::windowsIdToDefaultIanaId(tzid.toUtf8());
    if (!iana.isEmpty()) {
        const QTimeZone zone(iana);
        if (zone.isValid())
            return zone;
    }
    return std::nullopt;
}

// A local time in `zone`. One inside a spring-forward gap moves forward by the
// gap, as Java's atZone does: it is read at the offset in force before it.
// Qt 6 does this itself; Qt 5.15 answers an invalid QDateTime instead.
QDateTime localAt(const QDate& day, const QTime& time, const QTimeZone& zone)
{
    const QDateTime at(day, time, zone);
    if (at.isValid())
        return at;
    const QDateTime asUtc(day, time, QTimeZone::utc());
    return asUtc.addSecs(-zone.offsetFromUtc(asUtc.addDays(-1))).toTimeZone(zone);
}

// Shaped like a DATE or DATE-TIME, but naming no real day or time.
bool isImpossibleDate(const QString& value)
{
    if (matches(dateRe(), value))
        return !QDate::fromString(value, QStringLiteral("yyyyMMdd")).isValid();
    const QRegularExpressionMatch m = dateTimeRe().match(value);
    return m.hasMatch()
        && (!QDate::fromString(m.captured(1), QStringLiteral("yyyyMMdd")).isValid()
            || !QTime::fromString(m.captured(2), QStringLiteral("HHmmss")).isValid());
}

struct EventTime
{
    QDateTime at;
    bool allDay = false;
    QString unknownZone;
};

std::optional<EventTime> eventTime(const ContentLine& line, const QTimeZone& localZone)
{
    const QString value = line.value.trimmed();
    if (line.params.value(QStringLiteral("VALUE")).compare(QLatin1String("DATE"), Qt::CaseInsensitive) == 0
        || matches(dateRe(), value)) {
        if (!matches(dateRe(), value))
            return std::nullopt;
        const QDate day = QDate::fromString(value, QStringLiteral("yyyyMMdd"));
        if (!day.isValid())
            return std::nullopt;
        return EventTime{ localAt(day, QTime(0, 0), localZone), true, {} };
    }
    const QRegularExpressionMatch m = dateTimeRe().match(value);
    if (!m.hasMatch())
        return std::nullopt;
    const QDate day = QDate::fromString(m.captured(1), QStringLiteral("yyyyMMdd"));
    const QTime time = QTime::fromString(m.captured(2), QStringLiteral("HHmmss"));
    if (!day.isValid() || !time.isValid())
        return std::nullopt;
    if (!m.captured(3).isEmpty())
        return EventTime{ QDateTime(day, time, QTimeZone::utc()), false, {} };
    const QString tzid = line.params.value(QStringLiteral("TZID")).trimmed();
    if (tzid.isEmpty())
        return EventTime{ localAt(day, time, localZone), false, {} };
    const std::optional<QTimeZone> zone = resolveZone(tzid);
    return EventTime{ localAt(day, time, zone.value_or(localZone)), false, zone ? QString() : visible(tzid, 64) };
}

// The REPLY carries no VTIMEZONE, so a TZID cannot be copied: a date stays a
// date, a floating or UTC time stays as written, a zoned time becomes UTC.
// Empty when it cannot be restated.
QString recurrenceIdLine(const ContentLine& line, const QTimeZone& localZone)
{
    const std::optional<EventTime> time = eventTime(line, localZone);
    if (!time || !time->unknownZone.isEmpty())
        return {};
    const QString value = line.value.trimmed();
    if (time->allDay)
        return QStringLiteral("RECURRENCE-ID;VALUE=DATE:") + value;
    if (line.params.value(QStringLiteral("TZID")).trimmed().isEmpty() || value.endsWith(QLatin1Char('Z')))
        return QStringLiteral("RECURRENCE-ID:") + value;
    return QStringLiteral("RECURRENCE-ID:")
        + QLocale::c().toString(time->at.toUTC(), QStringLiteral("yyyyMMdd'T'HHmmss'Z'"));
}

// RFC 5545 §3.3.6. Days and weeks are nominal, so they move with DST like a
// calendar would; hours, minutes and seconds are exact.
std::optional<QDateTime> addDuration(const QDateTime& start, const QString& value)
{
    const QRegularExpressionMatch m = durationRe().match(value);
    if (!m.hasMatch())
        return std::nullopt;
    bool any = false;
    for (int group = 2; group <= 6; ++group)
        any = any || !m.captured(group).isEmpty();
    if (!any)
        return std::nullopt;
    const auto n = [&m](int group) { return m.captured(group).isEmpty() ? 0LL : m.captured(group).toLongLong(); };
    const qint64 sign = m.captured(1) == QLatin1String("-") ? -1 : 1;
    const QDateTime end = start.addDays(sign * (7 * n(2) + n(3))).addSecs(sign * (3600 * n(4) + 60 * n(5) + n(6)));
    return end.isValid() ? std::optional<QDateTime>(end) : std::nullopt;
}

struct Parsed
{
    CalendarEvent event;
    bool isOverride = false;
};

std::optional<Parsed> toEvent(const QHash<QString, ContentLine>& props, const QStringList& attendees,
                              const QTimeZone& localZone)
{
    const auto prop = [&props](const char* name) -> const ContentLine* {
        const auto it = props.constFind(QLatin1String(name));
        return it == props.constEnd() ? nullptr : &it.value();
    };

    const ContentLine* dtstart = prop("DTSTART");
    const std::optional<EventTime> start = dtstart ? eventTime(*dtstart, localZone) : std::nullopt;
    if (!start)
        return std::nullopt;
    std::optional<EventTime> endTime;
    if (const ContentLine* dtend = prop("DTEND")) {
        endTime = eventTime(*dtend, localZone);
        if (!endTime)
            return std::nullopt;
    }
    std::optional<QDateTime> end;
    if (endTime)
        end = endTime->at;
    else if (const ContentLine* duration = prop("DURATION"))
        end = addDuration(start->at, duration->value.trimmed());
    if (!end)
        end = start->allDay ? start->at.addDays(1) : start->at;

    CalendarEvent event;
    // Untrimmed: the organizer matches the REPLY on the exact UID text.
    if (const ContentLine* uid = prop("UID")) {
        const QString& value = uid->value;
        const bool usable = !value.trimmed().isEmpty() && value.size() <= 1000
            && std::none_of(value.cbegin(), value.cend(), [](QChar c) { return isIsoControl(c.unicode()); });
        if (usable)
            event.uid = value;
    }
    if (const ContentLine* organizer = prop("ORGANIZER"))
        event.organizer = mailtoAddress(organizer->value);
    const ContentLine* recurrence = prop("RECURRENCE-ID");
    if (recurrence) {
        // Android's strict parse throws on a date-shaped value that is no
        // real date, which refuses the whole invite; anything else that is
        // not a date only means no RSVP.
        if (isImpossibleDate(recurrence->value.trimmed()))
            return std::nullopt;
        event.recurrenceId = recurrenceIdLine(*recurrence, localZone);
    }
    // A SEQUENCE we cannot read back exactly would make a valid-looking reply
    // to the wrong revision.
    std::optional<int> sequence = 0;
    if (const ContentLine* seq = prop("SEQUENCE")) {
        bool ok = false;
        const int value = seq->value.trimmed().toInt(&ok);
        sequence = ok && value >= 0 ? std::optional<int>(value) : std::nullopt;
    }
    event.sequence = sequence.value_or(0);
    event.summary = text(prop("SUMMARY"), 1000);
    event.location = text(prop("LOCATION"), 1000);
    event.description = text(prop("DESCRIPTION"), kMaxInviteDescriptionChars);
    event.begin = start->at;
    event.end = std::max(*end, start->at);
    event.allDay = start->allDay;
    if (const ContentLine* rrule = prop("RRULE")) {
        const QString value = rrule->value.trimmed();
        if (matches(rruleRe(), value))
            event.rrule = value;
    }
    event.unknownZone = !start->unknownZone.isEmpty() ? start->unknownZone
        : endTime                                    ? endTime->unknownZone
                                                     : QString();
    event.attendees = attendees;
    event.canReply = !event.uid.isEmpty() && !event.organizer.isEmpty() && sequence.has_value()
        && (!recurrence || !event.recurrenceId.isEmpty());
    return Parsed{ event, recurrence != nullptr };
}

std::optional<CalendarInvite> parse(const QString& text, const QTimeZone& localZone)
{
    QString normalised = text;
    normalised.replace(QLatin1String("\r\n"), QLatin1String("\n")).replace(QLatin1Char('\r'), QLatin1Char('\n'));
    QStringList lines;
    for (const QString& line : normalised.split(QLatin1Char('\n'))) {
        if (!line.trimmed().isEmpty())
            lines.append(line);
    }

    QStringList stack;
    QString method;
    std::optional<QHash<QString, ContentLine>> event;
    QStringList attendees;
    QVector<Parsed> events;
    int eventCount = 0;
    for (qsizetype index = 0; index < lines.size(); ++index) {
        const std::optional<ContentLine> line = contentLine(lines.at(index));
        if (!line)
            return std::nullopt;
        if (index == 0
            && !(line->name == QLatin1String("BEGIN")
                 && line->value.compare(QLatin1String("VCALENDAR"), Qt::CaseInsensitive) == 0))
            return std::nullopt;
        if (index > 0 && stack.isEmpty())
            return std::nullopt;
        if (line->name == QLatin1String("BEGIN")) {
            const QString component = line->value.trimmed().toUpper();
            if (component == QLatin1String("VCALENDAR") && !stack.isEmpty())
                return std::nullopt;
            if (component == QLatin1String("VEVENT")) {
                if (stack.size() != 1 || ++eventCount > kMaxInviteEvents)
                    return std::nullopt;
                event.emplace();
                attendees.clear();
            }
            stack.append(component);
        } else if (line->name == QLatin1String("END")) {
            if (stack.isEmpty() || stack.last() != line->value.trimmed().toUpper())
                return std::nullopt;
            if (stack.takeLast() == QLatin1String("VEVENT")) {
                if (!event)
                    return std::nullopt;
                std::optional<Parsed> parsed = toEvent(*event, attendees, localZone);
                if (!parsed)
                    return std::nullopt;
                events.append(*parsed);
                event.reset();
            }
        } else if (stack.size() == 1 && line->name == QLatin1String("METHOD")) {
            const QString value = line->value.trimmed().toUpper();
            method = matches(methodRe(), value) ? value : QString();
        } else if (stack.size() == 2 && stack.at(1) == QLatin1String("VEVENT") && event) {
            // Only the VEVENT's own properties: a VALARM inside it has a
            // DESCRIPTION too.
            if (line->name == QLatin1String("ATTENDEE")) {
                // Uncapped: kMaxInviteBytes already bounds the list, and a
                // cut could drop the user's alias.
                const QString address = mailtoAddress(line->value);
                if (!address.isEmpty())
                    attendees.append(address);
            } else if (!event->contains(line->name)) {
                event->insert(line->name, *line);
            }
        }
    }
    // No series master: an override must never be the headline event.
    if (!stack.isEmpty()
        || std::all_of(events.cbegin(), events.cend(), [](const Parsed& p) { return p.isOverride; }))
        return std::nullopt;
    std::stable_sort(events.begin(), events.end(),
                     [](const Parsed& a, const Parsed& b) { return !a.isOverride && b.isOverride; });
    CalendarInvite invite;
    invite.method = method;
    for (const Parsed& parsed : events)
        invite.events.append(parsed.event);
    return invite;
}

// RFC 5545 §3.1: at most 75 octets a line, never splitting a UTF-8 sequence.
QString fold(const QString& line)
{
    QString out;
    out.reserve(line.size() + line.size() / 70 * 3);
    int octets = 0;
    qsizetype i = 0;
    while (i < line.size()) {
        const QChar c = line.at(i);
        char32_t cp = c.unicode();
        int units = 1;
        if (c.isHighSurrogate() && i + 1 < line.size() && line.at(i + 1).isLowSurrogate()) {
            cp = QChar::surrogateToUcs4(c, line.at(i + 1));
            units = 2;
        }
        const int size = cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
        if (octets + size > 75) {
            out += QLatin1String("\r\n ");
            octets = 1;
        }
        out += line.mid(i, units);
        octets += size;
        i += units;
    }
    return out;
}

} // namespace

std::optional<CalendarInvite> parseCalendarInvite(const QByteArray& bytes, const QTimeZone& localZone)
{
    if (bytes.isEmpty() || bytes.size() > kMaxInviteBytes)
        return std::nullopt;
    QString text = QString::fromUtf8(unfold(bytes));
    if (text.startsWith(QChar(0xFEFF)))
        text.remove(0, 1);
    return parse(text, localZone);
}

std::optional<MailAttachmentInfo> inviteAttachment(const QVector<MailAttachmentInfo>& listed)
{
    const auto type = [](const MailAttachmentInfo& info) {
        return info.mimeType.section(QLatin1Char(';'), 0, 0).trimmed().toLower();
    };
    auto candidate = std::find_if(listed.cbegin(), listed.cend(), [&type](const MailAttachmentInfo& info) {
        return type(info) == QLatin1String("text/calendar");
    });
    if (candidate == listed.cend()) {
        candidate = std::find_if(listed.cbegin(), listed.cend(), [&type](const MailAttachmentInfo& info) {
            return type(info) == QLatin1String("application/ics")
                || info.name.trimmed().endsWith(QLatin1String(".ics"), Qt::CaseInsensitive);
        });
    }
    if (candidate == listed.cend() || candidate->size < 1 || candidate->size > kMaxInviteBytes)
        return std::nullopt;
    return *candidate;
}

QString rsvpPartstat(RsvpAnswer answer)
{
    switch (answer) {
    case RsvpAnswer::Accepted:
        return QStringLiteral("ACCEPTED");
    case RsvpAnswer::Tentative:
        return QStringLiteral("TENTATIVE");
    case RsvpAnswer::Declined:
        return QStringLiteral("DECLINED");
    }
    return {};
}

QString rsvpSubjectPrefix(RsvpAnswer answer)
{
    switch (answer) {
    case RsvpAnswer::Accepted:
        return QStringLiteral("Accepted");
    case RsvpAnswer::Tentative:
        return QStringLiteral("Tentative");
    case RsvpAnswer::Declined:
        return QStringLiteral("Declined");
    }
    return {};
}

std::optional<RsvpAnswer> rsvpAnswerFromPartstat(const QString& partstat)
{
    for (const RsvpAnswer answer : { RsvpAnswer::Accepted, RsvpAnswer::Tentative, RsvpAnswer::Declined }) {
        if (partstat == rsvpPartstat(answer))
            return answer;
    }
    return std::nullopt;
}

bool isRsvpAddress(const QString& address)
{
    return matches(addressRe(), address);
}

QString rsvpAttendee(const CalendarEvent& event, const QStringList& accountAddresses)
{
    QStringList own;
    for (const QString& address : accountAddresses) {
        const QString trimmed = address.trimmed();
        if (isRsvpAddress(trimmed))
            own.append(trimmed);
    }
    for (const QString& mine : own) {
        if (event.attendees.contains(mine, Qt::CaseInsensitive))
            return mine;
    }
    return own.isEmpty() ? QString() : own.first();
}

QString rsvpKey(const CalendarEvent& event)
{
    return event.uid + QChar(0) + event.recurrenceId + QChar(0) + QString::number(event.sequence) + QChar(0)
        + event.organizer.toLower();
}

std::optional<RsvpMail> rsvpMail(const CalendarEvent& event, const QString& attendee, RsvpAnswer answer,
                                 const QDateTime& now, const QString& sendAs)
{
    if (!event.canReply || !isRsvpAddress(attendee))
        return std::nullopt;
    QStringList lines{
        QStringLiteral("BEGIN:VCALENDAR"),
        QStringLiteral("VERSION:2.0"),
        QStringLiteral("PRODID:-//KyPost//Linux//EN"),
        QStringLiteral("METHOD:REPLY"),
        QStringLiteral("BEGIN:VEVENT"),
        QStringLiteral("UID:") + event.uid,
    };
    if (!event.recurrenceId.isEmpty())
        lines.append(event.recurrenceId);
    lines << QStringLiteral("SEQUENCE:%1").arg(event.sequence)
          << QStringLiteral("DTSTAMP:") + QLocale::c().toString(now.toUTC(), QStringLiteral("yyyyMMdd'T'HHmmss'Z'"))
          << QStringLiteral("ORGANIZER:mailto:") + event.organizer
          << QStringLiteral("ATTENDEE;PARTSTAT=%1:mailto:%2").arg(rsvpPartstat(answer), attendee)
          << QStringLiteral("END:VEVENT") << QStringLiteral("END:VCALENDAR");
    RsvpMail mail;
    for (const QString& line : lines)
        mail.ics += fold(line) + QLatin1String("\r\n");

    static const char* const verbs[] = { "accepted", "tentatively accepted", "declined" };
    const QString title = event.summary.simplified();
    mail.to = event.organizer;
    mail.from = sendAs;
    mail.subject = rsvpSubjectPrefix(answer) + QLatin1String(": ")
        + (title.isEmpty() ? QStringLiteral("Invitation") : title);
    mail.body = QStringLiteral("%1 has %2 this invitation.").arg(attendee, QLatin1String(verbs[static_cast<int>(answer)]));
    return mail;
}
