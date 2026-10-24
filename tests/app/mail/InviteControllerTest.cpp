#include "mail/InviteController.h"

#include "domain/DevicePairing.h"
#include "domain/PairingStore.h"
#include "net/NetworkExecutor.h"
#include "stores/SecureStoreFile.h"

#include "../../core/net/FakeRelayServer.h"

#include <QJsonArray>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {

QByteArray inviteIcs(const QByteArray& organizer = "boss@example.com", const QByteArray& method = "REQUEST")
{
    return "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nMETHOD:" + method
        + "\r\nBEGIN:VEVENT\r\nUID:evt-1@example.com\r\nSEQUENCE:2\r\nDTSTART:20261112T150000Z\r\n"
          "DTEND:20261112T160000Z\r\nSUMMARY:Launch review\r\nLOCATION:Room 4\r\nORGANIZER;CN=Boss:mailto:"
        + organizer
        + "\r\nATTENDEE:mailto:me@example.com\r\nATTENDEE:mailto:alias@example.com\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";
}

const char* const kBootstrap =
    R"({"hasIdentity":false,"protection":"","suggestedUserIDs":["me@example.com","alias@example.com"]})";
const char* const kSentAck = R"({"ok":true,"sentSaved":true,"warning":"","calendarReply":true})";

QVariantList listing(const QString& calendarMethod = QStringLiteral("REQUEST"))
{
    return { QVariantMap{ { QStringLiteral("index"), 0 }, { QStringLiteral("name"), QStringLiteral("agenda.pdf") },
                          { QStringLiteral("mimeType"), QStringLiteral("application/pdf") }, { QStringLiteral("size"), 10 } },
             QVariantMap{ { QStringLiteral("index"), 1 }, { QStringLiteral("name"), QStringLiteral("invite.ics") },
                          { QStringLiteral("mimeType"), QStringLiteral("text/calendar") }, { QStringLiteral("size"), 900 },
                          { QStringLiteral("calendarMethod"), calendarMethod } } };
}

DevicePairing pairingFor(quint16 port, const QString& subscriber, const QString& device)
{
    DevicePairing pairing;
    pairing.subscriberId = subscriber;
    pairing.serverBaseUrl = QStringLiteral("http://127.0.0.1:%1").arg(port);
    pairing.deviceId = device;
    pairing.deviceSecret = QStringLiteral("secret-") + device;
    return pairing;
}

struct Harness
{
    // `first` answers ahead of everything else: the fake's first match wins.
    explicit Harness(const QByteArray& sendResponse = httpResponse(200, "OK", kSentAck),
                     const QList<QPair<QByteArray, QByteArray>>& first = {})
        : fake(httpResponse(404, "Not Found", "404 page not found", "text/plain"))
        , secureStore(dir.path())
        , pairingStore(secureStore)
        , executor(3000)
        , controller(executor, pairingStore,
                     [] { return QDateTime(QDate(2026, 10, 23), QTime(9, 0), QTimeZone::utc()); }, QTimeZone::utc())
    {
        for (const auto& [needle, response] : first)
            fake.setResponseForPath(needle, response);
        fake.setResponseForPath("messageId=m2", httpResponse(200, "OK", inviteIcs("other@attacker.example"), "text/calendar"));
        fake.setResponseForPath("/api/mail/attachment?", httpResponse(200, "OK", inviteIcs(), "text/calendar"));
        fake.setResponseForPath("/api/pgp/bootstrap", httpResponse(200, "OK", kBootstrap));
        fake.setResponseForPath("/api/mail/send", sendResponse);
        saved = pairingStore.save(pairingFor(fake.port(), QStringLiteral("sub-1"), QStringLiteral("dev-1")));
    }
    ~Harness() { executor.shutdown(); }

    // Loads m1's invite and returns its card, or {} on timeout.
    QVariantMap loadCard(const QString& messageId = QStringLiteral("m1"), const QVariantList& attachments = listing(),
                         const QString& sender = QStringLiteral("Boss <boss@example.com>"))
    {
        QSignalSpy loaded(&controller, &InviteController::inviteLoaded);
        controller.load(QStringLiteral("INBOX"), messageId, attachments, sender, false);
        if (!loaded.wait(10000))
            return {};
        return loaded.first().at(1).toMap();
    }

    QVariantMap stateOf(int token) const { return controller.states().value(QString::number(token)).toMap(); }

    // requestRsvp, then wait for the confirmation it raises.
    bool ask(int token, const QString& partstat = QStringLiteral("ACCEPTED"))
    {
        controller.requestRsvp(token, partstat);
        return QTest::qWaitFor([this] { return !controller.confirmation().isEmpty(); }, 10000);
    }

    int sends() const
    {
        int n = 0;
        for (const QByteArray& request : fake.receivedRequests())
            n += request.startsWith("POST /api/mail/send ");
        return n;
    }

    QJsonObject lastSendBody() const
    {
        for (qsizetype i = fake.receivedRequests().size() - 1; i >= 0; --i) {
            const QByteArray& request = fake.receivedRequests().at(i);
            if (request.startsWith("POST /api/mail/send "))
                return QJsonDocument::fromJson(request.mid(request.indexOf("\r\n\r\n") + 4)).object();
        }
        return {};
    }

    FakeRelayServer fake;
    QTemporaryDir dir;
    SecureStoreFile secureStore;
    PairingStore pairingStore;
    NetworkExecutor executor;
    InviteController controller;
    bool saved = false;
};

} // namespace

class InviteControllerTest : public QObject
{
    Q_OBJECT

private slots:
    void loadReadsTheListedPartIntoACard();
    void aRelayWithoutCalendarMethodOffersNoButtons();
    void aMessageWithoutAnInviteGetsNoCard();
    void anAcceptIsConfirmedThenSentPlainToTheOrganizer();
    void anAliasTheInviteListsIsAnsweredAndSentFrom();
    void aReplacedAccountNeverSendsTheConfirmedAnswer();
    void aCardFromAReplacedAccountCannotBeAnswered();
    void lockingWithdrawsTheConfirmationAndSendsNothing();
    void aConfirmationAnsweredUnderTheLockSendsNothing();
    void cancellingReleasesTheRevision();
    void anAnsweredRevisionCannotBeAnsweredAgain();
    void aSharedUidCannotRedirectAnotherCardsAnswer();
    void aTwoHundredWithoutTheAckIsPlainMailNotAnRsvp();
    void aRefusalReArmsTheButtons();
    void aBadGatewayMayHaveBeenSentUntilReopened();
    void aCompletionForAReplacedAccountRecordsNothing();
    void noAccountAddressMeansNoConfirmation();
    void aCardForAReplacedAccountIsNotShown();
    void forgettingACardDuringTheLookupLeavesNothingAsked();
    void aSecondAnswerWaitsForTheFirstConfirmation();
    void aLoginThatIsNotAnAddressAnswersFromTheAlias();
};

void InviteControllerTest::loadReadsTheListedPartIntoACard()
{
    Harness h;
    QVERIFY(h.saved);
    const QVariantMap card = h.loadCard();
    QVERIFY(!card.isEmpty());
    // The text/calendar part, by its own index; held in memory, not saved.
    QVERIFY(h.fake.receivedRequests().at(0).startsWith("GET /api/mail/attachment?mailbox=INBOX&messageId=m1&index=1 "));
    QCOMPARE(card.value(QStringLiteral("title")).toString(), QStringLiteral("Launch review"));
    QCOMPARE(card.value(QStringLiteral("eyebrow")).toString(), QStringLiteral("Calendar invite · REQUEST"));
    const QStringList details = card.value(QStringLiteral("details")).toStringList();
    QVERIFY(details.contains(QStringLiteral("Where: Room 4")));
    QVERIFY(details.contains(QStringLiteral("Organizer: boss@example.com")));
    // SEQUENCE 2 on a REQUEST: an update.
    QCOMPARE(card.value(QStringLiteral("notices")).toStringList(),
             QStringList{ QStringLiteral("This invite was updated. Check your calendar.") });
    QVERIFY(card.value(QStringLiteral("offersRsvp")).toBool());
    QVERIFY(h.stateOf(card.value(QStringLiteral("token")).toInt()).value(QStringLiteral("answerable")).toBool());
}

void InviteControllerTest::aRelayWithoutCalendarMethodOffersNoButtons()
{
    Harness h;
    const QVariantMap card = h.loadCard(QStringLiteral("m1"), listing(QString()), QStringLiteral("someone@else.example"));
    QVERIFY(!card.isEmpty());
    QVERIFY(!card.value(QStringLiteral("offersRsvp")).toBool());
    QVERIFY(card.value(QStringLiteral("notices")).toStringList().first().startsWith(QStringLiteral("The organizer (boss@example.com) is not the sender")));
    h.controller.requestRsvp(card.value(QStringLiteral("token")).toInt(), QStringLiteral("ACCEPTED"));
    QTest::qWait(200);
    QVERIFY(h.controller.confirmation().isEmpty());
    QCOMPARE(h.fake.receivedRequests().size(), 1);
}

void InviteControllerTest::aMessageWithoutAnInviteGetsNoCard()
{
    Harness h;
    QSignalSpy loaded(&h.controller, &InviteController::inviteLoaded);
    h.controller.load(QStringLiteral("INBOX"), QStringLiteral("m1"), { listing().first() }, {}, false);
    QTest::qWait(200);
    QCOMPARE(loaded.count(), 0);
    QCOMPARE(h.fake.connectionCount(), 0);
}

void InviteControllerTest::anAcceptIsConfirmedThenSentPlainToTheOrganizer()
{
    Harness h;
    const int token = h.loadCard().value(QStringLiteral("token")).toInt();
    QVERIFY(h.ask(token));
    QVERIFY(!h.stateOf(token).value(QStringLiteral("answerable")).toBool());
    // Asking is not sending.
    QVERIFY(!h.stateOf(token).value(QStringLiteral("sending")).toBool());
    QCOMPARE(h.sends(), 0);
    const QVariantMap confirmation = h.controller.confirmation();
    QCOMPARE(confirmation.value(QStringLiteral("token")).toInt(), token);
    QVERIFY(confirmation.value(QStringLiteral("text")).toString().startsWith(
        QStringLiteral("Reply \"Accepted\" to boss@example.com as me@example.com.")));
    QVERIFY(confirmation.value(QStringLiteral("text")).toString().contains(QStringLiteral("unencrypted and unsigned")));

    h.controller.confirmRsvp(token);
    QVERIFY(h.controller.confirmation().isEmpty());
    QVERIFY(h.stateOf(token).value(QStringLiteral("sending")).toBool());
    QTRY_COMPARE_WITH_TIMEOUT(h.stateOf(token).value(QStringLiteral("message")).toString(), QStringLiteral("RSVP sent"), 10000);
    QVERIFY(!h.stateOf(token).value(QStringLiteral("answerable")).toBool());

    QCOMPARE(h.sends(), 1);
    const QJsonObject body = h.lastSendBody();
    QCOMPARE(body.value(QStringLiteral("to")).toString(), QStringLiteral("boss@example.com"));
    QVERIFY(!body.contains(QStringLiteral("from")));
    QCOMPARE(body.value(QStringLiteral("subject")).toString(), QStringLiteral("Accepted: Launch review"));
    QVERIFY(!body.value(QStringLiteral("encrypt")).toBool());
    QVERIFY(!body.value(QStringLiteral("sign")).toBool());
    const QString ics = body.value(QStringLiteral("calendarReply")).toObject().value(QStringLiteral("ics")).toString();
    QVERIFY(ics.contains(QStringLiteral("\r\nMETHOD:REPLY\r\n")));
    QVERIFY(ics.contains(QStringLiteral("\r\nUID:evt-1@example.com\r\n")));
    QVERIFY(ics.contains(QStringLiteral("\r\nSEQUENCE:2\r\n")));
    QVERIFY(ics.contains(QStringLiteral("\r\nDTSTAMP:20261023T090000Z\r\n")));
    QVERIFY(ics.contains(QStringLiteral("\r\nATTENDEE;PARTSTAT=ACCEPTED:mailto:me@example.com\r\n")));
}

void InviteControllerTest::anAliasTheInviteListsIsAnsweredAndSentFrom()
{
    // The bootstrap lists the alias second; an invite naming only the alias
    // is answered as, and sent from, the alias.
    Harness h(httpResponse(200, "OK", kSentAck),
              { { "messageId=m3", httpResponse(200, "OK", inviteIcs().replace("ATTENDEE:mailto:me@example.com\r\n", ""),
                                               "text/calendar") } });
    const int token = h.loadCard(QStringLiteral("m3")).value(QStringLiteral("token")).toInt();
    QVERIFY(h.ask(token, QStringLiteral("TENTATIVE")));
    h.controller.confirmRsvp(token);
    QTRY_COMPARE_WITH_TIMEOUT(h.sends(), 1, 10000);
    const QJsonObject body = h.lastSendBody();
    QCOMPARE(body.value(QStringLiteral("from")).toString(), QStringLiteral("alias@example.com"));
    QVERIFY(body.value(QStringLiteral("calendarReply")).toObject().value(QStringLiteral("ics")).toString().contains(
        QStringLiteral("ATTENDEE;PARTSTAT=TENTATIVE:mailto:alias@example.com")));
}

// "Sent only for the account that confirmed it": the pairing is replaced
// while the confirmation is up, and Send then sends nothing at all.
void InviteControllerTest::aReplacedAccountNeverSendsTheConfirmedAnswer()
{
    Harness h;
    const int token = h.loadCard().value(QStringLiteral("token")).toInt();
    QVERIFY(h.ask(token));
    QVERIFY(h.pairingStore.save(pairingFor(h.fake.port(), QStringLiteral("sub-2"), QStringLiteral("dev-2"))));

    h.controller.confirmRsvp(token);
    QTest::qWait(300);
    QCOMPARE(h.sends(), 0);
}

void InviteControllerTest::aCardFromAReplacedAccountCannotBeAnswered()
{
    Harness h;
    const int token = h.loadCard().value(QStringLiteral("token")).toInt();
    QVERIFY(h.pairingStore.save(pairingFor(h.fake.port(), QStringLiteral("sub-2"), QStringLiteral("dev-2"))));
    const qsizetype before = h.fake.receivedRequests().size();
    h.controller.requestRsvp(token, QStringLiteral("ACCEPTED"));
    QTest::qWait(300);
    QVERIFY(h.controller.confirmation().isEmpty());
    // Not even the address lookup goes out for the new account.
    QCOMPARE(h.fake.receivedRequests().size(), before);
}

void InviteControllerTest::lockingWithdrawsTheConfirmationAndSendsNothing()
{
    Harness h;
    const int token = h.loadCard().value(QStringLiteral("token")).toInt();
    QVERIFY(h.ask(token));
    h.controller.setAppLocked(true);
    QVERIFY(h.controller.confirmation().isEmpty());
    // Withdrawn, not hidden: unlocking does not bring the prompt back, and
    // the revision is released so the user may ask again.
    h.controller.setAppLocked(false);
    QVERIFY(h.controller.confirmation().isEmpty());
    QVERIFY(h.stateOf(token).value(QStringLiteral("answerable")).toBool());
    h.controller.confirmRsvp(token);
    QTest::qWait(300);
    QCOMPARE(h.sends(), 0);
}

void InviteControllerTest::aConfirmationAnsweredUnderTheLockSendsNothing()
{
    Harness h;
    const int token = h.loadCard().value(QStringLiteral("token")).toInt();
    // Locked while the address lookup is in flight: when it lands, nothing
    // is raised, and unlocking does not bring a prompt up either.
    h.controller.requestRsvp(token, QStringLiteral("ACCEPTED"));
    h.controller.setAppLocked(true);
    QTRY_VERIFY_WITH_TIMEOUT(h.fake.receivedRequests().size() == 2, 10000);
    QTest::qWait(300);
    h.controller.setAppLocked(false);
    QVERIFY(h.controller.confirmation().isEmpty());
    QVERIFY(h.stateOf(token).value(QStringLiteral("answerable")).toBool());
    h.controller.setAppLocked(true);
    h.controller.requestRsvp(token, QStringLiteral("DECLINED"));
    QTest::qWait(300);
    QCOMPARE(h.sends(), 0);
    QCOMPARE(h.fake.receivedRequests().size(), 2);
}

void InviteControllerTest::cancellingReleasesTheRevision()
{
    Harness h;
    const int token = h.loadCard().value(QStringLiteral("token")).toInt();
    QVERIFY(h.ask(token));
    h.controller.cancelRsvp(token);
    QVERIFY(h.controller.confirmation().isEmpty());
    QVERIFY(h.stateOf(token).value(QStringLiteral("answerable")).toBool());
    QCOMPARE(h.sends(), 0);
}

void InviteControllerTest::anAnsweredRevisionCannotBeAnsweredAgain()
{
    Harness h;
    const int first = h.loadCard().value(QStringLiteral("token")).toInt();
    // A second reader on the same invite.
    const int second = h.loadCard().value(QStringLiteral("token")).toInt();
    QVERIFY(first != second);
    QVERIFY(h.ask(first));
    h.controller.requestRsvp(second, QStringLiteral("DECLINED"));
    QVERIFY(!h.stateOf(second).value(QStringLiteral("answerable")).toBool());
    h.controller.confirmRsvp(first);
    QTRY_COMPARE_WITH_TIMEOUT(h.stateOf(second).value(QStringLiteral("message")).toString(), QStringLiteral("RSVP sent"), 10000);
    // Reopened: a confirmed RSVP stays sent.
    const int third = h.loadCard().value(QStringLiteral("token")).toInt();
    QVERIFY(!h.stateOf(third).value(QStringLiteral("answerable")).toBool());
    h.controller.requestRsvp(third, QStringLiteral("DECLINED"));
    QTest::qWait(300);
    QCOMPARE(h.sends(), 1);
}

// m2 is a copy of m1's invite, same UID and SEQUENCE, naming another
// organizer. Answering m1's card must still go to m1's organizer.
void InviteControllerTest::aSharedUidCannotRedirectAnotherCardsAnswer()
{
    Harness h;
    // The copy first, so the latest-loaded card is not the genuine one.
    const QVariantMap copy = h.loadCard(QStringLiteral("m2"), listing(), QStringLiteral("other@attacker.example"));
    const int genuine = h.loadCard(QStringLiteral("m1")).value(QStringLiteral("token")).toInt();
    QVERIFY(copy.value(QStringLiteral("token")).toInt() != genuine);
    // Different organizers: answering one does not mark the other answered.
    QVERIFY(h.stateOf(copy.value(QStringLiteral("token")).toInt()).value(QStringLiteral("answerable")).toBool());
    QVERIFY(h.ask(genuine));
    h.controller.confirmRsvp(genuine);
    QTRY_COMPARE_WITH_TIMEOUT(h.sends(), 1, 10000);
    QCOMPARE(h.lastSendBody().value(QStringLiteral("to")).toString(), QStringLiteral("boss@example.com"));
}

void InviteControllerTest::aTwoHundredWithoutTheAckIsPlainMailNotAnRsvp()
{
    Harness h(httpResponse(200, "OK", R"({"ok":true,"sentSaved":true,"warning":""})"));
    const int token = h.loadCard().value(QStringLiteral("token")).toInt();
    QVERIFY(h.ask(token));
    h.controller.confirmRsvp(token);
    QTRY_VERIFY_WITH_TIMEOUT(h.stateOf(token).value(QStringLiteral("message")).toString().startsWith(
                                 QStringLiteral("Your server didn't send this as a calendar reply.")),
                             10000);
    QVERIFY(!h.stateOf(token).value(QStringLiteral("answerable")).toBool());
}

void InviteControllerTest::aRefusalReArmsTheButtons()
{
    Harness h(httpResponse(400, "Bad Request", "invalid calendar reply: calendar reply has no VEVENT\n", "text/plain"));
    const int token = h.loadCard().value(QStringLiteral("token")).toInt();
    QVERIFY(h.ask(token));
    h.controller.confirmRsvp(token);
    QTRY_COMPARE_WITH_TIMEOUT(h.stateOf(token).value(QStringLiteral("message")).toString(),
                              QStringLiteral("RSVP not sent: invalid calendar reply: calendar reply has no VEVENT"), 10000);
    QVERIFY(h.stateOf(token).value(QStringLiteral("answerable")).toBool());
}

void InviteControllerTest::aBadGatewayMayHaveBeenSentUntilReopened()
{
    Harness h(httpResponse(502, "Bad Gateway", "smtp: connection reset\n", "text/plain"));
    const int token = h.loadCard().value(QStringLiteral("token")).toInt();
    QVERIFY(h.ask(token));
    h.controller.confirmRsvp(token);
    QTRY_VERIFY_WITH_TIMEOUT(h.stateOf(token).value(QStringLiteral("message")).toString().startsWith(
                                 QStringLiteral("RSVP may not have been sent")),
                             10000);
    QVERIFY(!h.stateOf(token).value(QStringLiteral("answerable")).toBool());
    // Opening the invite again is the user's deliberate retry.
    const int reopened = h.loadCard().value(QStringLiteral("token")).toInt();
    QVERIFY(h.stateOf(reopened).value(QStringLiteral("answerable")).toBool());
    // The outcome sentence stays beside the retry.
    QVERIFY(h.stateOf(reopened).value(QStringLiteral("message")).toString().startsWith(QStringLiteral("RSVP may not")));
}

void InviteControllerTest::aCompletionForAReplacedAccountRecordsNothing()
{
    Harness h;
    const int token = h.loadCard().value(QStringLiteral("token")).toInt();
    QVERIFY(h.ask(token));
    h.controller.confirmRsvp(token);
    // Replaced while the send is on its way; main.cpp then clears.
    QVERIFY(h.pairingStore.save(pairingFor(h.fake.port(), QStringLiteral("sub-2"), QStringLiteral("dev-2"))));
    h.controller.clear();
    QTRY_COMPARE_WITH_TIMEOUT(h.sends(), 1, 10000);
    QTest::qWait(300);
    QVERIFY(h.controller.states().isEmpty());
    // The new account receives the same invite: it is still its to answer,
    // with nothing of the previous account's answer on it.
    const int theirs = h.loadCard().value(QStringLiteral("token")).toInt();
    QVERIFY(h.stateOf(theirs).value(QStringLiteral("answerable")).toBool());
    QCOMPARE(h.stateOf(theirs).value(QStringLiteral("message")).toString(), QString());
}

void InviteControllerTest::noAccountAddressMeansNoConfirmation()
{
    for (const QByteArray& bootstrap :
         { httpResponse(200, "OK", R"({"hasIdentity":false,"protection":"","suggestedUserIDs":["not-an-address"]})"),
           httpResponse(503, "Service Unavailable", "down\n", "text/plain") }) {
        Harness h(httpResponse(200, "OK", kSentAck), { { "/api/pgp/bootstrap", bootstrap } });
        const int token = h.loadCard().value(QStringLiteral("token")).toInt();
        h.controller.requestRsvp(token, QStringLiteral("ACCEPTED"));
        QTRY_VERIFY_WITH_TIMEOUT(h.stateOf(token).value(QStringLiteral("message")).toString().startsWith(
                                     QStringLiteral("Couldn't confirm your account address")),
                                 10000);
        QVERIFY(h.controller.confirmation().isEmpty());
        QVERIFY(h.stateOf(token).value(QStringLiteral("answerable")).toBool());
        QCOMPARE(h.sends(), 0);
    }
}

// The invite is downloaded for one account and lands after the pairing moved:
// the previous account's event never reaches the reader.
void InviteControllerTest::aCardForAReplacedAccountIsNotShown()
{
    Harness h;
    QSignalSpy loaded(&h.controller, &InviteController::inviteLoaded);
    h.controller.load(QStringLiteral("INBOX"), QStringLiteral("m1"), listing(), {}, false);
    QVERIFY(h.pairingStore.save(pairingFor(h.fake.port(), QStringLiteral("sub-2"), QStringLiteral("dev-2"))));
    QTRY_COMPARE_WITH_TIMEOUT(h.fake.receivedRequests().size(), 1, 10000);
    QTest::qWait(300);
    QCOMPARE(loaded.count(), 0);
    QVERIFY(h.controller.states().isEmpty());
}

// The reader moved on while the address lookup was out. Without forget(), the
// lookup would raise a confirmation no reader shows, and every RSVP in the
// process would wait on it.
void InviteControllerTest::forgettingACardDuringTheLookupLeavesNothingAsked()
{
    Harness h;
    const int token = h.loadCard().value(QStringLiteral("token")).toInt();
    h.controller.requestRsvp(token, QStringLiteral("ACCEPTED"));
    h.controller.forget(token);
    QTRY_VERIFY_WITH_TIMEOUT(h.fake.receivedRequests().size() == 2, 10000);
    QTest::qWait(300);
    QVERIFY(h.controller.confirmation().isEmpty());
    QVERIFY(!h.controller.states().contains(QString::number(token)));
    // The same invite, reopened, is answerable and asks as normal.
    const int reopened = h.loadCard().value(QStringLiteral("token")).toInt();
    QVERIFY(h.ask(reopened));
    // Leaving with the confirmation up withdraws it, and sends nothing.
    h.controller.forget(reopened);
    QVERIFY(h.controller.confirmation().isEmpty());
    const int again = h.loadCard().value(QStringLiteral("token")).toInt();
    QVERIFY(h.stateOf(again).value(QStringLiteral("answerable")).toBool());
    QCOMPARE(h.sends(), 0);
}

void InviteControllerTest::aSecondAnswerWaitsForTheFirstConfirmation()
{
    Harness h;
    const int first = h.loadCard(QStringLiteral("m1")).value(QStringLiteral("token")).toInt();
    const int other = h.loadCard(QStringLiteral("m2"), listing(), QStringLiteral("other@attacker.example"))
                          .value(QStringLiteral("token")).toInt();
    QVERIFY(h.ask(first));
    h.controller.requestRsvp(other, QStringLiteral("DECLINED"));
    QCOMPARE(h.stateOf(other).value(QStringLiteral("message")).toString(), QStringLiteral("Answer the other RSVP first."));
    QVERIFY(h.stateOf(other).value(QStringLiteral("answerable")).toBool());
    QCOMPARE(h.controller.confirmation().value(QStringLiteral("token")).toInt(), first);
}

// suggestedUserIDs[0] is the IMAP login, here not an address: the attendee is
// the listed alias, and since the relay would send from the login unasked,
// the alias goes as `from` too.
void InviteControllerTest::aLoginThatIsNotAnAddressAnswersFromTheAlias()
{
    Harness h(httpResponse(200, "OK", kSentAck),
              { { "/api/pgp/bootstrap",
                  httpResponse(200, "OK",
                               R"({"hasIdentity":false,"protection":"","suggestedUserIDs":["yoshi","alias@example.com"],)"
                               R"("accountAddresses":["alias@example.com"]})") } });
    const int token = h.loadCard().value(QStringLiteral("token")).toInt();
    QVERIFY(h.ask(token));
    h.controller.confirmRsvp(token);
    QTRY_COMPARE_WITH_TIMEOUT(h.sends(), 1, 10000);
    QCOMPARE(h.lastSendBody().value(QStringLiteral("from")).toString(), QStringLiteral("alias@example.com"));
}

QTEST_MAIN(InviteControllerTest)
#include "InviteControllerTest.moc"
