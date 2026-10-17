#include "domain/MailRepository.h"

#include "db/Database.h"
#include "db/EmailDao.h"
#include "domain/DevicePairing.h"
#include "domain/PairingStore.h"
#include "net/HttpClient.h"
#include "net/RelayMailSource.h"
#include "stores/CursorStore.h"
#include "stores/SecureStoreFile.h"

#include "../net/FakeRelayServer.h"

#include <QNetworkAccessManager>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>

// Inbox paging (plan item L12): the window is one page, older pages load
// below the oldest held row, and a resync replaces only the window's range.
// Message ids are IMAP UIDs, the numbers the relay pages by.
class MailPagingTest : public QObject
{
    Q_OBJECT

private slots:
    void theWindowAsksForOnePage();
    void anOlderPageAsksBelowTheOldestHeldRowWithoutBodies();
    void aResyncKeepsOlderRowsAndPrunesOnlyItsOwnRange();
    void aResyncThatNoLongerReachesTheHeldRowsDropsThem();
    void aWindowShorterThanAPageIsTheWholeFolder();
    void aDeltaWithMoreThanAWindowDropsRowsItCannotReach();
    void aDeltaWithMoreThanAWindowKeepsRowsThatReachIt();
    void aRelayThatCannotPageStoresNothing();
    void anOlderPageThatNoLongerJoinsOnIsDropped();
    void anOlderPageForAReplacedAccountIsDropped();
    void anOlderPageStoresOnlyRowsBelowItsAnchor();
};

namespace {

const QString kInbox = QStringLiteral("INBOX");

QList<int> range(int from, int to)
{
    QList<int> ids;
    for (int id = from; id <= to; ++id)
        ids.append(id);
    return ids;
}

Email row(int id, bool metadataOnly = false)
{
    Email email;
    email.messageId = QString::number(id);
    email.folder = kInbox;
    email.subject = QStringLiteral("Message %1").arg(id);
    email.atUtc = QStringLiteral("2026-07-01T00:00:00Z");
    email.status = QStringLiteral("read");
    if (!metadataOnly)
        email.body = QStringLiteral("body %1").arg(id);
    email.metadataOnly = metadataOnly;
    return email;
}

InboxFetchResult reply(const QList<int>& ids)
{
    InboxFetchResult result;
    result.tabs = QStringList{ QStringLiteral("Uncategorized") };
    QVector<InboxEmailItem> items;
    for (int id : ids)
        items.append(InboxEmailItem{ row(id), QString(), std::nullopt });
    result.byTab[QStringLiteral("Uncategorized")] = items;
    return result;
}

QByteArray pageJson(const QList<int>& ids, const QByteArray& paging)
{
    QByteArray rows;
    for (int id : ids) {
        if (!rows.isEmpty())
            rows += ',';
        rows += R"({"messageId":")" + QByteArray::number(id)
            + R"(","subject":"Old","status":"read","atUtc":"2026-01-01T00:00:00Z"})";
    }
    return R"({"tabs":["Uncategorized"],"byTab":{"Uncategorized":[)" + rows + "]}" + paging + "}";
}

struct Fixture
{
    explicit Fixture(quint16 port = 1)
    {
        [&] {
            QVERIFY(db.open(QStringLiteral(":memory:")));
            QVERIFY(secureDir.isValid());
            QVERIFY(cursorDir.isValid());
        }();
        DevicePairing pairing;
        pairing.subscriberId = QStringLiteral("sub-1");
        pairing.deviceId = QStringLiteral("dev-1");
        pairing.deviceSecret = QStringLiteral("secret-1");
        pairing.serverBaseUrl = QStringLiteral("http://127.0.0.1:%1").arg(port);
        [&] { QVERIFY(pairingStore.save(pairing)); }();
    }

    void hold(const QList<int>& ids, bool metadataOnly = false)
    {
        for (int id : ids)
            [&] { QVERIFY(emailDao.insertOrReplace(row(id, metadataOnly))); }();
    }

    QList<int> held()
    {
        QList<int> ids;
        for (const QString& id : emailDao.messageIdsInFolder(kInbox))
            ids.append(id.toInt());
        std::sort(ids.begin(), ids.end());
        return ids;
    }

    MailRefreshPlan refreshPlan() const
    {
        MailRefreshPlan plan;
        plan.folder = kInbox;
        plan.subscriberId = QStringLiteral("sub-1");
        plan.endpoint.auth.deviceId = QStringLiteral("dev-1");
        return plan;
    }

    Database db;
    QTemporaryDir secureDir;
    QTemporaryDir cursorDir;
    EmailDao emailDao{ db.handle() };
    SecureStoreFile secureStore{ secureDir.path() };
    PairingStore pairingStore{ secureStore };
    CursorStore cursorStore{ cursorDir.filePath(QStringLiteral("cursor.ini")) };
    QNetworkAccessManager manager;
    HttpClient http{ manager };
    RelayMailSource source{ http };
    MailRepository repository{ source, emailDao, pairingStore, cursorStore };
};

QList<int> without(QList<int> ids, int id)
{
    ids.removeAll(id);
    return ids;
}

} // namespace

void MailPagingTest::theWindowAsksForOnePage()
{
    FakeRelayServer fake(httpResponse(200, "OK", pageJson({ 5 }, R"(,"delta":false,"cursor":9,"hasMore":false)")));
    Fixture f(fake.port());

    QCOMPARE(f.repository.refreshFolder(kInbox).outcome, MailRepositoryOutcome::Success);
    const QByteArray request = fake.receivedRequest();
    QVERIFY(request.contains("limit=50"));
    QVERIFY(request.contains("since=0"));
    QVERIFY(!request.contains("before="));
    QVERIFY(!request.contains("bodies=0"));
}

void MailPagingTest::anOlderPageAsksBelowTheOldestHeldRowWithoutBodies()
{
    FakeRelayServer fake(httpResponse(200, "OK", pageJson(range(10, 59), R"(,"hasMore":true,"nextBefore":"10")")));
    Fixture f(fake.port());
    f.hold(range(60, 100));

    const std::optional<MailOlderPlan> plan = f.repository.planOlder(kInbox);
    QVERIFY(plan.has_value());
    QCOMPARE(plan->before, QStringLiteral("60"));

    const InboxFetchResult result = MailRepository::fetchOlderWith(f.http, *plan);
    const QByteArray requestLine = fake.receivedRequest().left(fake.receivedRequest().indexOf("\r\n"));
    QVERIFY(requestLine.startsWith("GET /api/inbox?"));
    QVERIFY(requestLine.contains("limit=50"));
    QVERIFY(requestLine.contains("mailbox=INBOX"));
    QVERIFY(requestLine.contains("before=60"));
    QVERIFY(requestLine.contains("bodies=0"));
    QVERIFY(!requestLine.contains("since="));
    QCOMPARE(result.hasMore, std::optional<bool>(true));
    QCOMPARE(result.nextBefore, QStringLiteral("10"));

    const MailOlderOutcome outcome = f.repository.applyOlder(*plan, result);
    QCOMPARE(outcome.outcome, MailRepositoryOutcome::Success);
    QVERIFY(outcome.more);
    QCOMPARE(f.held(), range(10, 100));
    const Email older = *f.emailDao.findById(kInbox, QStringLiteral("10"));
    QVERIFY(older.metadataOnly);
    QVERIFY(!older.body.has_value());
    QVERIFY(!f.emailDao.findById(kInbox, QStringLiteral("60"))->metadataOnly);

    // The next page starts where this one ended: its nextBefore.
    QCOMPARE(f.repository.planOlder(kInbox)->before, result.nextBefore);
}

void MailPagingTest::aResyncKeepsOlderRowsAndPrunesOnlyItsOwnRange()
{
    Fixture f;
    f.hold(range(1, 50), true);
    f.hold(range(51, 100));

    // 60 was deleted on the server; 51 fell below the window and still exists.
    const QList<int> window = without(range(52, 102), 60);
    QCOMPARE(window.size(), MailRepository::kInboxPageSize);
    InboxFetchResult snapshot = reply(window);
    snapshot.cursor = 5;

    QCOMPARE(f.repository.applyRefresh(f.refreshPlan(), snapshot).outcome, MailRepositoryOutcome::Success);
    QCOMPARE(f.held(), without(range(1, 102), 60));
    QVERIFY(f.emailDao.findById(kInbox, QStringLiteral("1"))->metadataOnly);
    QVERIFY(!f.emailDao.findById(kInbox, QStringLiteral("102"))->metadataOnly);
}

void MailPagingTest::aResyncThatNoLongerReachesTheHeldRowsDropsThem()
{
    Fixture f;
    f.hold(range(1, 20));

    // Everything held is older than the window; whatever lies between 20 and
    // 200 is unknown, and paging starts from the oldest held row.
    QCOMPARE(f.repository.applyRefresh(f.refreshPlan(), reply(range(200, 249))).outcome,
             MailRepositoryOutcome::Success);
    QCOMPARE(f.held(), range(200, 249));
}

void MailPagingTest::aWindowShorterThanAPageIsTheWholeFolder()
{
    Fixture f;
    f.hold(range(1, 25));

    QCOMPARE(f.repository.applyRefresh(f.refreshPlan(), reply(range(21, 30))).outcome,
             MailRepositoryOutcome::Success);
    QCOMPARE(f.held(), range(21, 30));
}

void MailPagingTest::aDeltaWithMoreThanAWindowDropsRowsItCannotReach()
{
    Fixture f;
    f.hold(range(1, 50));

    InboxFetchResult delta = reply(range(200, 249));
    delta.isDelta = true;
    delta.cursor = 8;
    delta.hasMore = true;
    delta.nextBefore = QStringLiteral("200");

    QCOMPARE(f.repository.applyRefresh(f.refreshPlan(), delta).outcome, MailRepositoryOutcome::Success);
    QCOMPARE(f.held(), range(200, 249));
}

void MailPagingTest::aDeltaWithMoreThanAWindowKeepsRowsThatReachIt()
{
    Fixture f;
    f.hold(range(1, 50));

    InboxFetchResult delta = reply(range(51, 60));
    delta.isDelta = true;
    delta.cursor = 8;
    delta.hasMore = true;
    delta.nextBefore = QStringLiteral("40");

    QCOMPARE(f.repository.applyRefresh(f.refreshPlan(), delta).outcome, MailRepositoryOutcome::Success);
    QCOMPARE(f.held(), range(1, 60));
}

void MailPagingTest::aRelayThatCannotPageStoresNothing()
{
    Fixture f;
    f.hold(range(60, 100));
    const MailOlderPlan plan = *f.repository.planOlder(kInbox);

    // An old relay ignores `before=` and answers with its window, no hasMore.
    const MailOlderOutcome outcome = f.repository.applyOlder(plan, reply(range(101, 150)));
    QCOMPARE(outcome.outcome, MailRepositoryOutcome::Success);
    QVERIFY(!outcome.more);
    QCOMPARE(outcome.endsAt, QStringLiteral("60"));
    QCOMPARE(f.held(), range(60, 100));
}

void MailPagingTest::anOlderPageThatNoLongerJoinsOnIsDropped()
{
    Fixture f;
    f.hold(range(60, 100));
    const MailOlderPlan plan = *f.repository.planOlder(kInbox);

    // A removal moved the oldest row while the page was out.
    QVERIFY(f.emailDao.deleteById(kInbox, QStringLiteral("60")));
    InboxFetchResult page = reply(range(10, 59));
    page.hasMore = true;

    const MailOlderOutcome outcome = f.repository.applyOlder(plan, page);
    QCOMPARE(outcome.outcome, MailRepositoryOutcome::Success);
    QCOMPARE(f.held(), range(61, 100));
}

void MailPagingTest::anOlderPageForAReplacedAccountIsDropped()
{
    Fixture f;
    f.hold(range(60, 100));
    const MailOlderPlan plan = *f.repository.planOlder(kInbox);

    DevicePairing replacement;
    replacement.subscriberId = QStringLiteral("sub-2");
    replacement.deviceId = QStringLiteral("dev-2");
    replacement.deviceSecret = QStringLiteral("secret-2");
    replacement.serverBaseUrl = QStringLiteral("http://127.0.0.1:1");
    QVERIFY(f.pairingStore.save(replacement));

    InboxFetchResult page = reply(range(10, 59));
    page.hasMore = true;
    QCOMPARE(f.repository.applyOlder(plan, page).outcome, MailRepositoryOutcome::PairingChanged);
    QCOMPARE(f.held(), range(60, 100));
}

void MailPagingTest::anOlderPageStoresOnlyRowsBelowItsAnchor()
{
    Fixture f;
    f.hold(range(60, 100));
    const MailOlderPlan plan = *f.repository.planOlder(kInbox);

    InboxFetchResult page = reply({ 70, 55 });
    page.byTab[QStringLiteral("Uncategorized")][0].email.subject = QStringLiteral("Replaced");
    page.hasMore = true;
    MailOlderOutcome outcome = f.repository.applyOlder(plan, page);
    QVERIFY(outcome.more);
    QCOMPARE(f.emailDao.findById(kInbox, QStringLiteral("70"))->subject, QStringLiteral("Message 70"));
    QVERIFY(f.emailDao.findById(kInbox, QStringLiteral("70"))->body.has_value());
    QVERIFY(f.emailDao.findById(kInbox, QStringLiteral("55"))->metadataOnly);

    // A page that adds nothing ends paging rather than asking again.
    page = reply({ 80 });
    page.hasMore = true;
    outcome = f.repository.applyOlder(*f.repository.planOlder(kInbox), page);
    QCOMPARE(outcome.outcome, MailRepositoryOutcome::Success);
    QVERIFY(!outcome.more);
    QCOMPARE(outcome.endsAt, QStringLiteral("55"));
}

QTEST_GUILESS_MAIN(MailPagingTest)
#include "MailPagingTest.moc"
