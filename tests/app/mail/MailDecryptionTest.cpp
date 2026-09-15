#include "mail/MailController.h"

#include "db/Database.h"
#include "db/EmailDao.h"
#include "db/FolderDao.h"
#include "domain/DevicePairing.h"
#include "domain/FolderRepository.h"
#include "domain/KeywordRepository.h"
#include "domain/MailRepository.h"
#include "domain/PairingStore.h"
#include "net/FolderClient.h"
#include "net/HttpClient.h"
#include "net/NetworkExecutor.h"
#include "net/PgpBootstrapClient.h"
#include "net/PgpRecipientChecker.h"
#include "net/RelayMailSource.h"
#include "pgp/OpenPgpDecryptor.h"
#include "stores/CursorStore.h"
#include "stores/SecureStoreFile.h"
#include "stores/SettingsStore.h"

#include "../../core/net/FakeRelayServer.h"
#include "../../core/pgp/GnupgFixture.h"
#include "MailPgpHarness.h"
#include "../ExecutorShutdownGuard.h"

#include <QJsonDocument>
#include <QImage>
#include <QBuffer>
#include "pgp/PgpMimeWriter.h"
#include "pgp/MimeBodyReader.h"
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QTemporaryDir>
#include <QTest>
#include <QSignalSpy>

namespace {

// The string the whole file is about. If this ever appears in the database,
// the app has demoted an end-to-end encrypted message to the protection level
// of every other mail in the cache.
const char* const kCanary = "canary-plaintext-must-never-be-written";

// One client-protected row: pgpEncrypted, no body, no decrypt error. That
// exact shape is what PgpMessageState calls ClientProtected.
QByteArray inboxWithOneEncryptedMessage()
{
    return R"({
      "tabs": ["Uncategorized"],
      "byTab": {
        "Uncategorized": [
          {
            "messageId": "5",
            "sender": "sender@example.com",
            "sentTo": "me@example.com",
            "cc": "", "bcc": "",
            "subject": "Encrypted",
            "status": "unread",
            "atUtc": "2026-08-23T00:00:00Z",
            "hasAttachments": false,
            "label": "",
            "pgpEncrypted": true
          }
        ]
      }
    })";
}

QByteArray payloadResponse(const QByteArray& armored)
{
    const QJsonObject body{
        { QStringLiteral("messageId"), 5 },
        { QStringLiteral("mailbox"), QStringLiteral("INBOX") },
        { QStringLiteral("encryptedPayload"), QString::fromUtf8(armored) },
    };
    return httpResponse(200, "OK", QJsonDocument(body).toJson(QJsonDocument::Compact));
}

// Every value in every column of every table, as one blob to search.
//
// Deliberately not "check the emails table": the point is that the plaintext
// is nowhere, and a targeted check would keep passing if some later change
// started writing it somewhere else.
QString everythingInTheDatabase(QSqlDatabase database)
{
    QString dump;
    QSqlQuery tables(QStringLiteral("SELECT name FROM sqlite_master WHERE type='table'"), database);
    while (tables.next()) {
        const QString table = tables.value(0).toString();
        QSqlQuery rows(QStringLiteral("SELECT * FROM \"%1\"").arg(table), database);
        while (rows.next()) {
            const QSqlRecord record = rows.record();
            for (int i = 0; i < record.count(); ++i)
                dump += record.value(i).toString() + QLatin1Char('\n');
        }
    }
    return dump;
}

} // namespace

class MailDecryptionTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void signedInboxRowCanBeReadAndVerified_data();
    void signedInboxRowCanBeReadAndVerified();
    void largeEncryptedWindowKeepsDeltaAndFolderCursors();
    void encryptedDraftNeverUploadsPlaintext();
    void reopenedDraftPreservesRecipientsAndMemoryAttachments();
    void pairingChangeReleasesRestoredDraft_data();
    void pairingChangeReleasesRestoredDraft();
    void draftCustodyFailuresNeverPost_data();
    void draftCustodyFailuresNeverPost();
    void serverCustodyDraftStillSaves();
    void decryptingAClientProtectedMessageShowsItsText();
    void aDecryptedMessageNeverReachesTheDatabase();
    void forgettingDropsTheHeldPlaintext();
    void forgettingInvalidatesAnInFlightRead();
    void lockingInvalidatesAnInFlightRead();
    void protectedSubjectIsTransient();
    void decryptedAttachmentsStayLocalAndRequireTheCurrentToken();
    void cidImagesAreBoundToTheCurrentUnlockedMessage();
    void duplicateUidsUseTheSelectedFolder();
    void serverCustodyIsExplainedRatherThanRetried();
    void anOutageIsTheOneRetryableFailure();
    void aReplyForAReplacedAccountIsNeverShown();
    void aDecryptedMessageIsNotVisibleAfterTheAccountIsReplaced();

    void addressBootstrapWithRetiredKeyNeverUploads_data();
    void addressBootstrapWithRetiredKeyNeverUploads();

private:
    GnupgFixture m_fixture;
};

void MailDecryptionTest::initTestCase()
{
    if (!OpenPgpDecryptor::engineAvailable())
        QSKIP("no usable gpg on this system -- client-side decryption is NOT covered");
    if (!m_fixture.build())
        QSKIP("could not build a throwaway GnuPG keyring -- client-side decryption is NOT covered");

    // The controller decrypts against the USER's keyring by design -- there
    // is no home-directory argument on that path and there should not be, so
    // production cannot be pointed somewhere else. GNUPGHOME is how the test
    // redirects it, and gpg reads it when it is spawned, so the ordering
    // relative to gpgme's own initialisation does not matter.
    qputenv("GNUPGHOME", m_fixture.path().toUtf8());
}

void MailDecryptionTest::cleanupTestCase()
{
    GnupgFixture::killAgent(m_fixture.path());
}

void MailDecryptionTest::signedInboxRowCanBeReadAndVerified_data()
{
    QTest::addColumn<bool>("attachmentOnly");
    QTest::newRow("body") << false;
    QTest::newRow("attachment-only") << true;
}

void MailDecryptionTest::signedInboxRowCanBeReadAndVerified()
{
    QFETCH(bool, attachmentOnly);
    QByteArray inbox = inboxWithOneEncryptedMessage();
    inbox.replace("\"pgpEncrypted\": true", "\"pgpSigned\": true");
    FakeRelayServer fake(httpResponse(200, "OK", inbox));
    DecryptHarness h;
    QVERIFY(h.build(fake));
    h.controller->refresh();
    QTRY_VERIFY(!h.controller->isBusy());
    const auto row = h.controller->findByMessageId(QStringLiteral("5"));
    QCOMPARE(row.value(QStringLiteral("pgpState")).toInt(), 4);
    QVERIFY(row.value(QStringLiteral("canDecryptHere")).toBool());
    QCOMPARE(row.value(QStringLiteral("pgpReadAction")).toString(), QStringLiteral("Verify signature"));
    OutgoingMessage message;
    message.mode = QStringLiteral("plain");
    message.subject = QStringLiteral("signed subject");
    message.body = attachmentOnly ? QString() : QStringLiteral("signed body");
    if (attachmentOnly)
        message.attachments = {{QStringLiteral("signed.bin"), QStringLiteral("application/octet-stream"), QByteArray("signed bytes")}};
    const QByteArray content = protectedContent(message, randomMimeBoundary());
    const auto signature = m_fixture.detachedSignature(content, QStringLiteral("test@example.com"));
    QVERIFY(!signature.isEmpty());
    const QJsonArray keys{QJsonObject{{"publicKey", QString::fromUtf8(m_fixture.exportPublicKey(QStringLiteral("test@example.com")))},
        {"fingerprint", m_fixture.fingerprintOf(QStringLiteral("test@example.com"))}}};
    const QJsonObject payload{{"signedPartBase64", QString::fromLatin1(content.toBase64())},
        {"signaturePayload", QString::fromUtf8(signature)}, {"resolvedSender", "test@example.com"}, {"signerKeys", keys}};
    fake.setResponse(httpResponse(200, "OK", QJsonDocument(payload).toJson()));
    h.controller->decryptMessage(QStringLiteral("5"));
    QTRY_VERIFY_WITH_TIMEOUT(!h.controller->decryptBusy(), 10000);
    QVERIFY2(h.controller->decryptFailure().isEmpty(), qPrintable(h.controller->decryptFailure()));
    QCOMPARE(h.controller->decryptedPlain().trimmed(), message.body);
    QCOMPARE(h.controller->decryptedAttachments().size(), attachmentOnly ? 1 : 0);
    QCOMPARE(h.controller->decryptedSubject(), QStringLiteral("signed subject"));
    QCOMPARE(h.controller->decryptedSignature(), QStringLiteral("Signed by test@example.com."));
    QVERIFY(!h.controller->decryptedSignatureIsWarning());
    // A signed-only response cannot impersonate successful decryption of a
    // row classified as encrypted. The page must not imply confidentiality.
    auto encryptedRow = h.mailRepository->cachedEmail(QStringLiteral("INBOX"), QStringLiteral("5"));
    QVERIFY(encryptedRow.has_value());
    encryptedRow->pgpEncrypted = true;
    QVERIFY(h.emailDao->insertOrReplace(*encryptedRow));
    h.controller->decryptMessage(QStringLiteral("5"));
    QTRY_VERIFY_WITH_TIMEOUT(!h.controller->decryptBusy(), 10000);
    QVERIFY(!h.controller->decryptFailure().isEmpty());
    QVERIFY(h.controller->decryptedMessageId().isEmpty());
}

void MailDecryptionTest::largeEncryptedWindowKeepsDeltaAndFolderCursors()
{
    QJsonArray rows;
    for (int i = 1; i <= 500; ++i)
        rows.append(QJsonObject{{"messageId", QString::number(i)}, {"pgpEncrypted", true},
            {"subject", "[Encrypted]"}, {"sender", "sender@example.com"}, {"status", "unread"}});
    QJsonObject window{{"byTab", QJsonObject{{"Uncategorized", rows}}}, {"delta", false}, {"cursor", 100}};
    FakeRelayServer fake(httpResponse(200, "OK", QJsonDocument(window).toJson()));
    DecryptHarness h;
    QVERIFY(h.build(fake));
    h.controller->refresh();
    QTRY_VERIFY(!h.controller->isBusy());
    QVERIFY(fake.receivedRequest().contains("since=0"));
    QCOMPARE(h.mailRepository->cachedEmails(QStringLiteral("INBOX")).size(), 500);
    QCOMPARE(h.cursorStore->mailCursor(QStringLiteral("sub-1"), QStringLiteral("INBOX")), QStringLiteral("100"));
    window = QJsonObject{{"byTab", QJsonObject{{"Uncategorized", QJsonArray{QJsonObject{
        {"messageId", "5"}, {"pgpEncrypted", true}, {"pgpSigned", true}, {"status", "read"}, {"changeType", "updated"}}}}}},
        {"delta", true}, {"cursor", 101}, {"removed", QJsonArray{"7"}}};
    fake.setResponse(httpResponse(200, "OK", QJsonDocument(window).toJson()));
    h.controller->refresh();
    QTRY_VERIFY(!h.controller->isBusy());
    QVERIFY(fake.receivedRequest().contains("since=100"));
    QCOMPARE(h.mailRepository->cachedEmails(QStringLiteral("INBOX")).size(), 499);
    const auto row = h.mailRepository->cachedEmail(QStringLiteral("INBOX"), QStringLiteral("5"));
    QVERIFY(row.has_value() && row->pgpEncrypted && row->pgpSigned);
    QVERIFY(!row->body.has_value() || row->body->isEmpty());
    QCOMPARE(row->status, QStringLiteral("read"));
    QCOMPARE(h.cursorStore->mailCursor(QStringLiteral("sub-1"), QStringLiteral("INBOX")), QStringLiteral("101"));
    const auto archive = h.mailRepository->planRefresh(QStringLiteral("Archive"), false);
    QVERIFY(archive.has_value());
    QCOMPARE(archive->since, qint64(0));
    window = QJsonObject{{"byTab", QJsonObject{{"Uncategorized", rows}}}, {"delta", false}, {"cursor", 102}};
    fake.setResponse(httpResponse(200, "OK", QJsonDocument(window).toJson()));
    h.controller->refresh(true);
    QTRY_VERIFY(!h.controller->isBusy());
    QVERIFY(fake.receivedRequest().contains("since=0"));
    QCOMPARE(h.cursorStore->mailCursor(QStringLiteral("sub-1"), QStringLiteral("INBOX")), QStringLiteral("102"));
}

void MailDecryptionTest::encryptedDraftNeverUploadsPlaintext()
{
    const QString fingerprint = GnupgFixture::firstFingerprint(m_fixture.path(), QStringLiteral("test@example.com"));
    QVERIFY(!fingerprint.isEmpty());
    const QJsonObject bootstrap{{"hasIdentity", true}, {"protection", "client"},
                                {"fingerprint", fingerprint},
                                {"suggestedUserIDs", QJsonArray{"test@example.com"}}};
    FakeRelayServer fake(httpResponse(200, "OK", "{}"));
    fake.setResponseForPath("/api/pgp/bootstrap", httpResponse(200, "OK", QJsonDocument(bootstrap).toJson()));
    fake.setResponseForPath("/api/mail/draft", httpResponse(200, "OK", R"({"ok":true})"));
    DecryptHarness h;
    QVERIFY(h.build(fake));
    QTemporaryDir files;
    QFile file(files.filePath(QStringLiteral("private-file.txt")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("private-file-bytes"), 18);
    file.close();
    QSignalSpy saved(h.controller.get(), &MailController::draftSaveCompleted);
    const auto token = h.controller->saveDraft(QStringLiteral("to@example.com"), QStringLiteral("cc@example.com"),
        QStringLiteral("blind@example.com"), QStringLiteral("Private draft subject"), QStringLiteral("<p>Private draft body</p>"), {file.fileName()});
    QVERIFY(token != 0);
    QVERIFY(h.controller->isBusy());
    QCOMPARE(h.controller->saveDraft({}, {}, {}, {}, {}, {}), 0);
    QTRY_COMPARE_WITH_TIMEOUT(saved.size(), 1, 10000);
    QCOMPARE(saved.first().at(0).toULongLong(), token);
    QVERIFY2(saved.first().at(1).toBool(), qPrintable(h.controller->lastError()));
    QCOMPARE(fake.receivedRequests().size(), 2);
    const QByteArray request = fake.receivedRequests().last();
    for (const QByteArray& secret : {QByteArray("Private draft"), QByteArray("private-file"), QByteArray("blind@example.com"), QByteArray("cc@example.com")})
        QVERIFY(!request.contains(secret));
    const auto json = QJsonDocument::fromJson(request.mid(request.indexOf("\r\n\r\n") + 4)).object();
    QCOMPARE(json.size(), 2);
    QCOMPARE(json.value("to").toString(), QStringLiteral("to@example.com"));
    const QByteArray mime = json.value("pgpDraft").toString().toUtf8();
    QVERIFY(mime.contains("multipart/encrypted"));
    const int start = mime.indexOf("-----BEGIN PGP MESSAGE-----");
    const int end = mime.indexOf("-----END PGP MESSAGE-----", start);
    QVERIFY(start >= 0 && end > start);
    const auto decrypted = OpenPgpDecryptor().decrypt(mime.mid(start, end + 25 - start), m_fixture.path());
    QCOMPARE(decrypted.status, PgpDecryptStatus::Decrypted);
    QVERIFY(decrypted.plaintext.contains("To: to@example.com\r\n"));
    QVERIFY(decrypted.plaintext.contains("Cc: cc@example.com\r\n"));
    QVERIFY(decrypted.plaintext.contains("Bcc: blind@example.com\r\n"));
    const auto parsed = readMimeBody(decrypted.plaintext);
    QCOMPARE(parsed.subject, QStringLiteral("Private draft subject"));
    QCOMPARE(parsed.html, QStringLiteral("<p>Private draft body</p>"));
    QCOMPARE(parsed.attachments.size(), 1);
    QCOMPARE(parsed.attachments.first().data, QByteArray("private-file-bytes"));
}

void MailDecryptionTest::reopenedDraftPreservesRecipientsAndMemoryAttachments()
{
    OutgoingMessage message;
    message.to = {QStringLiteral("\"Doe, Jane\" <jane@example.com>")};
    message.cc = {QStringLiteral("copy@example.com")};
    message.subject = QStringLiteral("restored-private-subject");
    message.body = QStringLiteral("<p>restored-private-body</p>");
    message.mode = QStringLiteral("html");
    message.attachments = {{QStringLiteral("private.bin"), QStringLiteral("application/octet-stream"), QByteArray("secret\0bytes", 12)},
                           {QStringLiteral("empty.txt"), QStringLiteral("text/plain"), {}}};
    const auto entity = protectedDraftContent(message, {QStringLiteral("blind@example.com")}, QStringLiteral("restore"));
    const auto armored = m_fixture.encryptToTestKey(entity);
    QVERIFY(!armored.isEmpty());
    FakeRelayServer fake(payloadResponse(armored));
    fake.setResponseForPath("/pgp-payload", payloadResponse(armored));
    DecryptHarness h;
    QVERIFY(h.build(fake));
    Email row;
    row.messageId = QStringLiteral("5");
    row.folder = QStringLiteral("Drafts");
    row.subject = QStringLiteral("Encrypted");
    row.sentTo = QStringLiteral("outer@example.com");
    row.pgpEncrypted = true;
    QVERIFY(h.emailDao->insertOrReplace(row));
    h.controller->decryptMessage(row.messageId, row.folder);
    QTRY_VERIFY_WITH_TIMEOUT(!h.controller->decryptBusy(), 15000);
    QCOMPARE(h.controller->decryptFailure(), QString());
    QVERIFY(h.controller->reopenDecryptedDraft(QStringLiteral("stale")).isEmpty());
    const auto seed = h.controller->reopenDecryptedDraft(h.controller->decryptedToken());
    const QString session = seed.value("token").toString();
    QVERIFY(!session.isEmpty());
    QCOMPARE(seed.value("to").toString(), message.to.first());
    QCOMPARE(seed.value("cc").toString(), message.cc.first());
    QCOMPARE(seed.value("bcc").toString(), QStringLiteral("blind@example.com"));
    QCOMPARE(seed.value("subject").toString(), message.subject);
    QCOMPARE(seed.value("body").toString(), message.body);
    const QStringList paths = seed.value("paths").toStringList();
    QCOMPARE(paths.size(), 2);
    QVERIFY(!QFile::exists(paths.first()));
    h.controller->forgetDecrypted(); // navigating to Compose must not drop its files
    const QString fingerprint = GnupgFixture::firstFingerprint(m_fixture.path(), QStringLiteral("test@example.com"));
    const QJsonObject bootstrap{{"hasIdentity", true}, {"protection", "client"}, {"fingerprint", fingerprint},
                               {"suggestedUserIDs", QJsonArray{"test@example.com"}}};
    fake.setResponse(httpResponse(200, "OK", QJsonDocument(bootstrap).toJson()));
    fake.setResponseForPath("/api/mail/draft", httpResponse(200, "OK", R"({"ok":true})"));
    QSignalSpy saved(h.controller.get(), &MailController::draftSaveCompleted);
    QVERIFY(h.controller->saveDraft(seed.value("to").toString(), seed.value("cc").toString(), seed.value("bcc").toString(),
        message.subject, message.body, paths, session) != 0);
    QTRY_COMPARE_WITH_TIMEOUT(saved.size(), 1, 15000);
    QVERIFY2(saved.last()[1].toBool(), qPrintable(h.controller->lastError()));
    const auto request = fake.receivedRequests().last();
    QVERIFY(!request.contains("restored-private"));
    QVERIFY(!request.contains("private.bin"));
    const auto json = QJsonDocument::fromJson(request.mid(request.indexOf("\r\n\r\n") + 4)).object();
    const auto mime = json.value("pgpDraft").toString().toUtf8();
    const int start = mime.indexOf("-----BEGIN PGP MESSAGE-----");
    const int end = mime.indexOf("-----END PGP MESSAGE-----", start);
    QVERIFY(start >= 0 && end > start);
    const auto decrypted = OpenPgpDecryptor().decrypt(mime.mid(start, end + 25 - start), m_fixture.path());
    QCOMPARE(decrypted.status, PgpDecryptStatus::Decrypted);
    const auto restored = readMimeBody(decrypted.plaintext);
    QCOMPARE(restored.to, message.to.first());
    QCOMPARE(restored.bcc, QStringLiteral("blind@example.com"));
    QCOMPARE(restored.attachments.size(), 2);
    QCOMPARE(restored.attachments[0].data, message.attachments[0].data);
    QCOMPARE(restored.attachments[1].data, QByteArray());
    QVERIFY(!everythingInTheDatabase(h.db.handle()).contains(message.subject));

    const QJsonObject key{{"address", "test@example.com"}, {"fingerprint", fingerprint},
                         {"publicKey", QString::fromUtf8(m_fixture.exportPublicKey(QStringLiteral("test@example.com")))},
                         {"tier", "verified"}, {"usable", true}};
    fake.setResponseForPath("/api/pgp/recipients/resolve", httpResponse(200, "OK",
        QJsonDocument(QJsonObject{{"results", QJsonArray{key}}}).toJson()));
    fake.setResponseForPath("/api/mail/send-pgp", httpResponse(200, "OK", R"({"ok":true,"sentSaved":true})"));
    QSignalSpy sent(h.controller.get(), &MailController::sendCompleted);
    // Even the legacy entry point with both toggles off must encrypt a restored draft.
    QVERIFY(h.controller->sendMail(QStringLiteral("test@example.com"), {}, {}, message.subject,
                                  message.body, paths, false, false, session) != 0);
    QTRY_COMPARE_WITH_TIMEOUT(sent.size(), 1, 15000);
    QVERIFY2(sent.last()[1].toBool(), qPrintable(h.controller->lastError()));
    const auto deliveryRequest = fake.receivedRequests().last();
    QVERIFY(deliveryRequest.startsWith("POST /api/mail/send-pgp"));
    QVERIFY(!deliveryRequest.contains("restored-private"));
    const auto deliveryJson = QJsonDocument::fromJson(deliveryRequest.mid(deliveryRequest.indexOf("\r\n\r\n") + 4)).object();
    const auto delivery = deliveryJson.value("deliveries").toArray().first().toObject().value("ciphertext").toString().toUtf8();
    const int deliveryStart = delivery.indexOf("-----BEGIN PGP MESSAGE-----");
    const int deliveryEnd = delivery.indexOf("-----END PGP MESSAGE-----");
    QVERIFY(deliveryStart >= 0 && deliveryEnd > deliveryStart);
    const auto sentClear = OpenPgpDecryptor().decrypt(delivery.mid(deliveryStart, deliveryEnd + 25 - deliveryStart), m_fixture.path());
    QCOMPARE(sentClear.status, PgpDecryptStatus::Decrypted);
    const auto sentBody = readMimeBody(sentClear.plaintext);
    QCOMPARE(sentBody.attachments.size(), 2);
    QCOMPARE(sentBody.attachments[0].data, message.attachments[0].data);

    // An account custody change cannot downgrade a previously encrypted draft.
    fake.setResponse(httpResponse(200, "OK", R"({"hasIdentity":true,"protection":"server"})"));
    const auto beforeDowngrade = fake.receivedRequests().size();
    QVERIFY(h.controller->saveDraft(message.to.first(), {}, {}, message.subject, message.body, {}, session) != 0);
    QTRY_COMPARE_WITH_TIMEOUT(saved.size(), 2, 15000);
    QVERIFY(!saved.last()[1].toBool());
    QCOMPARE(fake.receivedRequests().size(), beforeDowngrade + 1); // bootstrap only

    // Even an attachment-free restored draft cannot cross to another pairing.
    auto pairing = h.pairingStore->load();
    QVERIFY(pairing.has_value());
    const auto original = *pairing;
    pairing->subscriberId = QStringLiteral("another-account");
    QVERIFY(h.pairingStore->save(*pairing));
    const auto requests = fake.receivedRequests().size();
    QCOMPARE(h.controller->saveDraft(message.to.first(), {}, {}, message.subject, message.body, {}, session), 0);
    QCOMPARE(h.controller->sendClientEncrypted(message.to.first(), {}, {}, message.subject, message.body, {}, session), 0);
    QCOMPARE(fake.receivedRequests().size(), requests);
    QVERIFY(h.pairingStore->save(original));
    h.controller->decryptMessage(row.messageId, row.folder);
    QTRY_VERIFY_WITH_TIMEOUT(!h.controller->decryptBusy(), 15000);
    const auto lockSeed = h.controller->reopenDecryptedDraft(h.controller->decryptedToken());
    const auto lockToken = lockSeed.value("token").toString();
    QVERIFY(!lockToken.isEmpty());
    h.controller->setAppLocked(true);
    h.controller->setAppLocked(false);
    const auto afterRead = fake.receivedRequests().size();
    QCOMPARE(h.controller->saveDraft(message.to.first(), {}, {}, message.subject, message.body, paths, lockToken), 0);
    QCOMPARE(fake.receivedRequests().size(), afterRead);
}

void MailDecryptionTest::pairingChangeReleasesRestoredDraft_data()
{
    QTest::addColumn<bool>("closeReaderFirst");
    QTest::newRow("reader-open") << false;
    QTest::newRow("reader-already-closed") << true;
}

void MailDecryptionTest::pairingChangeReleasesRestoredDraft()
{
    QFETCH(bool, closeReaderFirst);
    OutgoingMessage message;
    message.to = {QStringLiteral("test@example.com")};
    message.body = QStringLiteral("private draft");
    message.attachments = {{QStringLiteral("private.bin"), QStringLiteral("application/octet-stream"), QByteArray("secret attachment")}};
    const auto armored = m_fixture.encryptToTestKey(protectedDraftContent(message, {}, QStringLiteral("pairing-change")));
    QVERIFY(!armored.isEmpty());
    FakeRelayServer fake(payloadResponse(armored));
    DecryptHarness h;
    QVERIFY(h.build(fake));
    Email row;
    row.messageId = QStringLiteral("5");
    row.folder = QStringLiteral("Drafts");
    row.pgpEncrypted = true;
    QVERIFY(h.emailDao->insertOrReplace(row));
    h.controller->decryptMessage(row.messageId, row.folder);
    QTRY_VERIFY_WITH_TIMEOUT(!h.controller->decryptBusy(), 15000);
    const auto seed = h.controller->reopenDecryptedDraft(h.controller->decryptedToken());
    const auto token = seed.value("token").toString();
    QVERIFY(!token.isEmpty());
    QCOMPARE(h.controller->m_draftAttachments.size(), 1);
    if (closeReaderFirst) {
        h.controller->forgetDecrypted();
        QCOMPARE(h.controller->m_draftToken, token);
        QCOMPARE(h.controller->m_draftAttachments.size(), 1);
    }
    auto pairing = h.pairingStore->load();
    QVERIFY(pairing.has_value());
    pairing->subscriberId = QStringLiteral("replacement-account");
    QVERIFY(h.pairingStore->save(*pairing));
    h.controller->forgetDecrypted(); // Same hook used by PairingController::pairingChanged.
    QVERIFY(h.controller->m_draftToken.isEmpty());
    QVERIFY(h.controller->m_draftAttachments.isEmpty());
    h.controller->forgetDecrypted();
    h.controller->releaseDraft(token);
    QVERIFY(h.controller->m_draftAttachments.isEmpty());
}

void MailDecryptionTest::draftCustodyFailuresNeverPost_data()
{
    QTest::addColumn<QByteArray>("bootstrap");
    QTest::newRow("empty") << QByteArray("{}");
    QTest::newRow("wrong-type") << QByteArray(R"({"hasIdentity":"false","protection":""})");
    QTest::newRow("unknown-mode") << QByteArray(R"({"hasIdentity":true,"protection":"future"})");
    QTest::newRow("missing-identity") << QByteArray(R"({"hasIdentity":false,"protection":"client"})");
    QTest::newRow("missing-local-key") << QByteArray(R"({"hasIdentity":true,"protection":"client","fingerprint":"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA","suggestedUserIDs":["test@example.com"]})");
}

void MailDecryptionTest::draftCustodyFailuresNeverPost()
{
    QFETCH(QByteArray, bootstrap);
    FakeRelayServer fake(httpResponse(200, "OK", bootstrap));
    DecryptHarness h;
    QVERIFY(h.build(fake));
    QSignalSpy saved(h.controller.get(), &MailController::draftSaveCompleted);
    QVERIFY(h.controller->saveDraft(QStringLiteral("to@example.com"), {}, {}, QStringLiteral("Secret"), QStringLiteral("secret body"), {}) != 0);
    QTRY_COMPARE_WITH_TIMEOUT(saved.size(), 1, 10000);
    QVERIFY(!saved.first().at(1).toBool());
    QCOMPARE(fake.receivedRequests().size(), 1);
    QVERIFY(fake.receivedRequests().first().startsWith("GET /api/pgp/bootstrap"));
    QVERIFY(!h.controller->lastError().isEmpty());
}

void MailDecryptionTest::addressBootstrapWithRetiredKeyNeverUploads_data()
{
    QTest::addColumn<bool>("retired");
    QTest::newRow("one-key") << false;
    QTest::newRow("retired-key") << true;
}

void MailDecryptionTest::addressBootstrapWithRetiredKeyNeverUploads()
{
    QFETCH(bool, retired);
    // Keep both secret keys, as real users do for reading historical mail.
    if (retired)
        QVERIFY(m_fixture.build(QStringLiteral("Retired Identity <test@example.com>")));
    QCOMPARE(GnupgFixture::fingerprintsIn(m_fixture.path()).size(), retired ? 2 : 1);
    FakeRelayServer fake(httpResponse(200, "OK", R"({"hasIdentity":true,"protection":"client","fingerprint":"test@example.com","suggestedUserIDs":["test@example.com"]})"));
    fake.setResponseForPath("/api/mail/draft", httpResponse(200, "OK", R"({"ok":true})"));
    DecryptHarness h;
    QVERIFY(h.build(fake));
    QSignalSpy saved(h.controller.get(), &MailController::draftSaveCompleted);
    QVERIFY(h.controller->saveDraft(QStringLiteral("to@example.com"), {}, {}, QStringLiteral("Secret"), QStringLiteral("secret body"), {}) != 0);
    QTRY_COMPARE_WITH_TIMEOUT(saved.size(), 1, 10000);
    QVERIFY(!saved.first().at(1).toBool());
    QCOMPARE(fake.receivedRequests().size(), 1);
    QVERIFY(fake.receivedRequests().first().startsWith("GET /api/pgp/bootstrap"));
}

void MailDecryptionTest::serverCustodyDraftStillSaves()
{
    FakeRelayServer fake(httpResponse(200, "OK", R"({"hasIdentity":true,"protection":"server"})"));
    fake.setResponseForPath("/api/mail/draft", httpResponse(200, "OK", R"({"ok":true})"));
    DecryptHarness h;
    QVERIFY(h.build(fake));
    QSignalSpy saved(h.controller.get(), &MailController::draftSaveCompleted);
    QVERIFY(h.controller->saveDraft(QStringLiteral("to@example.com"), {}, {}, {}, QStringLiteral("draft body"), {}) != 0);
    QTRY_COMPARE(saved.size(), 1);
    QVERIFY(saved.first().at(1).toBool());
    QCOMPARE(fake.receivedRequests().size(), 2);
    QVERIFY(fake.receivedRequests().last().contains("draft body"));
    QVERIFY(!fake.receivedRequests().last().contains("pgpDraft"));
}

void MailDecryptionTest::decryptingAClientProtectedMessageShowsItsText()
{
    const QByteArray entity = QByteArray("Content-Type: text/plain; charset=utf-8\r\n\r\n") + kCanary + "\r\n";
    const QByteArray armored = m_fixture.encryptToTestKey(entity);
    QVERIFY(!armored.isEmpty());
    QVERIFY2(!armored.contains(kCanary), "the fixture did not actually encrypt anything");

    FakeRelayServer fake(httpResponse(200, "OK", inboxWithOneEncryptedMessage()));
    DecryptHarness harness;
    QVERIFY(harness.build(fake));

    harness.controller->refresh();
    QTRY_VERIFY_WITH_TIMEOUT(!harness.controller->isBusy(), 5000);

    // The row really is the state this feature exists for.
    const QVariantMap row = harness.controller->findByMessageId(QStringLiteral("5"));
    QCOMPARE(row.value(QStringLiteral("pgpState")).toInt(), 1); // ClientProtected
    QVERIFY2(row.value(QStringLiteral("body")).toString().isEmpty(),
             "a client-protected row arrived with a body");

    fake.setResponse(payloadResponse(armored));
    harness.controller->decryptMessage(QStringLiteral("5"));
    QTRY_VERIFY_WITH_TIMEOUT(!harness.controller->decryptBusy(), 15000);

    QCOMPARE(harness.controller->decryptFailure(), QString());
    QCOMPARE(harness.controller->decryptedMessageId(), QStringLiteral("5"));
    QVERIFY2(harness.controller->decryptedPlain().contains(QString::fromUtf8(kCanary)),
             "the decrypted text did not reach the reader");
    // The MIME headers are parsed away, not shown.
    QVERIFY2(!harness.controller->decryptedPlain().contains(QStringLiteral("Content-Type")),
             "MIME source was handed to the reader as the message");
}

// The rule the decrypt* properties document, enforced rather than asserted in
// a comment. Scans EVERY column of EVERY table, so it keeps holding if some
// later change starts writing the body somewhere other than the emails table.
void MailDecryptionTest::aDecryptedMessageNeverReachesTheDatabase()
{
    const QByteArray entity = QByteArray("Content-Type: text/plain; charset=utf-8\r\n\r\n") + kCanary + "\r\n";
    const QByteArray armored = m_fixture.encryptToTestKey(entity);
    QVERIFY(!armored.isEmpty());

    FakeRelayServer fake(httpResponse(200, "OK", inboxWithOneEncryptedMessage()));
    DecryptHarness harness;
    QVERIFY(harness.build(fake));

    harness.controller->refresh();
    QTRY_VERIFY_WITH_TIMEOUT(!harness.controller->isBusy(), 5000);

    fake.setResponse(payloadResponse(armored));
    harness.controller->decryptMessage(QStringLiteral("5"));
    QTRY_VERIFY_WITH_TIMEOUT(!harness.controller->decryptBusy(), 15000);

    // It really did decrypt -- otherwise "the plaintext is not in the
    // database" would be true for the uninteresting reason.
    QVERIFY2(harness.controller->decryptedPlain().contains(QString::fromUtf8(kCanary)),
             "nothing was decrypted, so this test proves nothing");

    const QString stored = everythingInTheDatabase(harness.db.handle());
    QVERIFY2(!stored.contains(QString::fromUtf8(kCanary)),
             "the decrypted message was written to the database");
    QVERIFY2(!stored.contains(QStringLiteral("BEGIN PGP MESSAGE")),
             "the ciphertext was written to the database");

    // And a re-read of the cached row still has no body: the cache was not
    // quietly updated in memory either.
    QVERIFY(harness.controller->findByMessageId(QStringLiteral("5"))
                .value(QStringLiteral("body"))
                .toString()
                .isEmpty());
}

void MailDecryptionTest::forgettingDropsTheHeldPlaintext()
{
    const QByteArray entity = QByteArray("Content-Type: text/plain; charset=utf-8\r\n\r\n") + kCanary + "\r\n";
    const QByteArray armored = m_fixture.encryptToTestKey(entity);
    QVERIFY(!armored.isEmpty());

    FakeRelayServer fake(httpResponse(200, "OK", inboxWithOneEncryptedMessage()));
    DecryptHarness harness;
    QVERIFY(harness.build(fake));

    harness.controller->refresh();
    QTRY_VERIFY_WITH_TIMEOUT(!harness.controller->isBusy(), 5000);
    fake.setResponse(payloadResponse(armored));
    harness.controller->decryptMessage(QStringLiteral("5"));
    QTRY_VERIFY_WITH_TIMEOUT(!harness.controller->decryptBusy(), 15000);
    QVERIFY(!harness.controller->decryptedPlain().isEmpty());

    // This is what the app lock calls. Nothing may survive it.
    harness.controller->forgetDecrypted();

    QCOMPARE(harness.controller->decryptedMessageId(), QString());
    QCOMPARE(harness.controller->decryptedPlain(), QString());
    QCOMPARE(harness.controller->decryptedHtml(), QString());
    QCOMPARE(harness.controller->decryptFailure(), QString());
}

// 409 has an instruction attached and no amount of retrying satisfies it.
void MailDecryptionTest::serverCustodyIsExplainedRatherThanRetried()
{
    FakeRelayServer fake(httpResponse(200, "OK", inboxWithOneEncryptedMessage()));
    DecryptHarness harness;
    QVERIFY(harness.build(fake));

    harness.controller->refresh();
    QTRY_VERIFY_WITH_TIMEOUT(!harness.controller->isBusy(), 5000);

    fake.setResponse(httpResponse(409, "Conflict", R"({"error":"migrate","migrationNeeded":true})"));
    harness.controller->decryptMessage(QStringLiteral("5"));
    QTRY_VERIFY_WITH_TIMEOUT(!harness.controller->decryptBusy(), 15000);

    QVERIFY2(!harness.controller->decryptFailure().isEmpty(), "the user was told nothing");
    QVERIFY2(!harness.controller->decryptRetryable(),
             "Retry was offered for a condition retrying cannot fix");
    QCOMPARE(harness.controller->decryptedMessageId(), QString());
}

void MailDecryptionTest::anOutageIsTheOneRetryableFailure()
{
    FakeRelayServer fake(httpResponse(200, "OK", inboxWithOneEncryptedMessage()));
    DecryptHarness harness;
    QVERIFY(harness.build(fake));

    harness.controller->refresh();
    QTRY_VERIFY_WITH_TIMEOUT(!harness.controller->isBusy(), 5000);

    fake.setResponse(httpResponse(503, "Service Unavailable", "down", "text/plain"));
    harness.controller->decryptMessage(QStringLiteral("5"));
    QTRY_VERIFY_WITH_TIMEOUT(!harness.controller->decryptBusy(), 15000);

    QVERIFY(!harness.controller->decryptFailure().isEmpty());
    QVERIFY2(harness.controller->decryptRetryable(), "an outage was reported as permanent");
}

// The stale-reply defect, at the site where the window is widest. pinentry
// waits for the user -- indefinitely, if the key is on a hardware token they
// have to walk across the room for -- so an account replacement landing
// mid-decrypt is far more plausible here than on any ordinary request. No
// table carries a subscriber column, so a plaintext shown after the swap is
// the previous account's mail in the new account's reader.
//
// Deterministic, not racy: decryptMessage() returns immediately and the
// reply is delivered through the event queue, so a pairing change made
// synchronously after the call is guaranteed to happen first. The test only
// enters the event loop at QTRY_VERIFY below.
void MailDecryptionTest::aReplyForAReplacedAccountIsNeverShown()
{
    const QByteArray entity = QByteArray("Content-Type: text/plain; charset=utf-8\r\n\r\n") + kCanary + "\r\n";
    const QByteArray armored = m_fixture.encryptToTestKey(entity);
    QVERIFY(!armored.isEmpty());

    FakeRelayServer fake(httpResponse(200, "OK", inboxWithOneEncryptedMessage()));
    DecryptHarness harness;
    QVERIFY(harness.build(fake));

    harness.controller->refresh();
    QTRY_VERIFY_WITH_TIMEOUT(!harness.controller->isBusy(), 5000);

    fake.setResponse(payloadResponse(armored));
    harness.controller->decryptMessage(QStringLiteral("5"));

    // A different account, on the same relay -- what pairing a second
    // subscriber on this device looks like.
    DevicePairing replacement;
    replacement.subscriberId = QStringLiteral("sub-2");
    replacement.deviceSecret = QStringLiteral("secret-2");
    replacement.serverBaseUrl = QStringLiteral("http://127.0.0.1:%1").arg(fake.port());
    replacement.deviceId = QStringLiteral("dev-2");
    QVERIFY(harness.pairingStore->save(replacement));

    QTRY_VERIFY_WITH_TIMEOUT(!harness.controller->decryptBusy(), 15000);

    QVERIFY2(!harness.controller->decryptedPlain().contains(QString::fromUtf8(kCanary)),
             "the previous account's decrypted mail was shown to the new account");
    QCOMPARE(harness.controller->decryptedMessageId(), QString());
    QCOMPARE(harness.controller->decryptedHtml(), QString());
}

// GUILESS, like MailControllerTest over the same sources. Nothing here draws
// anything, and QTEST_MAIN builds a QApplication that aborts on a headless
// runner before QtTest prints a single line -- which is exactly how this
// arrived in CI: "Subprocess aborted" with no output to read.
// The held plaintext must not survive an account replacement.
//
// decryptedMessageId is an IMAP UID -- "5" -- and UIDs are per-mailbox, not
// per-account. So after pairing a different account, UID 5 exists again and
// means somebody else's message. Everything that decides whether to show the
// held body compares that id, so a match is not evidence the body belongs to
// what is on screen.
//
// The reader moving on clears it, and so does the app lock, but neither fires
// when the account changes underneath a message that is already open.
void MailDecryptionTest::aDecryptedMessageIsNotVisibleAfterTheAccountIsReplaced()
{
    const QByteArray entity = QByteArray("Content-Type: text/plain; charset=utf-8\r\n\r\n") + kCanary + "\r\n";
    const QByteArray armored = m_fixture.encryptToTestKey(entity);
    QVERIFY(!armored.isEmpty());

    FakeRelayServer fake(httpResponse(200, "OK", inboxWithOneEncryptedMessage()));
    DecryptHarness harness;
    QVERIFY(harness.build(fake));

    harness.controller->refresh();
    QTRY_VERIFY_WITH_TIMEOUT(!harness.controller->isBusy(), 5000);
    fake.setResponse(payloadResponse(armored));
    harness.controller->decryptMessage(QStringLiteral("5"));
    QTRY_VERIFY_WITH_TIMEOUT(!harness.controller->decryptBusy(), 15000);
    QVERIFY(harness.controller->decryptedPlain().contains(QString::fromUtf8(kCanary)));

    // A different account on this device.
    DevicePairing replacement;
    replacement.subscriberId = QStringLiteral("sub-2");
    replacement.deviceSecret = QStringLiteral("secret-2");
    replacement.serverBaseUrl = QStringLiteral("http://127.0.0.1:%1").arg(fake.port());
    replacement.deviceId = QStringLiteral("dev-2");
    QVERIFY(harness.pairingStore->save(replacement));

    QVERIFY2(harness.controller->decryptedMessageId().isEmpty(),
             "the previous account's decrypted message is still claimed for this UID");
    QVERIFY2(!harness.controller->decryptedPlain().contains(QString::fromUtf8(kCanary)),
             "the previous account's plaintext is still readable");
    QVERIFY(harness.controller->decryptedHtml().isEmpty());
}

void MailDecryptionTest::forgettingInvalidatesAnInFlightRead()
{
    const QByteArray armored = m_fixture.encryptToTestKey("Content-Type: text/plain\r\nSubject: Secret\r\n\r\nsecret body");
    QVERIFY(!armored.isEmpty());
    FakeRelayServer fake(httpResponse(200, "OK", inboxWithOneEncryptedMessage()));
    DecryptHarness h;
    QVERIFY(h.build(fake));
    h.controller->refresh();
    QTRY_VERIFY(!h.controller->isBusy());
    fake.setResponse(payloadResponse(armored));
    h.controller->decryptMessage(QStringLiteral("5"));
    QVERIFY(h.controller->decryptBusy());
    h.controller->forgetDecrypted();
    QTRY_VERIFY_WITH_TIMEOUT(!h.controller->decryptBusy(), 15000);
    QVERIFY(h.controller->decryptedMessageId().isEmpty());
    QVERIFY(h.controller->decryptedSubject().isEmpty());
    QVERIFY(h.controller->decryptedPlain().isEmpty());
}

void MailDecryptionTest::lockingInvalidatesAnInFlightRead()
{
    const QByteArray armored = m_fixture.encryptToTestKey("Content-Type: text/plain\r\nSubject: Secret\r\n\r\nsecret body");
    QVERIFY(!armored.isEmpty());
    FakeRelayServer fake(httpResponse(200, "OK", inboxWithOneEncryptedMessage()));
    DecryptHarness h;
    QVERIFY(h.build(fake));
    h.controller->refresh();
    QTRY_VERIFY(!h.controller->isBusy());
    fake.setResponse(payloadResponse(armored));
    h.controller->decryptMessage(QStringLiteral("5"));
    h.controller->setAppLocked(true);
    h.controller->setAppLocked(false);
    QTRY_VERIFY_WITH_TIMEOUT(!h.controller->decryptBusy(), 15000);
    QVERIFY(h.controller->decryptedMessageId().isEmpty());
    QVERIFY(h.controller->decryptedSubject().isEmpty());
    h.controller->setAppLocked(true);
    h.controller->decryptMessage(QStringLiteral("5"));
    QVERIFY(!h.controller->decryptBusy());
}

void MailDecryptionTest::protectedSubjectIsTransient()
{
    const QByteArray armored = m_fixture.encryptToTestKey("Content-Type: text/plain\r\nSubject: protected-subject-canary\r\n\r\nbody");
    QVERIFY(!armored.isEmpty());
    FakeRelayServer fake(httpResponse(200, "OK", inboxWithOneEncryptedMessage()));
    DecryptHarness h;
    QVERIFY(h.build(fake));
    h.controller->refresh();
    QTRY_VERIFY(!h.controller->isBusy());
    fake.setResponse(payloadResponse(armored));
    h.controller->decryptMessage(QStringLiteral("5"));
    QTRY_VERIFY_WITH_TIMEOUT(!h.controller->decryptBusy(), 15000);
    QCOMPARE(h.controller->decryptedSubject(), QStringLiteral("protected-subject-canary"));
    QCOMPARE(h.controller->decryptedFolder(), QStringLiteral("INBOX"));
    QVERIFY(!everythingInTheDatabase(h.db.handle()).contains(QStringLiteral("protected-subject-canary")));
    h.controller->setAppLocked(true);
    QVERIFY(h.controller->decryptedSubject().isEmpty());
    QVERIFY(h.controller->decryptedFolder().isEmpty());
}

void MailDecryptionTest::duplicateUidsUseTheSelectedFolder()
{
    const QByteArray armored = m_fixture.encryptToTestKey("Content-Type: text/plain\r\nSubject: Archive subject\r\n\r\narchive body");
    QVERIFY(!armored.isEmpty());
    FakeRelayServer fake(httpResponse(200, "OK", inboxWithOneEncryptedMessage()));
    DecryptHarness h;
    QVERIFY(h.build(fake));
    h.controller->refresh();
    QTRY_VERIFY(!h.controller->isBusy());
    auto copy = h.mailRepository->cachedEmail(QStringLiteral("INBOX"), QStringLiteral("5"));
    QVERIFY(copy.has_value());
    copy->folder = QStringLiteral("Archive");
    QVERIFY(h.emailDao->insertOrReplace(*copy));
    QVERIFY(h.controller->findByMessageId(QStringLiteral("5")).isEmpty());
    QCOMPARE(h.controller->findByMessageId(QStringLiteral("5"), QStringLiteral("Archive"))
        .value(QStringLiteral("folder")).toString(), QStringLiteral("Archive"));
    fake.setResponse(payloadResponse(armored));
    h.controller->decryptMessage(QStringLiteral("5"), QStringLiteral("Archive"));
    QTRY_VERIFY_WITH_TIMEOUT(!h.controller->decryptBusy(), 15000);
    QCOMPARE(h.controller->decryptedFolder(), QStringLiteral("Archive"));
    QCOMPARE(h.controller->decryptedSubject(), QStringLiteral("Archive subject"));
    QVERIFY(fake.receivedRequest().contains("mailbox=Archive"));
}

void MailDecryptionTest::decryptedAttachmentsStayLocalAndRequireTheCurrentToken()
{
    OutgoingMessage message;
    message.mode = QStringLiteral("plain");
    message.attachments = {{QStringLiteral("../secret.bin"), QStringLiteral("application/octet-stream"), QByteArray::fromHex("00ff010203")}};
    const auto encrypted = m_fixture.encryptToTestKey(protectedContent(message, QStringLiteral("b")));
    QVERIFY(!encrypted.isEmpty());
    FakeRelayServer fake(httpResponse(200, "OK", inboxWithOneEncryptedMessage()));
    DecryptHarness h;
    QVERIFY(h.build(fake));
    h.controller->refresh();
    QTRY_VERIFY(!h.controller->isBusy());
    fake.setResponse(payloadResponse(encrypted));
    h.controller->decryptMessage(QStringLiteral("5"));
    QTRY_VERIFY_WITH_TIMEOUT(!h.controller->decryptBusy(), 15000);
    QVERIFY(h.controller->decryptFailure().isEmpty());
    QCOMPARE(h.controller->decryptedAttachments().size(), 1);
    QCOMPARE(h.controller->decryptedAttachments()[0].toMap().value(QStringLiteral("name")).toString(), QStringLiteral("secret.bin"));
    const QString token = h.controller->decryptedToken();
    QVERIFY(!token.isEmpty());
    QTemporaryDir output;
    QVERIFY(output.isValid());
    const QUrl target = QUrl::fromLocalFile(output.filePath(QStringLiteral("saved.bin")));
    QVERIFY(!h.controller->saveDecryptedAttachment(QStringLiteral("wrong"), 0, target));
    QVERIFY(!QFile::exists(target.toLocalFile()));
    QVERIFY(h.controller->saveDecryptedAttachment(token, 0, target));
    QFile saved(target.toLocalFile());
    QVERIFY(saved.open(QIODevice::ReadOnly));
    QCOMPARE(saved.readAll(), message.attachments[0].data);
    QVERIFY(!(saved.permissions() & (QFile::ReadGroup | QFile::ReadOther)));
    QVERIFY(h.settingsStore->setHostileLocationProtectionEnabled(true));
    QVERIFY(!h.controller->saveDecryptedAttachment(token, 0, target));
    QVERIFY(h.settingsStore->setHostileLocationProtectionEnabled(false));
    QVERIFY(!h.controller->saveDecryptedAttachment(token, 0, QUrl::fromLocalFile(output.path())));
    h.controller->forgetDecrypted();
    QVERIFY(h.controller->decryptedAttachments().isEmpty());
    QVERIFY(!h.controller->saveDecryptedAttachment(token, 0, target));
}

void MailDecryptionTest::cidImagesAreBoundToTheCurrentUnlockedMessage()
{
    QByteArray png;
    QBuffer buffer(&png);
    QVERIFY(buffer.open(QIODevice::WriteOnly));
    QImage image(2, 2, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(&buffer, "PNG"));
    const auto entity = QByteArray("Content-Type: multipart/related; boundary=b\r\n\r\n"
        "--b\r\nContent-Type: text/html\r\n\r\n<img src=\"cid:logo@example\">\r\n"
        "--b\r\nContent-Type: image/png\r\nContent-ID: <logo@example>\r\nContent-Transfer-Encoding: base64\r\n\r\n")
        + png.toBase64() + "\r\n--b--\r\n";
    const auto encrypted = m_fixture.encryptToTestKey(entity);
    QVERIFY(!encrypted.isEmpty());
    FakeRelayServer fake(httpResponse(200, "OK", inboxWithOneEncryptedMessage()));
    DecryptHarness h;
    QVERIFY(h.build(fake));
    h.controller->refresh();
    QTRY_VERIFY(!h.controller->isBusy());
    fake.setResponse(payloadResponse(encrypted));
    h.controller->decryptMessage(QStringLiteral("5"));
    QTRY_VERIFY_WITH_TIMEOUT(!h.controller->decryptBusy(), 15000);
    const QUrl url(h.controller->decryptedImageBase() + QStringLiteral("logo@example"));
    QCOMPARE(h.controller->protectedImage(url).second, png);
    QCOMPARE(h.controller->protectedImage(url).first, QByteArray("image/png"));
    QVERIFY(h.controller->protectedImage(QUrl(QStringLiteral("kypost-cid://wrong/logo@example"))).second.isEmpty());
    h.controller->setAppLocked(true);
    QVERIFY(h.controller->protectedImage(url).second.isEmpty());
    h.controller->setAppLocked(false);
    QVERIFY(h.controller->protectedImage(url).second.isEmpty());
}

QTEST_GUILESS_MAIN(MailDecryptionTest)
#include "MailDecryptionTest.moc"
