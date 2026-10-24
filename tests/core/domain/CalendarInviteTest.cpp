#include "domain/CalendarInvite.h"

#include "net/RelayMailSource.h"

#include <QElapsedTimer>
#include <QTest>

// Ported from kypost-android's CalendarInviteTest and CalendarRsvpTest, so
// the two clients read an invite and write a REPLY the same way.
namespace {

const QTimeZone& berlin()
{
    static const QTimeZone zone("Europe/Berlin");
    return zone;
}

QDateTime at(const char* iso)
{
    return QDateTime::fromString(QString::fromLatin1(iso), Qt::ISODate);
}

QByteArray ics(const QStringList& lines)
{
    return (lines.join(QStringLiteral("\r\n")) + QStringLiteral("\r\n")).toUtf8();
}

QByteArray inviteBytes(const QString& method, const QStringList& event)
{
    return ics(QStringList{ QStringLiteral("BEGIN:VCALENDAR"), QStringLiteral("VERSION:2.0"),
                            QStringLiteral("METHOD:") + method, QStringLiteral("BEGIN:VEVENT") }
               + event + QStringList{ QStringLiteral("END:VEVENT"), QStringLiteral("END:VCALENDAR") });
}

std::optional<CalendarInvite> parse(const QByteArray& bytes)
{
    return parseCalendarInvite(bytes, berlin());
}

CalendarEvent primaryOf(const QByteArray& bytes)
{
    const std::optional<CalendarInvite> parsed = parse(bytes);
    return parsed ? parsed->primary() : CalendarEvent{};
}

// The RSVP fixture: an Outlook-style REQUEST with the user listed under an alias.
QStringList request()
{
    return { QStringLiteral("UID:040000008200E00074C5B7101A82E008@outlook.example"),
             QStringLiteral("SEQUENCE:4"),
             QStringLiteral("DTSTART:20261012T100000Z"),
             QStringLiteral("SUMMARY:Quarterly\\nplanning"),
             QStringLiteral("ORGANIZER;CN=Boss:mailto:boss@example.com"),
             QStringLiteral("ATTENDEE;PARTSTAT=NEEDS-ACTION:mailto:me@example.com"),
             QStringLiteral("ATTENDEE;CN=\"Me, Alias\";RSVP=TRUE:MAILTO:Alias@Example.com") };
}

QStringList without(QStringList lines, const QString& prefix)
{
    lines.erase(std::remove_if(lines.begin(), lines.end(), [&prefix](const QString& l) { return l.startsWith(prefix); }),
                lines.end());
    return lines;
}

QStringList replacing(QStringList lines, const QString& prefix, const QString& line)
{
    for (QString& l : lines) {
        if (l.startsWith(prefix))
            l = line;
    }
    return lines;
}

// The override VEVENT of a series, parsed beside its master: an
// override-only invite does not parse.
CalendarEvent overrideEvent(const QStringList& extra)
{
    const QStringList lines = QStringList{ QStringLiteral("BEGIN:VCALENDAR"), QStringLiteral("METHOD:REQUEST"),
                                           QStringLiteral("BEGIN:VEVENT") }
        + request() + QStringList{ QStringLiteral("END:VEVENT"), QStringLiteral("BEGIN:VEVENT") } + request() + extra
        + QStringList{ QStringLiteral("END:VEVENT"), QStringLiteral("END:VCALENDAR") };
    const std::optional<CalendarInvite> parsed = parseCalendarInvite(ics(lines), QTimeZone::utc());
    return parsed ? parsed->events.last() : CalendarEvent{};
}

CalendarInvite rsvpInvite(const QStringList& event, const QString& method = QStringLiteral("REQUEST"))
{
    return parseCalendarInvite(inviteBytes(method, event), QTimeZone::utc()).value_or(CalendarInvite{});
}

QString unfolded(const QString& text)
{
    return QString(text).remove(QStringLiteral("\r\n "));
}

QStringList replyLines(const RsvpMail& mail)
{
    QString text = unfolded(mail.ics);
    text.chop(2);
    return text.split(QStringLiteral("\r\n"));
}

const QDateTime kNow = QDateTime(QDate(2026, 10, 10), QTime(8, 9, 10), QTimeZone::utc());

// KyPost-Server's mailmsg.CalendarReply checks, ported: what the relay
// refuses with 400.
bool relayAccepts(const QString& text, QString* why)
{
    const QByteArray bytes = text.toUtf8();
    if (bytes.size() > 64 * 1024 || !text.endsWith(QStringLiteral("\r\n"))) {
        *why = QStringLiteral("size or ending");
        return false;
    }
    QString body = text;
    body.chop(2);
    const QStringList lines = body.split(QStringLiteral("\r\n"));
    if (lines.first() != QStringLiteral("BEGIN:VCALENDAR")) {
        *why = QStringLiteral("first line");
        return false;
    }
    QStringList stack;
    int methods = 0, events = 0;
    for (qsizetype i = 0; i < lines.size(); ++i) {
        const QString& line = lines.at(i);
        if (line.isEmpty() || line.toUtf8().size() > 75) {
            *why = QStringLiteral("empty or unfolded: ") + line;
            return false;
        }
        for (const QChar c : line) {
            if ((c < QLatin1Char(' ') && c != QLatin1Char('\t')) || c == QChar(0x7f)) {
                *why = QStringLiteral("control in ") + line;
                return false;
            }
        }
        if (line.at(0) == QLatin1Char(' '))
            continue;
        if (i > 0 && stack.isEmpty()) {
            *why = QStringLiteral("content after END:VCALENDAR");
            return false;
        }
        const QString name = line.section(QLatin1Char(':'), 0, 0).section(QLatin1Char(';'), 0, 0).toUpper();
        const QString value = line.section(QLatin1Char(':'), 1).trimmed().toUpper();
        if (name == QStringLiteral("BEGIN")) {
            stack.append(value);
            events += value == QStringLiteral("VEVENT");
        } else if (name == QStringLiteral("END")) {
            if (stack.isEmpty() || stack.takeLast() != value) {
                *why = QStringLiteral("unbalanced END");
                return false;
            }
        } else if (name == QStringLiteral("METHOD")) {
            if (stack.size() != 1 || value != QStringLiteral("REPLY")) {
                *why = QStringLiteral("METHOD");
                return false;
            }
            ++methods;
        }
    }
    if (!stack.isEmpty() || methods != 1 || events != 1) {
        *why = QStringLiteral("structure");
        return false;
    }
    return true;
}

} // namespace

class CalendarInviteTest : public QObject
{
    Q_OBJECT

private slots:
    // Reading an invite.
    void readsAGoogleStyleRequest();
    void tzidIsResolvedToItsZoneNotTheDevices();
    void mozillaPrefixedTzidResolvesFromItsTail();
    void outlookWindowsZoneGoesThroughQtsTable();
    void unknownTzidFallsBackToFloatingAndSaysSo();
    void aTzidShapedLikeAPathIsNeverLookedUp();
    void hugeTzidIsUnknownNotQuadratic();
    void foldSplittingAUtf8SequenceIsRejoined();
    void impossibleDateIsRefusedNotRounded();
    void parameterNameCannotSwallowTheValue();
    void formatCharactersAreStrippedButJoinersKept();
    void overrideOnlyInviteIsRefused();
    void oneUnparseableEventRefusesTheWholeInvite();
    void unknownZoneOnTheEndIsReportedToo();
    void floatingTimeIsReadInTheLocalZone();
    void allDayEventIsLocalMidnightWithAnExclusiveEnd();
    void allDayWithoutAnEndLastsOneDay();
    void durationDaysAreNominalAcrossDst();
    void weekDurationIsUnderstood();
    void rrulePassesThroughAndJunkDoesNot();
    void cancelIsRecognised();
    void raisedSequenceOnARequestIsAnUpdate();
    void foldedLinesAreUnfolded();
    void alarmPropertiesDoNotOverrideTheEvents();
    void seriesMasterIsPrimaryEvenWhenAnOverrideComesFirst();
    void organizerKeepsTheAddressAndRejectsJunk();
    void bidiAndControlCharactersAreStrippedFromText();
    void missingMethodIsBlank();
    void malformedInputIsRefused();
    void oversizeInputIsRefusedBeforeParsing();
    void moreThanFiftyEventsIsRefused();
    void descriptionIsBounded();
    void inviteAttachmentPrefersTheCalendarPartAndRespectsTheCap();
    void anImpossibleRecurrenceIdDateRefusesTheInvite();
    void aTimeInASpringForwardGapMovesForward();
    void anUnknownZoneNameCannotReorderTheNotice();

    // Answering it.
    void replyCopiesUidAndSequenceAndAnswersAsTheListedAddress();
    void ourOwnParserReadsTheReplyBack();
    void eachAnswerIsItsPartstat();
    void overrideRecurrenceIdIsRestatedWithoutATzid();
    void unrestatableRecurrenceIdOrSequenceMeansNoRsvp();
    void uidIsCopiedVerbatimIncludingSurroundingSpaces();
    void aNewRevisionIsANewKey();
    void aCopyNamingAnotherOrganizerIsAnotherKey();
    void anAliasListedPastFiveHundredAttendeesIsStillFound();
    void aliasAnswerAlsoSendsFromTheAlias();
    void longUidIsFoldedWithoutSplittingUtf8();
    void unlistedUserFallsBackToThePrimaryAddress();
    void noUsableAccountAddressMeansNoReply();
    void rsvpIsOfferedOnlyForAnAnswerableRequestFromANewEnoughRelay();
    void attendeesAreCollectedNotJustTheFirst();
};

void CalendarInviteTest::readsAGoogleStyleRequest()
{
    const std::optional<CalendarInvite> parsed = parse(inviteBytes(
        QStringLiteral("REQUEST"),
        { QStringLiteral("UID:abc@google.com"), QStringLiteral("SEQUENCE:0"), QStringLiteral("DTSTART:20261012T130000Z"),
          QStringLiteral("DTEND:20261012T140000Z"), QStringLiteral("SUMMARY:Planning\\, Q4"),
          QStringLiteral("LOCATION:Room 4\\; Floor 2"), QStringLiteral("DESCRIPTION:Line one\\nLine two\\\\done"),
          QStringLiteral("ORGANIZER;CN=\"Boss: The Real One\":mailto:boss@example.com") }));
    QVERIFY(parsed.has_value());
    const CalendarEvent& event = parsed->primary();
    QCOMPARE(parsed->method, QStringLiteral("REQUEST"));
    QCOMPARE(event.uid, QStringLiteral("abc@google.com"));
    QCOMPARE(event.summary, QStringLiteral("Planning, Q4"));
    QCOMPARE(event.location, QStringLiteral("Room 4; Floor 2"));
    QCOMPARE(event.description, QStringLiteral("Line one\nLine two\\done"));
    QCOMPARE(event.organizer, QStringLiteral("boss@example.com"));
    QCOMPARE(event.begin, at("2026-10-12T13:00:00Z"));
    QCOMPARE(event.end, at("2026-10-12T14:00:00Z"));
    QVERIFY(!event.allDay);
    QVERIFY(!parsed->isUpdate());
    QVERIFY(event.canReply);
}

void CalendarInviteTest::tzidIsResolvedToItsZoneNotTheDevices()
{
    const CalendarEvent event = primaryOf(inviteBytes(
        QStringLiteral("REQUEST"),
        { QStringLiteral("DTSTART;TZID=America/New_York:20261012T090000"), QStringLiteral("DURATION:PT90M") }));
    QCOMPARE(event.begin, at("2026-10-12T13:00:00Z"));
    QCOMPARE(event.end, at("2026-10-12T14:30:00Z"));
    QCOMPARE(event.unknownZone, QString());
}

void CalendarInviteTest::mozillaPrefixedTzidResolvesFromItsTail()
{
    const CalendarEvent event = primaryOf(inviteBytes(
        QStringLiteral("REQUEST"), { QStringLiteral("DTSTART;TZID=/mozilla.org/20050126_1/America/New_York:20261012T090000") }));
    QCOMPARE(event.begin, at("2026-10-12T13:00:00Z"));
}

void CalendarInviteTest::outlookWindowsZoneGoesThroughQtsTable()
{
    const CalendarEvent event = primaryOf(
        inviteBytes(QStringLiteral("REQUEST"), { QStringLiteral("DTSTART;TZID=\"Pacific Standard Time\":20261012T090000") }));
    QCOMPARE(event.begin, at("2026-10-12T16:00:00Z"));
    QCOMPARE(event.unknownZone, QString());
}

void CalendarInviteTest::unknownTzidFallsBackToFloatingAndSaysSo()
{
    const CalendarEvent event =
        primaryOf(inviteBytes(QStringLiteral("REQUEST"), { QStringLiteral("DTSTART;TZID=Narnia Time:20261012T090000") }));
    QCOMPARE(event.begin, at("2026-10-12T07:00:00Z"));
    QCOMPARE(event.unknownZone, QStringLiteral("Narnia Time"));
}

// Linux only: Qt's TZ backend reads /usr/share/zoneinfo/<id>, so a TZID that
// is not shaped like an IANA name never reaches QTimeZone.
void CalendarInviteTest::aTzidShapedLikeAPathIsNeverLookedUp()
{
    for (const char* tzid : { "/usr/share/zoneinfo/UTC/..", "./UTC", "UTC/../UTC" }) {
        const CalendarEvent event = primaryOf(inviteBytes(
            QStringLiteral("REQUEST"), { QStringLiteral("DTSTART;TZID=%1:20261012T090000").arg(QLatin1String(tzid)) }));
        // Floating, in the local zone, and said to be unknown.
        QCOMPARE(event.unknownZone, QLatin1String(tzid));
        QCOMPARE(event.begin, at("2026-10-12T07:00:00Z"));
    }
}

void CalendarInviteTest::hugeTzidIsUnknownNotQuadratic()
{
    const QString tzid = QStringLiteral("/a").repeated(100000);
    QElapsedTimer timer;
    timer.start();
    const CalendarEvent event =
        primaryOf(inviteBytes(QStringLiteral("REQUEST"), { QStringLiteral("DTSTART;TZID=%1:20261012T090000").arg(tzid) }));
    QVERIFY(timer.elapsed() < 5000);
    QCOMPARE(event.unknownZone.size(), 64);
}

void CalendarInviteTest::foldSplittingAUtf8SequenceIsRejoined()
{
    const QByteArray e = QString(QChar(0xE9)).toUtf8();
    const QByteArray bytes = QByteArray("BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nDTSTART:20261012T100000Z\r\nSUMMARY:Caf")
        + e.at(0) + QByteArray("\r\n ") + e.at(1) + QByteArray("\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n");
    QCOMPARE(primaryOf(bytes).summary, QString::fromUtf16(u"Café"));
}

void CalendarInviteTest::impossibleDateIsRefusedNotRounded()
{
    QVERIFY(!parse(inviteBytes(QStringLiteral("REQUEST"), { QStringLiteral("DTSTART:20260231T100000Z") })));
    QVERIFY(!parse(inviteBytes(QStringLiteral("REQUEST"), { QStringLiteral("DTSTART;VALUE=DATE:20260231") })));
}

void CalendarInviteTest::parameterNameCannotSwallowTheValue()
{
    QVERIFY(!parse(inviteBytes(QStringLiteral("REQUEST"),
                               { QStringLiteral("DTSTART:20261012T100000Z"), QStringLiteral("X-JUNK;FOO:a=b:c") })));
}

void CalendarInviteTest::formatCharactersAreStrippedButJoinersKept()
{
    const QString summary = QString::fromUtf16(u"SUMMARY:a\u200Bb\u061Cc\uFEFFd \U0001F469\u200D\U0001F4BB\U000E0041");
    const CalendarEvent event =
        primaryOf(inviteBytes(QStringLiteral("REQUEST"), { QStringLiteral("DTSTART:20261012T100000Z"), summary }));
    // The tag character past the BMP goes too, which Android's per-unit walk misses.
    QCOMPARE(event.summary, QString::fromUtf16(u"abcd \U0001F469\u200D\U0001F4BB"));
    const CalendarEvent zeroWidth = primaryOf(
        inviteBytes(QStringLiteral("REQUEST"), { QStringLiteral("DTSTART:20261012T100000Z"),
                                                 QString::fromUtf16(u"ORGANIZER:mailto:ce\u200Bo@example.com") }));
    QCOMPARE(zeroWidth.organizer, QString());
}

void CalendarInviteTest::overrideOnlyInviteIsRefused()
{
    QVERIFY(!parse(inviteBytes(QStringLiteral("REQUEST"), { QStringLiteral("RECURRENCE-ID:20261019T100000Z"),
                                                            QStringLiteral("DTSTART:20261019T110000Z") })));
}

void CalendarInviteTest::oneUnparseableEventRefusesTheWholeInvite()
{
    QVERIFY(!parse(ics({ QStringLiteral("BEGIN:VCALENDAR"), QStringLiteral("METHOD:REQUEST"),
                         QStringLiteral("BEGIN:VEVENT"), QStringLiteral("SUMMARY:no start"), QStringLiteral("END:VEVENT"),
                         QStringLiteral("BEGIN:VEVENT"), QStringLiteral("DTSTART:20261012T100000Z"),
                         QStringLiteral("SUMMARY:valid"), QStringLiteral("END:VEVENT"), QStringLiteral("END:VCALENDAR") })));
    QVERIFY(!parse(inviteBytes(QStringLiteral("REQUEST"),
                               { QStringLiteral("DTSTART:20261012T100000Z"), QStringLiteral("DTEND:garbage") })));
}

void CalendarInviteTest::unknownZoneOnTheEndIsReportedToo()
{
    const CalendarEvent event = primaryOf(inviteBytes(QStringLiteral("REQUEST"),
                                                      { QStringLiteral("DTSTART;TZID=Europe/Berlin:20261012T090000"),
                                                        QStringLiteral("DTEND;TZID=Narnia:20261012T100000") }));
    QCOMPARE(event.unknownZone, QStringLiteral("Narnia"));
}

void CalendarInviteTest::floatingTimeIsReadInTheLocalZone()
{
    const CalendarEvent event = primaryOf(inviteBytes(QStringLiteral("REQUEST"), { QStringLiteral("DTSTART:20261012T090000") }));
    QCOMPARE(event.begin, at("2026-10-12T07:00:00Z"));
    // No DTEND or DURATION on a DATE-TIME start: zero length, not a guessed hour.
    QCOMPARE(event.end, event.begin);
}

void CalendarInviteTest::allDayEventIsLocalMidnightWithAnExclusiveEnd()
{
    const CalendarEvent event = primaryOf(inviteBytes(
        QStringLiteral("REQUEST"), { QStringLiteral("DTSTART;VALUE=DATE:20261012"), QStringLiteral("DTEND;VALUE=DATE:20261014") }));
    QVERIFY(event.allDay);
    QCOMPARE(event.begin, at("2026-10-11T22:00:00Z"));
    QCOMPARE(event.end, at("2026-10-13T22:00:00Z"));
}

void CalendarInviteTest::allDayWithoutAnEndLastsOneDay()
{
    const CalendarEvent event = primaryOf(inviteBytes(QStringLiteral("REQUEST"), { QStringLiteral("DTSTART;VALUE=DATE:20261012") }));
    QCOMPARE(event.end, at("2026-10-12T22:00:00Z"));
}

void CalendarInviteTest::durationDaysAreNominalAcrossDst()
{
    // Berlin leaves DST on 2026-10-25: one calendar day there is 25 hours.
    const CalendarEvent event = primaryOf(inviteBytes(
        QStringLiteral("REQUEST"), { QStringLiteral("DTSTART;TZID=Europe/Berlin:20261024T100000"), QStringLiteral("DURATION:P1D") }));
    QCOMPARE(event.end, at("2026-10-25T09:00:00Z"));
}

void CalendarInviteTest::weekDurationIsUnderstood()
{
    const CalendarEvent event = primaryOf(
        inviteBytes(QStringLiteral("REQUEST"), { QStringLiteral("DTSTART:20261012T100000Z"), QStringLiteral("DURATION:P1W") }));
    QCOMPARE(event.end, at("2026-10-19T10:00:00Z"));
}

void CalendarInviteTest::rrulePassesThroughAndJunkDoesNot()
{
    QCOMPARE(primaryOf(inviteBytes(QStringLiteral("REQUEST"), { QStringLiteral("DTSTART:20261012T100000Z"),
                                                                QStringLiteral("RRULE:FREQ=WEEKLY;BYDAY=MO;COUNT=10") }))
                 .rrule,
             QStringLiteral("FREQ=WEEKLY;BYDAY=MO;COUNT=10"));
    QCOMPARE(primaryOf(inviteBytes(QStringLiteral("REQUEST"),
                                   { QStringLiteral("DTSTART:20261012T100000Z"), QStringLiteral("RRULE:FREQ=WEEKLY<script>") }))
                 .rrule,
             QString());
}

void CalendarInviteTest::cancelIsRecognised()
{
    const std::optional<CalendarInvite> parsed = parse(
        inviteBytes(QStringLiteral("CANCEL"), { QStringLiteral("DTSTART:20261012T100000Z"), QStringLiteral("SEQUENCE:2") }));
    QVERIFY(parsed.has_value());
    QVERIFY(parsed->isCancelled());
    QVERIFY(!parsed->isUpdate());
}

void CalendarInviteTest::raisedSequenceOnARequestIsAnUpdate()
{
    const std::optional<CalendarInvite> parsed = parse(
        inviteBytes(QStringLiteral("REQUEST"), { QStringLiteral("DTSTART:20261012T100000Z"), QStringLiteral("SEQUENCE:3") }));
    QVERIFY(parsed.has_value());
    QVERIFY(parsed->isUpdate());
    QCOMPARE(parsed->primary().sequence, 3);
}

void CalendarInviteTest::foldedLinesAreUnfolded()
{
    const CalendarEvent event = primaryOf(inviteBytes(
        QStringLiteral("REQUEST"), { QStringLiteral("DTSTART:20261012T100000Z"), QStringLiteral("SUMMARY:A very long"),
                                     QStringLiteral("  title that folds"), QStringLiteral("DESCRIPTION:tab\r\n\tfolded") }));
    QCOMPARE(event.summary, QStringLiteral("A very long title that folds"));
    QCOMPARE(event.description, QStringLiteral("tabfolded"));
}

void CalendarInviteTest::alarmPropertiesDoNotOverrideTheEvents()
{
    const CalendarEvent event = primaryOf(inviteBytes(
        QStringLiteral("REQUEST"),
        { QStringLiteral("DTSTART:20261012T100000Z"), QStringLiteral("BEGIN:VALARM"), QStringLiteral("ACTION:DISPLAY"),
          QStringLiteral("DESCRIPTION:Reminder text"), QStringLiteral("END:VALARM"), QStringLiteral("DESCRIPTION:The real one") }));
    QCOMPARE(event.description, QStringLiteral("The real one"));
}

void CalendarInviteTest::seriesMasterIsPrimaryEvenWhenAnOverrideComesFirst()
{
    const std::optional<CalendarInvite> parsed = parse(ics(
        { QStringLiteral("BEGIN:VCALENDAR"), QStringLiteral("METHOD:REQUEST"), QStringLiteral("BEGIN:VEVENT"),
          QStringLiteral("RECURRENCE-ID:20261019T100000Z"), QStringLiteral("DTSTART:20261019T110000Z"),
          QStringLiteral("SUMMARY:Moved instance"), QStringLiteral("END:VEVENT"), QStringLiteral("BEGIN:VEVENT"),
          QStringLiteral("DTSTART:20261012T100000Z"), QStringLiteral("SUMMARY:Weekly"), QStringLiteral("END:VEVENT"),
          QStringLiteral("END:VCALENDAR") }));
    QVERIFY(parsed.has_value());
    QCOMPARE(parsed->primary().summary, QStringLiteral("Weekly"));
    QCOMPARE(parsed->events.size(), 2);
}

void CalendarInviteTest::organizerKeepsTheAddressAndRejectsJunk()
{
    QCOMPARE(primaryOf(inviteBytes(QStringLiteral("REQUEST"),
                                   { QStringLiteral("DTSTART:20261012T100000Z"), QStringLiteral("ORGANIZER:a@b.example") }))
                 .organizer,
             QStringLiteral("a@b.example"));
    QCOMPARE(primaryOf(inviteBytes(QStringLiteral("REQUEST"),
                                   { QStringLiteral("DTSTART:20261012T100000Z"),
                                     QString::fromUtf16(u"ORGANIZER:mailto:evil@x\u202Eelpmaxe.com") }))
                 .organizer,
             QString());
}

void CalendarInviteTest::bidiAndControlCharactersAreStrippedFromText()
{
    const CalendarEvent event = primaryOf(
        inviteBytes(QStringLiteral("REQUEST"), { QStringLiteral("DTSTART:20261012T100000Z"),
                                                 QString::fromUtf16(u"SUMMARY:Pay\u202Egnp.exe\a now") }));
    QCOMPARE(event.summary, QStringLiteral("Paygnp.exe now"));
}

void CalendarInviteTest::missingMethodIsBlank()
{
    const std::optional<CalendarInvite> parsed =
        parse(ics({ QStringLiteral("BEGIN:VCALENDAR"), QStringLiteral("BEGIN:VEVENT"),
                    QStringLiteral("DTSTART:20261012T100000Z"), QStringLiteral("END:VEVENT"), QStringLiteral("END:VCALENDAR") }));
    QVERIFY(parsed.has_value());
    QCOMPARE(parsed->method, QString());
}

void CalendarInviteTest::malformedInputIsRefused()
{
    const char* const refused[] = {
        "",
        "not a calendar at all",
        "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nDTSTART:20261012T100000Z\r\nEND:VEVENT\r\n",
        "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nDTSTART:20261012T100000Z\r\nEND:VTODO\r\nEND:VCALENDAR\r\n",
        "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nDTSTART:20261399T100000Z\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n",
        "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nDTSTART;TZID=\"open:20261012T100000\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n",
        "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nSUMMARY:no start\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n",
        "BEGIN:VCALENDAR\r\nEND:VCALENDAR\r\nBEGIN:VCALENDAR\r\nEND:VCALENDAR\r\n",
        "BEGIN:VCALENDAR\r\nBEGIN:VCALENDAR\r\nEND:VCALENDAR\r\nEND:VCALENDAR\r\n",
        "END:VCALENDAR\r\n",
    };
    for (const char* input : refused)
        QVERIFY2(!parse(QByteArray(input)), input);
    // An empty DURATION is ignored, not fatal.
    const std::optional<CalendarInvite> emptyDuration = parse(QByteArray(
        "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nDTSTART:20261012T100000Z\r\nDURATION:P\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n"));
    QVERIFY(emptyDuration.has_value());
    QCOMPARE(emptyDuration->primary().end, emptyDuration->primary().begin);
}

void CalendarInviteTest::oversizeInputIsRefusedBeforeParsing()
{
    const QString padding = QStringLiteral("X-PAD:") + QString(kMaxInviteBytes, QLatin1Char('a'));
    QVERIFY(!parse(inviteBytes(QStringLiteral("REQUEST"), { QStringLiteral("DTSTART:20261012T100000Z"), padding })));
}

void CalendarInviteTest::moreThanFiftyEventsIsRefused()
{
    const auto calendar = [](int n) {
        QStringList lines{ QStringLiteral("BEGIN:VCALENDAR") };
        for (int i = 0; i < n; ++i)
            lines << QStringLiteral("BEGIN:VEVENT") << QStringLiteral("DTSTART:20261012T100000Z") << QStringLiteral("END:VEVENT");
        lines << QStringLiteral("END:VCALENDAR");
        return ics(lines);
    };
    const std::optional<CalendarInvite> fifty = parse(calendar(kMaxInviteEvents));
    QVERIFY(fifty.has_value());
    QCOMPARE(fifty->events.size(), kMaxInviteEvents);
    QVERIFY(!parse(calendar(kMaxInviteEvents + 1)));
}

void CalendarInviteTest::descriptionIsBounded()
{
    const CalendarEvent event = primaryOf(inviteBytes(
        QStringLiteral("REQUEST"), { QStringLiteral("DTSTART:20261012T100000Z"),
                                     QStringLiteral("DESCRIPTION:") + QString(kMaxInviteDescriptionChars + 50, QLatin1Char('d')) }));
    QCOMPARE(event.description.size(), kMaxInviteDescriptionChars);
}

void CalendarInviteTest::inviteAttachmentPrefersTheCalendarPartAndRespectsTheCap()
{
    const MailAttachmentInfo pdf{ 0, QStringLiteral("agenda.pdf"), QStringLiteral("application/pdf"), 10, {} };
    const MailAttachmentInfo file{ 1, QStringLiteral("invite.ics"), QStringLiteral("application/ics"), 900, {} };
    const MailAttachmentInfo part{ 2, QStringLiteral("invite.ics"), QStringLiteral("text/calendar; method=REQUEST"), 900,
                                   QStringLiteral("REQUEST") };
    QCOMPARE(inviteAttachment({ pdf, file, part }), std::optional<MailAttachmentInfo>(part));
    QCOMPARE(inviteAttachment({ pdf, file }), std::optional<MailAttachmentInfo>(file));
    QVERIFY(!inviteAttachment({ pdf }));
    MailAttachmentInfo tooBig = part;
    tooBig.size = kMaxInviteBytes + 1;
    QVERIFY(!inviteAttachment({ tooBig }));
    MailAttachmentInfo empty = part;
    empty.size = 0;
    QVERIFY(!inviteAttachment({ empty }));
}

// Android's LocalDateTime.parse (STRICT) throws on these, and the throw
// refuses the whole invite; garbage that is not date-shaped only means no RSVP.
void CalendarInviteTest::anImpossibleRecurrenceIdDateRefusesTheInvite()
{
    for (const char* rid : { "RECURRENCE-ID:20260230T100000", "RECURRENCE-ID;VALUE=DATE:20260230",
                             "RECURRENCE-ID:20261019T250000Z" }) {
        const QStringList lines = QStringList{ QStringLiteral("BEGIN:VCALENDAR"), QStringLiteral("METHOD:REQUEST"),
                                               QStringLiteral("BEGIN:VEVENT") }
            + request() + QStringList{ QStringLiteral("END:VEVENT"), QStringLiteral("BEGIN:VEVENT") } + request()
            + QStringList{ QLatin1String(rid), QStringLiteral("END:VEVENT"), QStringLiteral("END:VCALENDAR") };
        QVERIFY2(!parseCalendarInvite(ics(lines), QTimeZone::utc()), rid);
    }
    QVERIFY(!overrideEvent({ QStringLiteral("RECURRENCE-ID:garbage") }).uid.isEmpty());
}

// Java's atZone and atStartOfDay move a skipped local time forward by the gap.
void CalendarInviteTest::aTimeInASpringForwardGapMovesForward()
{
    // Berlin skips 02:00-03:00 on 2026-03-29: 02:30 is read as 03:30 CEST.
    QCOMPARE(primaryOf(inviteBytes(QStringLiteral("REQUEST"), { QStringLiteral("DTSTART;TZID=Europe/Berlin:20260329T023000") }))
                 .begin,
             at("2026-03-29T01:30:00Z"));
    // Santiago skips midnight on 2026-09-06: the day starts at 01:00 -03.
    const std::optional<CalendarInvite> allDay = parseCalendarInvite(
        inviteBytes(QStringLiteral("REQUEST"), { QStringLiteral("DTSTART;VALUE=DATE:20260906") }), QTimeZone("America/Santiago"));
    QVERIFY(allDay.has_value());
    QCOMPARE(allDay->primary().begin, at("2026-09-06T04:00:00Z"));
}

void CalendarInviteTest::anUnknownZoneNameCannotReorderTheNotice()
{
    const CalendarEvent event = primaryOf(inviteBytes(
        QStringLiteral("REQUEST"),
        { QStringLiteral("DTSTART;TZID=Nar") + QChar(0x202E) + QStringLiteral("nia:20261012T090000") }));
    QCOMPARE(event.unknownZone, QStringLiteral("Narnia"));
}

void CalendarInviteTest::replyCopiesUidAndSequenceAndAnswersAsTheListedAddress()
{
    const CalendarEvent event = rsvpInvite(request()).primary();
    const QString attendee = rsvpAttendee(event, { QStringLiteral("primary@example.com"), QStringLiteral("alias@example.com") });
    QCOMPARE(attendee, QStringLiteral("alias@example.com"));

    const std::optional<RsvpMail> mail = rsvpMail(event, attendee, RsvpAnswer::Accepted, kNow);
    QVERIFY(mail.has_value());
    QString why;
    QVERIFY2(relayAccepts(mail->ics, &why), qPrintable(why));
    const QStringList lines = replyLines(*mail);
    QVERIFY(lines.contains(QStringLiteral("UID:040000008200E00074C5B7101A82E008@outlook.example")));
    QVERIFY(lines.contains(QStringLiteral("SEQUENCE:4")));
    QVERIFY(lines.contains(QStringLiteral("DTSTAMP:20261010T080910Z")));
    QVERIFY(lines.contains(QStringLiteral("ORGANIZER:mailto:boss@example.com")));
    QVERIFY(lines.contains(QStringLiteral("ATTENDEE;PARTSTAT=ACCEPTED:mailto:alias@example.com")));
    QVERIFY(std::none_of(lines.cbegin(), lines.cend(), [](const QString& l) { return l.startsWith(QStringLiteral("RECURRENCE-ID")); }));

    QCOMPARE(mail->to, QStringLiteral("boss@example.com"));
    QCOMPARE(mail->subject, QStringLiteral("Accepted: Quarterly planning"));
    QCOMPARE(mail->body, QStringLiteral("alias@example.com has accepted this invitation."));
}

void CalendarInviteTest::ourOwnParserReadsTheReplyBack()
{
    const CalendarEvent event = rsvpInvite(request()).primary();
    const std::optional<RsvpMail> mail = rsvpMail(event, QStringLiteral("me@example.com"), RsvpAnswer::Declined, kNow);
    QVERIFY(mail.has_value());
    // The card parser insists on DTSTART, which a REPLY need not carry; add one to read it back.
    const QString withStart =
        QString(mail->ics).replace(QStringLiteral("END:VEVENT"), QStringLiteral("DTSTART:20261012T100000Z\r\nEND:VEVENT"));
    const std::optional<CalendarInvite> parsed = parseCalendarInvite(withStart.toUtf8(), QTimeZone::utc());
    QVERIFY(parsed.has_value());
    QCOMPARE(parsed->method, QStringLiteral("REPLY"));
    QCOMPARE(parsed->primary().uid, event.uid);
    QCOMPARE(parsed->primary().attendees, QStringList{ QStringLiteral("me@example.com") });
}

void CalendarInviteTest::eachAnswerIsItsPartstat()
{
    const CalendarEvent event = rsvpInvite(request()).primary();
    for (const RsvpAnswer answer : { RsvpAnswer::Accepted, RsvpAnswer::Tentative, RsvpAnswer::Declined }) {
        const std::optional<RsvpMail> mail = rsvpMail(event, QStringLiteral("me@example.com"), answer, kNow);
        QVERIFY(mail.has_value());
        QVERIFY(replyLines(*mail).contains(QStringLiteral("ATTENDEE;PARTSTAT=%1:mailto:me@example.com").arg(rsvpPartstat(answer))));
        QCOMPARE(rsvpAnswerFromPartstat(rsvpPartstat(answer)), std::optional<RsvpAnswer>(answer));
    }
    QVERIFY(!rsvpAnswerFromPartstat(QStringLiteral("NEEDS-ACTION")));
    QVERIFY(!rsvpAnswerFromPartstat(QStringLiteral("accepted")));
}

void CalendarInviteTest::overrideRecurrenceIdIsRestatedWithoutATzid()
{
    // The REPLY carries no VTIMEZONE, so a zoned RECURRENCE-ID must become UTC.
    const auto reply = [](const char* rid) {
        const std::optional<RsvpMail> mail =
            rsvpMail(overrideEvent({ QLatin1String(rid) }), QStringLiteral("me@example.com"), RsvpAnswer::Tentative, kNow);
        return mail ? unfolded(mail->ics) : QString();
    };
    QVERIFY(reply("RECURRENCE-ID;TZID=Europe/Berlin:20261019T100000").contains(QStringLiteral("\r\nRECURRENCE-ID:20261019T080000Z\r\n")));
    QVERIFY(reply("RECURRENCE-ID:20261019T100000Z").contains(QStringLiteral("\r\nRECURRENCE-ID:20261019T100000Z\r\n")));
    QVERIFY(reply("RECURRENCE-ID;VALUE=DATE:20261019").contains(QStringLiteral("\r\nRECURRENCE-ID;VALUE=DATE:20261019\r\n")));
    QVERIFY(reply("RECURRENCE-ID;X-JUNK=1:20261019T100000").contains(QStringLiteral("\r\nRECURRENCE-ID:20261019T100000\r\n")));
}

void CalendarInviteTest::unrestatableRecurrenceIdOrSequenceMeansNoRsvp()
{
    QVERIFY(!overrideEvent({ QStringLiteral("RECURRENCE-ID;TZID=Narnia:20261019T100000") }).canReply);
    QVERIFY(!overrideEvent({ QStringLiteral("RECURRENCE-ID:garbage") }).canReply);
    const CalendarInvite badSequence =
        rsvpInvite(replacing(request(), QStringLiteral("SEQUENCE"), QStringLiteral("SEQUENCE:99999999999")));
    QVERIFY(!badSequence.offersRsvp(QStringLiteral("REQUEST")));
    QVERIFY(!rsvpMail(badSequence.primary(), QStringLiteral("me@example.com"), RsvpAnswer::Accepted, kNow));
}

void CalendarInviteTest::uidIsCopiedVerbatimIncludingSurroundingSpaces()
{
    const CalendarEvent event =
        rsvpInvite(without(request(), QStringLiteral("UID")) << QStringLiteral("UID: event@example.com ")).primary();
    QCOMPARE(event.uid, QStringLiteral(" event@example.com "));
    const std::optional<RsvpMail> mail = rsvpMail(event, QStringLiteral("me@example.com"), RsvpAnswer::Accepted, kNow);
    QVERIFY(mail.has_value());
    QVERIFY(replyLines(*mail).contains(QStringLiteral("UID: event@example.com ")));
    // Blank is no UID at all.
    QVERIFY(!rsvpInvite(without(request(), QStringLiteral("UID")) << QStringLiteral("UID:   ")).offersRsvp(QStringLiteral("REQUEST")));
}

void CalendarInviteTest::aNewRevisionIsANewKey()
{
    const CalendarEvent seq4 = rsvpInvite(request()).primary();
    const CalendarEvent seq5 = rsvpInvite(replacing(request(), QStringLiteral("SEQUENCE"), QStringLiteral("SEQUENCE:5"))).primary();
    QVERIFY(rsvpKey(seq4) != rsvpKey(seq5));
    QVERIFY(rsvpKey(seq4) != rsvpKey(overrideEvent({ QStringLiteral("RECURRENCE-ID:20261019T100000Z") })));
    QCOMPARE(rsvpKey(seq4), rsvpKey(rsvpInvite(request()).primary()));
}

void CalendarInviteTest::aCopyNamingAnotherOrganizerIsAnotherKey()
{
    const CalendarEvent genuine = rsvpInvite(request()).primary();
    const CalendarEvent copy = rsvpInvite(replacing(request(), QStringLiteral("ORGANIZER"),
                                                    QStringLiteral("ORGANIZER:mailto:other@attacker.example")))
                                   .primary();
    QVERIFY(rsvpKey(genuine) != rsvpKey(copy));
    // Case is not a different organizer.
    const CalendarEvent shouted = rsvpInvite(replacing(request(), QStringLiteral("ORGANIZER"),
                                                       QStringLiteral("ORGANIZER:mailto:BOSS@example.com")))
                                      .primary();
    QCOMPARE(rsvpKey(shouted), rsvpKey(genuine));
}

void CalendarInviteTest::anAliasListedPastFiveHundredAttendeesIsStillFound()
{
    QStringList event = request();
    for (int i = 1; i <= 500; ++i)
        event << QStringLiteral("ATTENDEE:mailto:guest%1@example.com").arg(i);
    event << QStringLiteral("ATTENDEE:mailto:late-alias@example.com");
    const CalendarEvent parsed = rsvpInvite(event).primary();
    const QString attendee =
        rsvpAttendee(parsed, { QStringLiteral("primary@example.com"), QStringLiteral("late-alias@example.com") });
    QCOMPARE(attendee, QStringLiteral("late-alias@example.com"));
    const std::optional<RsvpMail> mail = rsvpMail(parsed, attendee, RsvpAnswer::Accepted, kNow);
    QVERIFY(mail && replyLines(*mail).contains(QStringLiteral("ATTENDEE;PARTSTAT=ACCEPTED:mailto:late-alias@example.com")));
}

void CalendarInviteTest::aliasAnswerAlsoSendsFromTheAlias()
{
    const CalendarEvent event = rsvpInvite(request()).primary();
    QCOMPARE(rsvpMail(event, QStringLiteral("alias@example.com"), RsvpAnswer::Accepted, kNow, QStringLiteral("alias@example.com"))->from,
             QStringLiteral("alias@example.com"));
    QCOMPARE(rsvpMail(event, QStringLiteral("me@example.com"), RsvpAnswer::Accepted, kNow)->from, QString());
}

void CalendarInviteTest::longUidIsFoldedWithoutSplittingUtf8()
{
    const QString uid = QString(300, QChar(0xE9)) + QStringLiteral("\U0001F600@example.com");
    const CalendarEvent event = rsvpInvite(without(request(), QStringLiteral("UID")) << QStringLiteral("UID:") + uid).primary();
    const std::optional<RsvpMail> mail = rsvpMail(event, QStringLiteral("me@example.com"), RsvpAnswer::Accepted, kNow);
    QVERIFY(mail.has_value());
    QString why;
    QVERIFY2(relayAccepts(mail->ics, &why), qPrintable(why));
    QVERIFY(replyLines(*mail).contains(QStringLiteral("UID:") + uid));
    // Every physical line decodes on its own: no fold landed inside a sequence.
    for (const QString& line : mail->ics.split(QStringLiteral("\r\n")))
        QVERIFY(line.isEmpty() || !line.back().isHighSurrogate());
}

void CalendarInviteTest::unlistedUserFallsBackToThePrimaryAddress()
{
    QCOMPARE(rsvpAttendee(rsvpInvite(request()).primary(), { QStringLiteral(" primary@example.com "), QStringLiteral("other@example.com") }),
             QStringLiteral("primary@example.com"));
}

void CalendarInviteTest::noUsableAccountAddressMeansNoReply()
{
    const CalendarEvent event = rsvpInvite(request()).primary();
    QCOMPARE(rsvpAttendee(event, {}), QString());
    // A relay-supplied address carrying CRLF could inject an ics line; it is not an address.
    QCOMPARE(rsvpAttendee(event, { QStringLiteral("me@example.com\r\nATTENDEE:mailto:x@y") }), QString());
    QCOMPARE(rsvpAttendee(event, { QStringLiteral("not-an-address") }), QString());
    QVERIFY(!rsvpMail(event, QStringLiteral("bad address"), RsvpAnswer::Accepted, kNow));
    QVERIFY(!rsvpMail(event, QStringLiteral("me@example.com\nX:y"), RsvpAnswer::Accepted, kNow));
}

void CalendarInviteTest::rsvpIsOfferedOnlyForAnAnswerableRequestFromANewEnoughRelay()
{
    QVERIFY(rsvpInvite(request()).offersRsvp(QStringLiteral("REQUEST")));
    // An older relay lists no calendarMethod and would drop calendarReply.
    QVERIFY(!rsvpInvite(request()).offersRsvp(QString()));
    QVERIFY(!rsvpInvite(request(), QStringLiteral("CANCEL")).offersRsvp(QStringLiteral("CANCEL")));
    QVERIFY(!rsvpInvite(request(), QStringLiteral("PUBLISH")).offersRsvp(QStringLiteral("PUBLISH")));
    QVERIFY(!rsvpInvite(without(request(), QStringLiteral("ORGANIZER"))).offersRsvp(QStringLiteral("REQUEST")));
    QVERIFY(!rsvpInvite(without(request(), QStringLiteral("UID"))).offersRsvp(QStringLiteral("REQUEST")));
}

void CalendarInviteTest::attendeesAreCollectedNotJustTheFirst()
{
    QCOMPARE(rsvpInvite(request()).primary().attendees,
             (QStringList{ QStringLiteral("me@example.com"), QStringLiteral("Alias@Example.com") }));
}

QTEST_GUILESS_MAIN(CalendarInviteTest)
#include "CalendarInviteTest.moc"
