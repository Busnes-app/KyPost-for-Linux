#include "net/RelayMailSource.h"

#include "models/Email.h"
#include "net/HttpClient.h"
#include "net/RelayAuth.h"

#include "FakeRelayServer.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QTest>
#include <QTimeZone>

class RelayMailSourceTest : public QObject
{
    Q_OBJECT

private slots:
    void oversizedEncryptedDraftNeverPosts();
    void fetchInboxMapsTwoTabsWithAtUtcPassthroughAndOptionalFields();
    void fetchInboxSendsLimitMailboxSinceAsQueryParamsAndAuthAsHeaders();
    void fetchInboxOmitsLimitAndSinceWhenNotProvided();
    void fetchInboxUnauthorizedFrom401PassesErrorThrough();

    void performActionMoveIncludesTargetMailboxInRequestBody();
    void performActionReadOmitsTargetMailboxFromRequestBodyButResponseCarriesEmptyString();
    void performActionSnoozeSendsWakeAtAsWholeSecondUtc();

    void sendMailJoinsRecipientsAndBase64EncodesAttachmentByteForByte();
    void sendMailSendsEmptyAttachmentsArrayWhenNoneProvided();
    void sendMailParsesAlwaysPresentWarningField();
    void sendMailPutsPgpFlagsInTheRequestBody();
    void sendMailParsesKeylessRecipient409();
    void sendMailClientSideNeeded409IsNotAKeylessRefusal();
    void sendMailDualField409ReportsTheUnrecoverableRefusal();
    void sendMail409WithNeitherPgpFieldStaysAGenericError();
    void sendMailMalformed409BodyDoesNotCrashTheDecode();
    void sendMailNamesTheAnsweredMessageOnlyForAReply();
    void sendMailRecognisesOnlyTheRelaysThreadingRefusals_data();
    void sendMailRecognisesOnlyTheRelaysThreadingRefusals();

    void listAttachmentsSendsMailboxMessageIdAsQueryParamsAndAuthAsHeadersAndParsesResult();

    void downloadAttachmentReturnsRawBytesAndParsesFilenameFromContentDisposition();
    void downloadAttachmentMapsNotFoundFrom404();

    void searchMailSendsTheRelayContractAndMapsMetadataRows();
    void searchMailDropsRowsPastTheCap();
    void searchMailRefusesUnsearchableQueriesWithoutARequest();
    void searchMailPassesHttpErrorsThrough();
    void fetchBodyReturnsBodyAndMode();

    void sendAtTravelsAsUtcWholeSeconds();
    void sendMailCarriesSendAtAndReadsTheHold();
    void sendMailWithoutSendAtSendsNone();
    void anUnaskedFor202IsNotASend();
    void aPlainTextRefusalIsTheDetail();
    void listScheduledMapsRows();
    void listScheduledWithoutTheRouteIsUnsupported();
    void cancelScheduledPostsToThatId();
    void cancelScheduledTellsPendingFromReturned();
    void cancelScheduledNamesGoneAndClaimed();
    void cancelScheduledRefusesAnIdThatIsNotTheRelays();
};

void RelayMailSourceTest::oversizedEncryptedDraftNeverPosts()
{
    FakeRelayServer fake(httpResponse(200, "OK", R"({"ok":true})"));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);
    const QUrl base(QStringLiteral("http://127.0.0.1:%1").arg(fake.port()));
    const auto result = source.saveDraft(base, {QStringLiteral("device"), QStringLiteral("secret")},
        QStringLiteral("to@example.com"), {}, {}, {}, {}, {}, {}, QString(25 * 1024 * 1024, QLatin1Char('A')));
    QVERIFY(!result.ok);
    QCOMPARE(result.error, std::optional<NetworkError>(NetworkError::ResponseTooLarge));
    QCOMPARE(fake.receivedRequests().size(), 0);
}

void RelayMailSourceTest::fetchInboxMapsTwoTabsWithAtUtcPassthroughAndOptionalFields()
{
    const QByteArray body = R"(
    {
      "tabs": ["Inbox", "Archive"],
      "byTab": {
        "Inbox": [
          {
            "messageId": "m1",
            "sender": "alice@example.com",
            "sentTo": "bob@example.com",
            "cc": "cc@example.com",
            "bcc": "bcc@example.com",
            "subject": "Hello",
            "body": "Body text",
            "bodyMode": "plain",
            "status": "unread",
            "atUtc": "2026-07-01T12:00:00Z",
            "wokenAt": "2026-07-05T09:00:00Z",
            "hasAttachments": true,
            "label": "important",
            "keywords": ["Primary", "$Phishing"],
            "detail": "queued",
            "changeType": "updated"
          }
        ],
        "Archive": [
          {
            "messageId": "m2",
            "sender": "carol@example.com",
            "sentTo": "dave@example.com",
            "cc": "",
            "bcc": "",
            "subject": "Archived",
            "status": "read",
            "atUtc": "2026-06-01T08:30:00Z",
            "hasAttachments": false,
            "label": ""
          }
        ]
      }
    }
    )";
    FakeRelayServer fake(httpResponse(200, "OK", body));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);

    const QUrl serverBaseUrl(QStringLiteral("http://127.0.0.1:%1").arg(fake.port()));
    const RelayAuth auth{ QStringLiteral("device-1"), QStringLiteral("secret-1") };
    const InboxFetchResult result = source.fetchInbox(serverBaseUrl, auth, 100, QStringLiteral("Inbox"), qint64(0));

    QVERIFY(!result.error.has_value());
    QCOMPARE(result.tabs, QStringList({ QStringLiteral("Inbox"), QStringLiteral("Archive") }));
    QCOMPARE(result.byTab.size(), 2);

    QVERIFY(result.byTab.contains(QStringLiteral("Inbox")));
    QCOMPARE(result.byTab.value(QStringLiteral("Inbox")).size(), 1);
    // The list is bound to a named local first: QHash::value() returns by
    // value, so `const InboxEmailItem& = ....value(k).at(0)` binds a
    // reference into a temporary QList that dies at the end of the
    // statement. It happens not to crash today only because QList's
    // implicit sharing keeps the buffer alive via the copy still in the
    // hash -- which is luck, not a guarantee (-Wdangling-reference).
    const QVector<InboxEmailItem> inboxItems = result.byTab.value(QStringLiteral("Inbox"));
    const InboxEmailItem& item1 = inboxItems.at(0);
    QCOMPARE(item1.email.messageId, QStringLiteral("m1"));
    QCOMPARE(item1.email.sender, QStringLiteral("alice@example.com"));
    QCOMPARE(item1.email.sentTo, QStringLiteral("bob@example.com"));
    QCOMPARE(item1.email.cc, QStringLiteral("cc@example.com"));
    QCOMPARE(item1.email.bcc, QStringLiteral("bcc@example.com"));
    QCOMPARE(item1.email.subject, QStringLiteral("Hello"));
    QVERIFY(item1.email.body.has_value());
    QCOMPARE(*item1.email.body, QStringLiteral("Body text"));
    // bodyMode travels with the body. Parsed rather than re-derived: sniffing
    // "Body text" would be harmless, but sniffing a plain-text message that
    // opens with an RFC 5322 address deletes the address -- see
    // Email::bodyMode.
    QVERIFY(item1.email.bodyMode.has_value());
    QCOMPARE(*item1.email.bodyMode, QStringLiteral("plain"));
    // No distinct "preview" key exists on the wire (confirmed against the Go
    // backend) -- preview must stay empty, not be populated from body.
    QVERIFY(item1.email.preview.isEmpty());
    QCOMPARE(item1.email.status, QStringLiteral("unread"));
    // atUtc is a direct pass-through of the wire key of the same name -- no
    // casing translation, assert it is unchanged.
    QCOMPARE(item1.email.atUtc, QStringLiteral("2026-07-01T12:00:00Z"));
    QCOMPARE(item1.email.wokenAt, std::optional<QString>(QStringLiteral("2026-07-05T09:00:00Z")));
    QCOMPARE(item1.email.hasAttachments, true);
    QCOMPARE(item1.email.label, QStringLiteral("important"));
    // Keywords carry the server's real IMAP keywords, including the $Phishing
    // anti-phishing flag the warning banner reads. Never parsed before this,
    // which also meant MailController's keyword filter could never match.
    QCOMPARE(item1.email.keywords, QStringList({ QStringLiteral("Primary"), QStringLiteral("$Phishing") }));
    // folder is set from the enclosing byTab map key, not a wire field.
    QCOMPARE(item1.email.folder, QStringLiteral("Inbox"));
    QCOMPARE(item1.detail, QStringLiteral("queued"));
    QVERIFY(item1.changeType.has_value());
    QCOMPARE(*item1.changeType, QStringLiteral("updated"));

    QVERIFY(result.byTab.contains(QStringLiteral("Archive")));
    QCOMPARE(result.byTab.value(QStringLiteral("Archive")).size(), 1);
    const QVector<InboxEmailItem> archiveItems = result.byTab.value(QStringLiteral("Archive"));
    const InboxEmailItem& item2 = archiveItems.at(0);
    QCOMPARE(item2.email.messageId, QStringLiteral("m2"));
    QCOMPARE(item2.email.folder, QStringLiteral("Archive"));
    // "body"/"bodyMode"/"detail"/"changeType" absent from the wire ->
    // nullopt/empty, not a parse error.
    QVERIFY(item2.email.keywords.isEmpty());
    QVERIFY(!item2.email.body.has_value());
    // Absent bodyMode stays absent -- "the server did not say" is a distinct
    // state from "plain", and only it falls back to the sniff.
    QVERIFY(!item2.email.bodyMode.has_value());
    // Absent wokenAt is "not woken", and on an updated row clears the pin.
    QVERIFY(!item2.email.wokenAt.has_value());
    QVERIFY(item2.detail.isEmpty());
    QVERIFY(!item2.changeType.has_value());
}

void RelayMailSourceTest::fetchInboxSendsLimitMailboxSinceAsQueryParamsAndAuthAsHeaders()
{
    FakeRelayServer fake(httpResponse(200, "OK", R"({"tabs":[],"byTab":{}})"));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);

    const QUrl serverBaseUrl(QStringLiteral("http://127.0.0.1:%1").arg(fake.port()));
    const RelayAuth auth{ QStringLiteral("device-9"), QStringLiteral("secret-9") };
    source.fetchInbox(serverBaseUrl, auth, 250, QStringLiteral("Inbox"), qint64(12345));

    const QByteArray request = fake.receivedRequest();
    QVERIFY(request.contains("GET /api/inbox?"));
    QVERIFY(request.contains("limit=250"));
    QVERIFY(request.contains("mailbox=Inbox"));
    QVERIFY(request.contains("since=12345"));
    QVERIFY(request.contains("X-Kypost-Device-Id: device-9"));
    QVERIFY(request.contains("X-Kypost-Device-Secret: secret-9"));
    QVERIFY(!request.contains("device=device-9"));
    QVERIFY(!request.contains("secret=secret-9"));
}

void RelayMailSourceTest::fetchInboxOmitsLimitAndSinceWhenNotProvided()
{
    FakeRelayServer fake(httpResponse(200, "OK", R"({"tabs":[],"byTab":{}})"));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);

    const QUrl serverBaseUrl(QStringLiteral("http://127.0.0.1:%1").arg(fake.port()));
    const RelayAuth auth{ QStringLiteral("device-1"), QStringLiteral("secret-1") };
    source.fetchInbox(serverBaseUrl, auth, std::nullopt, QStringLiteral("Inbox"), std::nullopt);

    const QByteArray request = fake.receivedRequest();
    QVERIFY(!request.contains("limit="));
    QVERIFY(!request.contains("since="));
    QVERIFY(request.contains("mailbox=Inbox"));
}

void RelayMailSourceTest::fetchInboxUnauthorizedFrom401PassesErrorThrough()
{
    FakeRelayServer fake(httpResponse(401, "Unauthorized", "Unauthorized\n"));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);

    const QUrl serverBaseUrl(QStringLiteral("http://127.0.0.1:%1").arg(fake.port()));
    const RelayAuth auth{ QStringLiteral("device-1"), QStringLiteral("secret-1") };
    const InboxFetchResult result = source.fetchInbox(serverBaseUrl, auth, std::nullopt, QStringLiteral("Inbox"), std::nullopt);

    QVERIFY(result.error.has_value());
    QCOMPARE(*result.error, NetworkError::Unauthorized);
    QVERIFY(result.byTab.isEmpty());
}

void RelayMailSourceTest::performActionMoveIncludesTargetMailboxInRequestBody()
{
    FakeRelayServer fake(
        httpResponse(200, "OK", R"({"ok":true,"action":"move","processed":2,"failed":[],"targetMailbox":"Archive"})"));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);

    const QUrl serverBaseUrl(QStringLiteral("http://127.0.0.1:%1").arg(fake.port()));
    const RelayAuth auth{ QStringLiteral("device-1"), QStringLiteral("secret-1") };
    const ActionResult result = source.performAction(serverBaseUrl, auth, QStringLiteral("move"),
                                                       { QStringLiteral("m1"), QStringLiteral("m2") },
                                                       QStringLiteral("Inbox"), QStringLiteral("Archive"));

    QVERIFY(!result.error.has_value());
    QCOMPARE(result.ok, true);
    QCOMPARE(result.action, QStringLiteral("move"));
    QCOMPARE(result.processed, 2);
    QVERIFY(result.failed.isEmpty());
    QCOMPARE(result.targetMailbox, QStringLiteral("Archive"));

    const QByteArray request = fake.receivedRequest();
    QVERIFY(request.contains("POST /api/inbox/actions HTTP/1.1"));
    QVERIFY(request.contains("X-Kypost-Device-Id: device-1"));
    QVERIFY(request.contains("X-Kypost-Device-Secret: secret-1"));
    QVERIFY(!request.contains("device=device-1"));
    QVERIFY(!request.contains("secret=secret-1"));
    const QJsonObject sent = fake.receivedJsonBody();
    QCOMPARE(sent.value(QStringLiteral("action")).toString(), QStringLiteral("move"));
    QCOMPARE(sent.value(QStringLiteral("mailbox")).toString(), QStringLiteral("Inbox"));
    QCOMPARE(sent.value(QStringLiteral("messageIds")).toArray().size(), 2);
    QVERIFY(sent.contains(QStringLiteral("targetMailbox")));
    QCOMPARE(sent.value(QStringLiteral("targetMailbox")).toString(), QStringLiteral("Archive"));
}

void RelayMailSourceTest::performActionReadOmitsTargetMailboxFromRequestBodyButResponseCarriesEmptyString()
{
    // targetMailbox is ALWAYS present on the wire response (even as "" when
    // the action wasn't "move") -- but must never be sent in the *request*
    // body for a non-move action.
    FakeRelayServer fake(httpResponse(
        200, "OK",
        R"({"ok":true,"action":"read","processed":0,"failed":[{"messageId":"m3","error":"not found"}],"targetMailbox":""})"));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);

    const QUrl serverBaseUrl(QStringLiteral("http://127.0.0.1:%1").arg(fake.port()));
    const RelayAuth auth{ QStringLiteral("device-1"), QStringLiteral("secret-1") };
    const ActionResult result = source.performAction(serverBaseUrl, auth, QStringLiteral("read"),
                                                       { QStringLiteral("m3") }, QStringLiteral("Inbox"), std::nullopt);

    QVERIFY(!result.error.has_value());
    QCOMPARE(result.action, QStringLiteral("read"));
    QCOMPARE(result.processed, 0);
    // targetMailbox is present in the response as "", parsed as such -- not
    // an absent/default value being confused with "omitted".
    QCOMPARE(result.targetMailbox, QString());
    QVERIFY(result.targetMailbox.isEmpty());
    QCOMPARE(result.failed.size(), 1);
    QCOMPARE(result.failed.at(0).messageId, QStringLiteral("m3"));
    QCOMPARE(result.failed.at(0).error, QStringLiteral("not found"));

    const QJsonObject sent = fake.receivedJsonBody();
    QCOMPARE(sent.value(QStringLiteral("action")).toString(), QStringLiteral("read"));
    QVERIFY(!sent.contains(QStringLiteral("targetMailbox")));
    QVERIFY(!sent.contains(QStringLiteral("wakeAt")));
}

// snooze.go parseWakeAt is time.RFC3339, so a zone is required. A local time
// with milliseconds goes out as whole-second UTC, as Android sends it.
void RelayMailSourceTest::performActionSnoozeSendsWakeAtAsWholeSecondUtc()
{
    FakeRelayServer fake(
        httpResponse(200, "OK", R"({"ok":true,"action":"snooze","processed":1,"failed":[],"targetMailbox":""})"));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);

    const QUrl serverBaseUrl(QStringLiteral("http://127.0.0.1:%1").arg(fake.port()));
    const RelayAuth auth{ QStringLiteral("device-1"), QStringLiteral("secret-1") };
    const QDateTime wakeAt(QDate(2026, 10, 18), QTime(10, 0, 0, 250), QTimeZone("Europe/Berlin"));
    QVERIFY(wakeAt.isValid());
    const ActionResult result = source.performAction(serverBaseUrl, auth, QStringLiteral("snooze"),
                                                       { QStringLiteral("m1") }, QStringLiteral("INBOX"),
                                                       std::nullopt, wakeAt);

    QVERIFY(!result.error.has_value());
    QVERIFY(result.ok);
    const QJsonObject sent = fake.receivedJsonBody();
    QCOMPARE(sent.value(QStringLiteral("action")).toString(), QStringLiteral("snooze"));
    QCOMPARE(sent.value(QStringLiteral("mailbox")).toString(), QStringLiteral("INBOX"));
    QCOMPARE(sent.value(QStringLiteral("messageIds")).toArray(), QJsonArray{ QStringLiteral("m1") });
    QCOMPARE(sent.value(QStringLiteral("wakeAt")).toString(), QStringLiteral("2026-10-18T08:00:00Z"));
    QVERIFY(!sent.contains(QStringLiteral("targetMailbox")));
}

void RelayMailSourceTest::sendMailJoinsRecipientsAndBase64EncodesAttachmentByteForByte()
{
    FakeRelayServer fake(httpResponse(200, "OK", R"({"ok":true,"sentSaved":true,"warning":""})"));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);

    // Exercise every byte value 0-255 so the base64 round trip can't pass
    // by accident on a text-only fixture.
    QByteArray attachmentBytes;
    for (int i = 0; i < 256; ++i)
        attachmentBytes.append(static_cast<char>(i));

    MailAttachmentUpload attachment;
    attachment.name = QStringLiteral("data.bin");
    attachment.mimeType = QStringLiteral("application/octet-stream");
    attachment.data = attachmentBytes;

    const QUrl serverBaseUrl(QStringLiteral("http://127.0.0.1:%1").arg(fake.port()));
    const RelayAuth auth{ QStringLiteral("device-1"), QStringLiteral("secret-1") };
    const SendMailResult result =
        source.sendMail(serverBaseUrl, auth, QStringLiteral("a@example.com,b@example.com"),
                         QStringLiteral("cc@example.com"), QString(), QStringLiteral("Hello"),
                         QStringLiteral("Body text"), QStringLiteral("plain"), { attachment });

    QVERIFY(!result.error.has_value());
    QCOMPARE(result.ok, true);
    QCOMPARE(result.sentSaved, true);
    QVERIFY(result.warning.isEmpty());

    const QByteArray request = fake.receivedRequest();
    QVERIFY(request.contains("POST /api/mail/send HTTP/1.1"));
    QVERIFY(request.contains("X-Kypost-Device-Id: device-1"));
    QVERIFY(request.contains("X-Kypost-Device-Secret: secret-1"));
    QVERIFY(!request.contains("device=device-1"));
    QVERIFY(!request.contains("secret=secret-1"));
    const QJsonObject sent = fake.receivedJsonBody();
    // to/cc/bcc travel as comma-joined strings, not JSON arrays -- this
    // client does not split/join on the caller's behalf.
    QCOMPARE(sent.value(QStringLiteral("to")).toString(), QStringLiteral("a@example.com,b@example.com"));
    QCOMPARE(sent.value(QStringLiteral("cc")).toString(), QStringLiteral("cc@example.com"));
    QCOMPARE(sent.value(QStringLiteral("bcc")).toString(), QString());
    QCOMPARE(sent.value(QStringLiteral("subject")).toString(), QStringLiteral("Hello"));
    QCOMPARE(sent.value(QStringLiteral("body")).toString(), QStringLiteral("Body text"));
    QCOMPARE(sent.value(QStringLiteral("mode")).toString(), QStringLiteral("plain"));

    const QJsonArray sentAttachments = sent.value(QStringLiteral("attachments")).toArray();
    QCOMPARE(sentAttachments.size(), 1);
    const QJsonObject sentAttachment = sentAttachments.at(0).toObject();
    QCOMPARE(sentAttachment.value(QStringLiteral("name")).toString(), QStringLiteral("data.bin"));
    QCOMPARE(sentAttachment.value(QStringLiteral("mimeType")).toString(), QStringLiteral("application/octet-stream"));

    // Byte-for-byte round trip: decode what actually reached the wire and
    // compare against the original bytes, not merely "the field is present".
    const QByteArray decoded =
        QByteArray::fromBase64(sentAttachment.value(QStringLiteral("dataBase64")).toString().toLatin1());
    QCOMPARE(decoded, attachmentBytes);
}

void RelayMailSourceTest::sendMailSendsEmptyAttachmentsArrayWhenNoneProvided()
{
    FakeRelayServer fake(httpResponse(200, "OK", R"({"ok":true,"sentSaved":true,"warning":""})"));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);

    const QUrl serverBaseUrl(QStringLiteral("http://127.0.0.1:%1").arg(fake.port()));
    const RelayAuth auth{ QStringLiteral("device-1"), QStringLiteral("secret-1") };
    source.sendMail(serverBaseUrl, auth, QStringLiteral("a@example.com"), QString(), QString(),
                     QStringLiteral("Hi"), QStringLiteral("Body"), QStringLiteral("plain"), {});

    const QJsonObject sent = fake.receivedJsonBody();
    QVERIFY(sent.contains(QStringLiteral("attachments")));
    QVERIFY(sent.value(QStringLiteral("attachments")).toArray().isEmpty());
}

void RelayMailSourceTest::sendMailParsesAlwaysPresentWarningField()
{
    // sentSaved=false + a non-empty warning is the "sent but Sent-folder
    // save failed" case from handleMailSend -- ok is still true.
    FakeRelayServer fake(httpResponse(
        200, "OK",
        R"({"ok":true,"sentSaved":false,"warning":"email sent but could not be saved to Sent folder"})"));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);

    const QUrl serverBaseUrl(QStringLiteral("http://127.0.0.1:%1").arg(fake.port()));
    const RelayAuth auth{ QStringLiteral("device-1"), QStringLiteral("secret-1") };
    const SendMailResult result = source.sendMail(serverBaseUrl, auth, QStringLiteral("a@example.com"), QString(),
                                                    QString(), QStringLiteral("Hi"), QStringLiteral("Body"),
                                                    QStringLiteral("plain"), {});

    QVERIFY(!result.error.has_value());
    QCOMPARE(result.ok, true);
    QCOMPARE(result.sentSaved, false);
    QCOMPARE(result.warning, QStringLiteral("email sent but could not be saved to Sent folder"));
}

void RelayMailSourceTest::listAttachmentsSendsMailboxMessageIdAsQueryParamsAndAuthAsHeadersAndParsesResult()
{
    const QByteArray body = R"(
    {
      "ok": true,
      "attachments": [
        {"index": 0, "name": "report.pdf", "mimeType": "application/pdf", "size": 1024},
        {"index": 1, "name": "image.png", "mimeType": "image/png", "size": 2048}
      ]
    }
    )";
    FakeRelayServer fake(httpResponse(200, "OK", body));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);

    const QUrl serverBaseUrl(QStringLiteral("http://127.0.0.1:%1").arg(fake.port()));
    const RelayAuth auth{ QStringLiteral("device-1"), QStringLiteral("secret-1") };
    const ListAttachmentsResult result =
        source.listAttachments(serverBaseUrl, auth, QStringLiteral("Inbox"), QStringLiteral("42"));

    QVERIFY(!result.error.has_value());
    QCOMPARE(result.attachments.size(), 2);
    QCOMPARE(result.attachments.at(0).index, 0);
    QCOMPARE(result.attachments.at(0).name, QStringLiteral("report.pdf"));
    QCOMPARE(result.attachments.at(0).mimeType, QStringLiteral("application/pdf"));
    QCOMPARE(result.attachments.at(0).size, 1024);
    QCOMPARE(result.attachments.at(1).index, 1);
    QCOMPARE(result.attachments.at(1).name, QStringLiteral("image.png"));
    QCOMPARE(result.attachments.at(1).mimeType, QStringLiteral("image/png"));
    QCOMPARE(result.attachments.at(1).size, 2048);

    const QByteArray request = fake.receivedRequest();
    QVERIFY(request.contains("GET /api/mail/attachments?"));
    QVERIFY(request.contains("mailbox=Inbox"));
    QVERIFY(request.contains("messageId=42"));
    QVERIFY(request.contains("X-Kypost-Device-Id: device-1"));
    QVERIFY(request.contains("X-Kypost-Device-Secret: secret-1"));
    QVERIFY(!request.contains("device=device-1"));
    QVERIFY(!request.contains("secret=secret-1"));
}

void RelayMailSourceTest::downloadAttachmentReturnsRawBytesAndParsesFilenameFromContentDisposition()
{
    // Every byte value 0-255, to confirm the raw body survives the round
    // trip byte-for-byte rather than being treated as/mangled like text.
    QByteArray rawBytes;
    for (int i = 0; i < 256; ++i)
        rawBytes.append(static_cast<char>(i));

    // Hand-written Content-Disposition header matching Go's
    // mime.FormatMediaType output shape exactly, including backslash-escaped
    // quotes inside the quoted filename, to exercise the escape-aware parser.
    FakeRelayServer fake(httpResponse(200, "OK", rawBytes, "application/pdf",
                                       { { "Content-Disposition", R"(attachment; filename="My File \"v2\".pdf")" } }));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);

    const QUrl serverBaseUrl(QStringLiteral("http://127.0.0.1:%1").arg(fake.port()));
    const RelayAuth auth{ QStringLiteral("device-1"), QStringLiteral("secret-1") };
    const DownloadAttachmentResult result =
        source.downloadAttachment(serverBaseUrl, auth, QStringLiteral("Inbox"), QStringLiteral("42"), 0);

    QVERIFY(!result.error.has_value());
    QCOMPARE(result.data, rawBytes);
    QCOMPARE(result.mimeType, QStringLiteral("application/pdf"));
    QCOMPARE(result.filename, QStringLiteral("My File \"v2\".pdf"));

    const QByteArray request = fake.receivedRequest();
    QVERIFY(request.contains("GET /api/mail/attachment?"));
    QVERIFY(request.contains("mailbox=Inbox"));
    QVERIFY(request.contains("messageId=42"));
    QVERIFY(request.contains("index=0"));
    QVERIFY(request.contains("X-Kypost-Device-Id: device-1"));
    QVERIFY(request.contains("X-Kypost-Device-Secret: secret-1"));
    QVERIFY(!request.contains("device=device-1"));
    QVERIFY(!request.contains("secret=secret-1"));
}

void RelayMailSourceTest::downloadAttachmentMapsNotFoundFrom404()
{
    FakeRelayServer fake(
        httpResponse(404, "Not Found", "attachment not found\n", "text/plain; charset=utf-8"));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);

    const QUrl serverBaseUrl(QStringLiteral("http://127.0.0.1:%1").arg(fake.port()));
    const RelayAuth auth{ QStringLiteral("device-1"), QStringLiteral("secret-1") };
    const DownloadAttachmentResult result =
        source.downloadAttachment(serverBaseUrl, auth, QStringLiteral("Inbox"), QStringLiteral("42"), 99);

    QVERIFY(result.error.has_value());
    QCOMPARE(*result.error, NetworkError::Server);
    QVERIFY(result.data.isEmpty());
}

void RelayMailSourceTest::sendMailPutsPgpFlagsInTheRequestBody()
{
    FakeRelayServer fake(httpResponse(200, "OK", R"({"ok":true,"sentSaved":true,"warning":""})"));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);

    const QUrl serverBaseUrl(QStringLiteral("http://127.0.0.1:%1").arg(fake.port()));
    const RelayAuth auth{ QStringLiteral("device-1"), QStringLiteral("secret-1") };
    source.sendMail(serverBaseUrl, auth, QStringLiteral("a@example.com"), QString(), QString(),
                    QStringLiteral("Hi"), QStringLiteral("Body"), QStringLiteral("plain"), {},
                    /*sign=*/true, /*encrypt=*/true, /*allowPickupFallback=*/true);

    const QJsonObject body = fake.receivedJsonBody();
    QCOMPARE(body.value(QStringLiteral("sign")).toBool(), true);
    QCOMPARE(body.value(QStringLiteral("encrypt")).toBool(), true);
    QCOMPARE(body.value(QStringLiteral("allowPickupFallback")).toBool(), true);
}

void RelayMailSourceTest::sendMailParsesKeylessRecipient409()
{
    // Nothing was delivered: the server refuses before any SMTP, which is why
    // re-sending with the opt-in cannot duplicate the message.
    FakeRelayServer fake(httpResponse(
        409, "Conflict",
        R"({"error":"some recipients have no usable PGP key","keylessRecipients":["bob@example.com"],)"
        R"("pickupFallbackAvailable":true})"));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);

    const QUrl serverBaseUrl(QStringLiteral("http://127.0.0.1:%1").arg(fake.port()));
    const RelayAuth auth{ QStringLiteral("device-1"), QStringLiteral("secret-1") };
    const SendMailResult result =
        source.sendMail(serverBaseUrl, auth, QStringLiteral("bob@example.com"), QString(), QString(),
                        QStringLiteral("Hi"), QStringLiteral("Body"), QStringLiteral("plain"), {},
                        /*sign=*/false, /*encrypt=*/true, /*allowPickupFallback=*/false);

    QCOMPARE(result.ok, false);
    QCOMPARE(result.pickupFallbackNeeded, true);
    QCOMPARE(result.keylessRecipients, QStringList{ QStringLiteral("bob@example.com") });
    // The two PGP refusals share a status code and are told apart by field.
    QCOMPARE(result.clientSideNeeded, false);
}

void RelayMailSourceTest::sendMailClientSideNeeded409IsNotAKeylessRefusal()
{
    FakeRelayServer fake(httpResponse(
        409, "Conflict",
        R"({"error":"this account's PGP key is end-to-end protected","clientSideNeeded":true})"));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);

    const QUrl serverBaseUrl(QStringLiteral("http://127.0.0.1:%1").arg(fake.port()));
    const RelayAuth auth{ QStringLiteral("device-1"), QStringLiteral("secret-1") };
    const SendMailResult result =
        source.sendMail(serverBaseUrl, auth, QStringLiteral("bob@example.com"), QString(), QString(),
                        QStringLiteral("Hi"), QStringLiteral("Body"), QStringLiteral("plain"), {},
                        /*sign=*/false, /*encrypt=*/true, /*allowPickupFallback=*/false);

    QCOMPARE(result.clientSideNeeded, true);
    QCOMPARE(result.pickupFallbackNeeded, false);
    QVERIFY(result.keylessRecipients.isEmpty());
}

// Pins the PRECEDENCE, which currently rests on a single unpinned
// `if (!out.clientSideNeeded)` guard. The live server refuses a client-custody
// account (server.go:1207) before it reaches the keyless gate (:1272), so the
// two fields never arrive together today -- but a refactor that inverted that
// guard would turn an unrecoverable refusal into a plaintext-link confirmation
// dialog, i.e. it would ask the user to consent to a server-side plaintext
// downgrade for a send that cannot succeed either way. clientSideNeeded wins.
void RelayMailSourceTest::sendMailDualField409ReportsTheUnrecoverableRefusal()
{
    FakeRelayServer fake(httpResponse(
        409, "Conflict",
        R"({"error":"this account's PGP key is end-to-end protected","clientSideNeeded":true,)"
        R"("keylessRecipients":["bob@example.com"],"pickupFallbackAvailable":true})"));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);

    const QUrl serverBaseUrl(QStringLiteral("http://127.0.0.1:%1").arg(fake.port()));
    const RelayAuth auth{ QStringLiteral("device-1"), QStringLiteral("secret-1") };
    const SendMailResult result =
        source.sendMail(serverBaseUrl, auth, QStringLiteral("bob@example.com"), QString(), QString(),
                        QStringLiteral("Hi"), QStringLiteral("Body"), QStringLiteral("plain"), {},
                        /*sign=*/false, /*encrypt=*/true, /*allowPickupFallback=*/false);

    QCOMPARE(result.ok, false);
    QCOMPARE(result.clientSideNeeded, true);
    QCOMPARE(result.pickupFallbackNeeded, false);
    // Not even parsed: nothing downstream should be able to name these
    // addresses in a confirmation that must never be offered.
    QVERIFY(result.keylessRecipients.isEmpty());
}

void RelayMailSourceTest::sendMail409WithNeitherPgpFieldStaysAGenericError()
{
    // A 409 that is neither PGP refusal must not inherit PGP wording or set
    // either flag -- otherwise an unrelated future conflict would open the
    // plaintext-link dialog.
    FakeRelayServer fake(httpResponse(409, "Conflict", R"({"error":"something else entirely"})"));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);

    const QUrl serverBaseUrl(QStringLiteral("http://127.0.0.1:%1").arg(fake.port()));
    const RelayAuth auth{ QStringLiteral("device-1"), QStringLiteral("secret-1") };
    const SendMailResult result =
        source.sendMail(serverBaseUrl, auth, QStringLiteral("bob@example.com"), QString(), QString(),
                        QStringLiteral("Hi"), QStringLiteral("Body"), QStringLiteral("plain"), {},
                        /*sign=*/false, /*encrypt=*/true, /*allowPickupFallback=*/false);

    QCOMPARE(result.pickupFallbackNeeded, false);
    QCOMPARE(result.clientSideNeeded, false);
    QCOMPARE(result.detail, QStringLiteral("something else entirely"));
}

void RelayMailSourceTest::sendMailMalformed409BodyDoesNotCrashTheDecode()
{
    FakeRelayServer fake(httpResponse(409, "Conflict", "not json at all", "text/plain"));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);

    const QUrl serverBaseUrl(QStringLiteral("http://127.0.0.1:%1").arg(fake.port()));
    const RelayAuth auth{ QStringLiteral("device-1"), QStringLiteral("secret-1") };
    const SendMailResult result =
        source.sendMail(serverBaseUrl, auth, QStringLiteral("bob@example.com"), QString(), QString(),
                        QStringLiteral("Hi"), QStringLiteral("Body"), QStringLiteral("plain"), {},
                        /*sign=*/false, /*encrypt=*/true, /*allowPickupFallback=*/false);

    QCOMPARE(result.pickupFallbackNeeded, false);
    QCOMPARE(result.clientSideNeeded, false);
    QVERIFY(!result.detail.isEmpty());
}

void RelayMailSourceTest::searchMailSendsTheRelayContractAndMapsMetadataRows()
{
    const QByteArray body = R"({"results":[
        {"messageId":"7","sender":"a@example.com","sentTo":"b@example.com","subject":"Invoice",
         "status":"unread","atUtc":"2026-01-02T03:04:05Z","label":"Work","keywords":["Work"],"body":"ignored"},
        {"sender":"no id"}
    ]})";
    FakeRelayServer fake(httpResponse(200, "OK", body));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);
    const QUrl base(QStringLiteral("http://127.0.0.1:%1").arg(fake.port()));
    const RelayAuth auth{ QStringLiteral("device-1"), QStringLiteral("secret-1") };

    const MailSearchResult result = source.searchMail(base, auth, QStringLiteral("C++ 100%25 a&b=c"), QStringLiteral("Archive/2025"));

    QVERIFY(!result.error.has_value());
    QCOMPARE(result.results.size(), 1);
    const Email& hit = result.results.at(0);
    QCOMPARE(hit.messageId, QStringLiteral("7"));
    QCOMPARE(hit.folder, QStringLiteral("Archive/2025"));
    QCOMPARE(hit.subject, QStringLiteral("Invoice"));
    QCOMPARE(hit.status, QStringLiteral("unread"));
    QCOMPARE(hit.keywords, QStringList{ QStringLiteral("Work") });
    QVERIFY(!hit.body.has_value()); // search never carries a body; absent means "not fetched"

    const QByteArray request = fake.receivedRequest();
    QVERIFY2(request.startsWith("GET /api/mail/search?"), request.constData());
    // Go decodes "+" as a space and "%25" as "%"; both must reach it literally.
    QVERIFY2(request.contains("q=C%2B%2B%20100%2525%20a%26b%3Dc&"), request.constData());
    QVERIFY(request.contains("field=all"));
    QVERIFY(request.contains("mailbox=Archive%2F2025"));
    QVERIFY(request.contains("limit=200"));
    QVERIFY(request.contains("X-Kypost-Device-Id: device-1"));
    QVERIFY(request.contains("X-Kypost-Device-Secret: secret-1"));
    QVERIFY(!request.contains("secret-1&"));
}

void RelayMailSourceTest::searchMailDropsRowsPastTheCap()
{
    QJsonArray rows;
    for (int i = 0; i < RelayMailSource::kMaxSearchResults + 5; ++i)
        rows.append(QJsonObject{ { QStringLiteral("messageId"), QString::number(i) } });
    FakeRelayServer fake(httpResponse(200, "OK", QJsonDocument(QJsonObject{ { QStringLiteral("results"), rows } }).toJson()));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);
    const MailSearchResult result = source.searchMail(QUrl(QStringLiteral("http://127.0.0.1:%1").arg(fake.port())),
                                                      {}, QStringLiteral("x"), QStringLiteral("INBOX"));
    QVERIFY(!result.error.has_value());
    QCOMPARE(result.results.size(), RelayMailSource::kMaxSearchResults);
}

void RelayMailSourceTest::searchMailRefusesUnsearchableQueriesWithoutARequest()
{
    FakeRelayServer fake(httpResponse(200, "OK", R"({"results":[]})"));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);
    const QUrl base(QStringLiteral("http://127.0.0.1:%1").arg(fake.port()));

    QVERIFY(!RelayMailSource::isSearchableQuery(QStringLiteral("  \t")));
    QVERIFY(RelayMailSource::isSearchableQuery(QStringLiteral("a")));
    // The relay's limit is bytes, so a multi-byte character counts for more.
    QVERIFY(RelayMailSource::isSearchableQuery(QString(RelayMailSource::kMaxSearchQueryBytes, QLatin1Char('a'))));
    QVERIFY(!RelayMailSource::isSearchableQuery(QString(RelayMailSource::kMaxSearchQueryBytes / 2 + 1, QChar(0x00e9))));

    QVERIFY(source.searchMail(base, {}, QStringLiteral(" "), QStringLiteral("INBOX")).error.has_value());
    QVERIFY(source.searchMail(base, {}, QString(RelayMailSource::kMaxSearchQueryBytes + 1, QLatin1Char('a')),
                              QStringLiteral("INBOX")).error.has_value());
    QCOMPARE(fake.connectionCount(), 0);
}

void RelayMailSourceTest::searchMailPassesHttpErrorsThrough()
{
    FakeRelayServer fake(httpResponse(503, "Service Unavailable", "search failed: imap down", "text/plain"));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);
    const MailSearchResult result = source.searchMail(QUrl(QStringLiteral("http://127.0.0.1:%1").arg(fake.port())),
                                                      {}, QStringLiteral("x"), QStringLiteral("INBOX"));
    QCOMPARE(result.error, std::optional<NetworkError>(NetworkError::ServiceUnavailable));
    QVERIFY(result.results.isEmpty());
    QVERIFY(!result.detail.isEmpty());
}

void RelayMailSourceTest::fetchBodyReturnsBodyAndMode()
{
    FakeRelayServer fake(httpResponse(200, "OK", R"({"body":"<p>hi</p>","bodyMode":"html"})"));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);
    const MailBodyResult result = source.fetchBody(QUrl(QStringLiteral("http://127.0.0.1:%1").arg(fake.port())),
                                                   { QStringLiteral("d"), QStringLiteral("s") },
                                                   QStringLiteral("INBOX"), QStringLiteral("9"));
    QVERIFY(!result.error.has_value());
    QCOMPARE(result.body, QStringLiteral("<p>hi</p>"));
    QCOMPARE(result.bodyMode, QStringLiteral("html"));
    const QByteArray request = fake.receivedRequest();
    QVERIFY2(request.startsWith("GET /api/mail/body?"), request.constData());
    QVERIFY(request.contains("mailbox=INBOX"));
    QVERIFY(request.contains("messageId=9"));
    QVERIFY(request.contains("X-Kypost-Device-Secret: s"));
}

namespace {

const RelayAuth kAuth{ QStringLiteral("device-1"), QStringLiteral("secret-1") };

QUrl baseOf(const FakeRelayServer& fake)
{
    return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(fake.port()));
}

SendMailResult sendAt(const FakeRelayServer& fake, RelayMailSource& source, const QDateTime& at)
{
    return source.sendMail(baseOf(fake), kAuth, QStringLiteral("a@example.com"), QString(), QString(),
                           QStringLiteral("Hi"), QStringLiteral("Body"), QStringLiteral("html"), {},
                           false, false, false, {}, {}, at);
}

} // namespace

// parseSendAt reads RFC 3339; the relay compares instants, so the zone the user
// picked in must come out as the same moment in UTC, without milliseconds.
void RelayMailSourceTest::sendAtTravelsAsUtcWholeSeconds()
{
    const QDateTime berlinSummer(QDate(2026, 10, 17), QTime(14, 30, 5, 678), QTimeZone("Europe/Berlin"));
    QCOMPARE(sendAtWireValue(berlinSummer), QStringLiteral("2026-10-17T12:30:05Z"));
    const QDateTime berlinWinter(QDate(2026, 11, 2), QTime(8, 0), QTimeZone("Europe/Berlin"));
    QCOMPARE(sendAtWireValue(berlinWinter), QStringLiteral("2026-11-02T07:00:00Z"));
    const QDateTime newYork(QDate(2026, 12, 31), QTime(22, 0), QTimeZone("America/New_York"));
    QCOMPARE(sendAtWireValue(newYork), QStringLiteral("2027-01-01T03:00:00Z"));
}

void RelayMailSourceTest::sendMailCarriesSendAtAndReadsTheHold()
{
    // scheduleSend's 202: no ok field.
    FakeRelayServer fake(httpResponse(202, "Accepted",
                                      R"({"scheduled":true,"id":"0b6e","sendAt":"2026-10-18T06:00:00Z"})"));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);

    const QDateTime at(QDate(2026, 10, 18), QTime(8, 0), QTimeZone("Europe/Berlin"));
    const SendMailResult result = sendAt(fake, source, at);

    QCOMPARE(fake.receivedJsonBody().value(QStringLiteral("sendAt")).toString(),
             QStringLiteral("2026-10-18T06:00:00Z"));
    QVERIFY(result.ok);
    QCOMPARE(result.scheduledAt, at);
}

void RelayMailSourceTest::sendMailWithoutSendAtSendsNone()
{
    FakeRelayServer fake(httpResponse(200, "OK", R"({"ok":true,"sentSaved":true,"warning":""})"));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);

    const SendMailResult result = sendAt(fake, source, QDateTime());

    QVERIFY(result.ok);
    QVERIFY(!result.scheduledAt.isValid());
    QVERIFY(!fake.receivedJsonBody().contains(QStringLiteral("sendAt")));
}

// A hold nobody asked for is not a send; neither is a 202 that names no time.
void RelayMailSourceTest::anUnaskedFor202IsNotASend()
{
    {
        FakeRelayServer fake(httpResponse(202, "Accepted",
                                          R"({"scheduled":true,"id":"1","sendAt":"2026-10-18T06:00:00Z"})"));
        QNetworkAccessManager manager;
        HttpClient http(manager);
        RelayMailSource source(http);
        const SendMailResult result = sendAt(fake, source, QDateTime());
        QVERIFY(!result.ok);
        QVERIFY(!result.scheduledAt.isValid());
    }
    {
        FakeRelayServer fake(httpResponse(202, "Accepted", R"({"scheduled":true})"));
        QNetworkAccessManager manager;
        HttpClient http(manager);
        RelayMailSource source(http);
        const SendMailResult result = sendAt(fake, source, QDateTime::currentDateTime().addDays(1));
        QVERIFY(!result.ok);
    }
}

void RelayMailSourceTest::aPlainTextRefusalIsTheDetail()
{
    FakeRelayServer fake(httpResponse(400, "Bad Request",
                                      "sendAt must be between one minute and 366 days from now\n", "text/plain"));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);

    const SendMailResult result = sendAt(fake, source, QDateTime::currentDateTime().addDays(400));

    QVERIFY(!result.ok);
    QCOMPARE(result.detail, QStringLiteral("sendAt must be between one minute and 366 days from now"));
}

void RelayMailSourceTest::listScheduledMapsRows()
{
    FakeRelayServer fake(httpResponse(200, "OK", R"({"scheduled":[)"
        R"({"id":"a-1","status":"scheduled","reason":"","sendAt":"2026-10-18T06:00:00Z","subject":"Hi","to":["x@example.com","y@example.com"]},)"
        R"({"id":"b-2","status":"returned","reason":"canceled","sendAt":"2026-10-17T06:00:00Z","subject":"","to":null}]})"));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);

    const ScheduledListResult result = source.listScheduled(baseOf(fake), kAuth);

    QVERIFY(!result.error.has_value());
    QVERIFY(fake.receivedRequest().startsWith("GET /api/scheduled "));
    QVERIFY(fake.receivedRequest().contains("X-Kypost-Device-Secret: secret-1"));
    QCOMPARE(result.items.size(), 2);
    const ScheduledSend first{ QStringLiteral("a-1"), QStringLiteral("scheduled"), QString(),
                               QDateTime(QDate(2026, 10, 18), QTime(6, 0), QTimeZone::UTC), QStringLiteral("Hi"),
                               { QStringLiteral("x@example.com"), QStringLiteral("y@example.com") } };
    QCOMPARE(result.items.at(0), first);
    QCOMPARE(result.items.at(1).reason, QStringLiteral("canceled"));
    QVERIFY(result.items.at(1).to.isEmpty());
}

void RelayMailSourceTest::listScheduledWithoutTheRouteIsUnsupported()
{
    FakeRelayServer fake(httpResponse(404, "Not Found", "404 page not found\n", "text/plain"));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);

    const ScheduledListResult result = source.listScheduled(baseOf(fake), kAuth);

    QVERIFY(result.error.has_value());
    QVERIFY(result.unsupported);
}

void RelayMailSourceTest::cancelScheduledPostsToThatId()
{
    FakeRelayServer fake(httpResponse(200, "OK", R"({"ok":true,"status":"returned"})"));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);

    const CancelScheduledResult result =
        source.cancelScheduled(baseOf(fake), kAuth, QStringLiteral("6f1c2d3e-0000-4000-8000-000000000001"));

    QVERIFY(fake.receivedRequest().startsWith("POST /api/scheduled/6f1c2d3e-0000-4000-8000-000000000001/cancel "));
    QVERIFY(result.ok);
    QVERIFY(result.returned);
}

void RelayMailSourceTest::cancelScheduledTellsPendingFromReturned()
{
    FakeRelayServer fake(httpResponse(202, "Accepted",
        R"({"ok":true,"status":"returning","warning":"canceled; it will be saved to Drafts shortly"})"));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);

    const CancelScheduledResult result = source.cancelScheduled(baseOf(fake), kAuth, QStringLiteral("abc"));

    QVERIFY(result.ok);
    QVERIFY(!result.returned);
}

void RelayMailSourceTest::cancelScheduledNamesGoneAndClaimed()
{
    {
        FakeRelayServer fake(httpResponse(404, "Not Found", "scheduled message not found\n", "text/plain"));
        QNetworkAccessManager manager;
        HttpClient http(manager);
        const CancelScheduledResult result =
            RelayMailSource(http).cancelScheduled(baseOf(fake), kAuth, QStringLiteral("abc"));
        QVERIFY(!result.ok);
        QVERIFY(result.gone);
        QVERIFY(!result.claimed);
    }
    {
        FakeRelayServer fake(httpResponse(409, "Conflict",
            R"({"error":"this message is no longer waiting and cannot be canceled","status":"sending"})"));
        QNetworkAccessManager manager;
        HttpClient http(manager);
        const CancelScheduledResult result =
            RelayMailSource(http).cancelScheduled(baseOf(fake), kAuth, QStringLiteral("abc"));
        QVERIFY(!result.ok);
        QVERIFY(result.claimed);
        QVERIFY(!result.gone);
    }
}

void RelayMailSourceTest::cancelScheduledRefusesAnIdThatIsNotTheRelays()
{
    FakeRelayServer fake(httpResponse(200, "OK", R"({"ok":true,"status":"returned"})"));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);

    for (const QString& id : { QStringLiteral(".."), QStringLiteral("a/../../api/mail/send"), QString(),
                               QStringLiteral("a?x=1"), QString(65, QLatin1Char('a')) }) {
        const CancelScheduledResult result = source.cancelScheduled(baseOf(fake), kAuth, id);
        QVERIFY(!result.ok);
        QVERIFY(result.gone);
    }
    QCOMPARE(fake.connectionCount(), 0);
}

// Android #188's wire contract: present on a reply, absent otherwise, so an
// older relay sees the request it always did. Drafts have no such arguments.
void RelayMailSourceTest::sendMailNamesTheAnsweredMessageOnlyForAReply()
{
    FakeRelayServer fake(httpResponse(200, "OK", R"({"ok":true,"sentSaved":true,"warning":""})"));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);
    const QUrl serverBaseUrl(QStringLiteral("http://127.0.0.1:%1").arg(fake.port()));

    QVERIFY(source.sendMail(serverBaseUrl, {}, QStringLiteral("a@example.com"), {}, {}, QStringLiteral("Re: Hi"),
                            QStringLiteral("Body"), QStringLiteral("html"), {}, false, false, false,
                            QStringLiteral("42"), QStringLiteral("Archive"))
                .ok);
    QVERIFY(source.sendMail(serverBaseUrl, {}, QStringLiteral("a@example.com"), {}, {}, QStringLiteral("Hi"),
                            QStringLiteral("Body"), QStringLiteral("html"), {})
                .ok);

    QCOMPARE(fake.receivedRequests().size(), 2);
    const auto bodyOf = [](const QByteArray& request) {
        return QJsonDocument::fromJson(request.mid(request.indexOf("\r\n\r\n") + 4)).object();
    };
    const QJsonObject reply = bodyOf(fake.receivedRequests().at(0));
    QCOMPARE(reply.value(QStringLiteral("replyToMessageId")).toString(), QStringLiteral("42"));
    QCOMPARE(reply.value(QStringLiteral("replyToMailbox")).toString(), QStringLiteral("Archive"));
    const QJsonObject fresh = bodyOf(fake.receivedRequests().at(1));
    QVERIFY(!fresh.contains(QStringLiteral("replyToMessageId")));
    QVERIFY(!fresh.contains(QStringLiteral("replyToMailbox")));
}

void RelayMailSourceTest::sendMailRecognisesOnlyTheRelaysThreadingRefusals_data()
{
    QTest::addColumn<int>("status");
    QTest::addColumn<QByteArray>("body");
    QTest::addColumn<bool>("isReply");
    QTest::addColumn<bool>("refused");
    // server_mail_send.go replyThreading, http.Error adds the newline.
    QTest::newRow("bad-id") << 400 << QByteArray("invalid replyToMessageId; refresh the mailbox\n") << true << true;
    QTest::newRow("bad-mailbox") << 400 << QByteArray("invalid replyToMailbox\n") << true << true;
    QTest::newRow("not-found") << 404 << QByteArray("the message being replied to was not found; nothing was sent\n")
                               << true << true;
    QTest::newRow("unreadable") << 502 << QByteArray("could not read the message being replied to; nothing was sent\n")
                                << true << true;
    // A 502 after delivery: offering to send again could send twice.
    QTest::newRow("smtp-failure") << 502 << QByteArray("failed to send email: 554 rejected\n") << true << false;
    QTest::newRow("smtp-quoting-the-words")
        << 502 << QByteArray("failed to send email: 550 the message being replied to was not found; nothing was sent\n")
        << true << false;
    QTest::newRow("wrong-status") << 502 << QByteArray("the message being replied to was not found; nothing was sent\n")
                                  << true << false;
    QTest::newRow("not-a-reply") << 404 << QByteArray("the message being replied to was not found; nothing was sent\n")
                                 << false << false;
}

void RelayMailSourceTest::sendMailRecognisesOnlyTheRelaysThreadingRefusals()
{
    QFETCH(int, status);
    QFETCH(QByteArray, body);
    QFETCH(bool, isReply);
    QFETCH(bool, refused);

    FakeRelayServer fake(httpResponse(status, "Error", body));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    RelayMailSource source(http);
    const SendMailResult result = source.sendMail(
        QUrl(QStringLiteral("http://127.0.0.1:%1").arg(fake.port())), {}, QStringLiteral("a@example.com"), {}, {},
        QStringLiteral("Re: Hi"), QStringLiteral("Body"), QStringLiteral("html"), {}, false, false, false,
        isReply ? QStringLiteral("42") : QString(), isReply ? QStringLiteral("INBOX") : QString());

    QVERIFY(!result.ok);
    QCOMPARE(result.replyThreadingRefused, refused);
    if (refused)
        QCOMPARE(result.detail, QString::fromUtf8(body.trimmed()));
}

QTEST_GUILESS_MAIN(RelayMailSourceTest)
#include "RelayMailSourceTest.moc"
