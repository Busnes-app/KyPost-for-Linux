#include "net/PushNotificationClient.h"

#include "net/HttpClient.h"
#include "net/RelayAuth.h"

#include "FakeRelayServer.h"

#include <QNetworkAccessManager>
#include <QTest>

class PushNotificationClientTest : public QObject
{
    Q_OBJECT

private slots:
    void successMapsItemsAndSplitsMultiValueKeywords();
    void firstPullOmitsAfterQueryParam();
    void subsequentPullSendsAfterQueryParam();
    void successWithZeroNotificationsIsDistinctFromFailure();
    void unauthorizedFrom401PassesErrorThrough();
    void signInChallengesAreRoutedAndMalformedOnesDropped();
};

void PushNotificationClientTest::successMapsItemsAndSplitsMultiValueKeywords()
{
    const QByteArray body = R"({"deliveryMode":"pull","cursor":42,"notifications":[)"
                             R"({"seq":1,"title":"Alice","body":"Hello there","createdAt":"2026-07-01T00:00:00Z",)"
                             R"("data":{"messageId":"msg-1","sender":"a@example.com","subject":"Hello",)"
                             R"("senderName":"Alice","emailSubject":"Hello there",)"
                             R"("Keywords":"work,urgent,follow-up","title":"Alice","body":"Hello there","url":"/read"}},)"
                             R"({"seq":2,"title":"Bob","body":"Reminder","createdAt":"2026-07-02T00:00:00Z",)"
                             R"("data":{"messageId":"msg-2","sender":"b@example.com","subject":"Re: Hello",)"
                             R"("senderName":"Bob","emailSubject":"Re: Hello","Keywords":"personal","title":"Bob",)"
                             R"("body":"Reminder","url":"/read2"}}]})";
    FakeRelayServer fake(httpResponse(200, "OK", body));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    PushNotificationClient client(http);

    const QUrl endpoint(QStringLiteral("http://127.0.0.1:%1/api/notifications/native/pull").arg(fake.port()));
    const RelayAuth auth{ QStringLiteral("device-1"), QStringLiteral("secret-1") };
    const PullResult result = client.pull(endpoint, auth, 0);

    QVERIFY(!result.error.has_value());
    QCOMPARE(result.deliveryMode, QStringLiteral("pull"));
    QCOMPARE(result.cursor, qint64(42));
    QCOMPARE(result.notifications.size(), 2);

    const PullNotificationItem& first = result.notifications.at(0);
    QCOMPARE(first.seq, qint64(1));
    QCOMPARE(first.notification.messageId, QStringLiteral("msg-1"));
    QCOMPARE(first.notification.sender, QStringLiteral("a@example.com"));
    QCOMPARE(first.notification.subject, QStringLiteral("Hello"));
    QCOMPARE(first.notification.senderName, QStringLiteral("Alice"));
    QCOMPARE(first.notification.emailSubject, QStringLiteral("Hello there"));
    QCOMPARE(first.notification.keywords,
             QStringList({ QStringLiteral("work"), QStringLiteral("urgent"), QStringLiteral("follow-up") }));
    QCOMPARE(first.notification.title, QStringLiteral("Alice"));
    QCOMPARE(first.notification.body, QStringLiteral("Hello there"));
    QCOMPARE(first.notification.url, QStringLiteral("/read"));

    const PullNotificationItem& second = result.notifications.at(1);
    QCOMPARE(second.seq, qint64(2));
    QCOMPARE(second.notification.keywords, QStringList({ QStringLiteral("personal") }));
}

void PushNotificationClientTest::firstPullOmitsAfterQueryParam()
{
    FakeRelayServer fake(httpResponse(200, "OK", R"({"deliveryMode":"pull","cursor":0,"notifications":[]})"));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    PushNotificationClient client(http);

    const QUrl endpoint(QStringLiteral("http://127.0.0.1:%1/api/notifications/native/pull").arg(fake.port()));
    const RelayAuth auth{ QStringLiteral("device-1"), QStringLiteral("secret-1") };
    client.pull(endpoint, auth, 0);

    const QByteArray request = fake.receivedRequest();
    QVERIFY(request.contains("X-Kypost-Device-Id: device-1"));
    QVERIFY(request.contains("X-Kypost-Device-Secret: secret-1"));
    QVERIFY(!request.contains("device=device-1"));
    QVERIFY(!request.contains("secret=secret-1"));
    QVERIFY(!request.contains("after="));
}

void PushNotificationClientTest::subsequentPullSendsAfterQueryParam()
{
    FakeRelayServer fake(httpResponse(200, "OK", R"({"deliveryMode":"pull","cursor":43,"notifications":[]})"));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    PushNotificationClient client(http);

    const QUrl endpoint(QStringLiteral("http://127.0.0.1:%1/api/notifications/native/pull").arg(fake.port()));
    const RelayAuth auth{ QStringLiteral("device-1"), QStringLiteral("secret-1") };
    client.pull(endpoint, auth, 42);

    QVERIFY(fake.receivedRequest().contains("after=42"));
}

void PushNotificationClientTest::successWithZeroNotificationsIsDistinctFromFailure()
{
    FakeRelayServer fake(httpResponse(200, "OK", R"({"deliveryMode":"push","cursor":5,"notifications":[]})"));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    PushNotificationClient client(http);

    const QUrl endpoint(QStringLiteral("http://127.0.0.1:%1/api/notifications/native/pull").arg(fake.port()));
    const RelayAuth auth{ QStringLiteral("device-1"), QStringLiteral("secret-1") };
    const PullResult result = client.pull(endpoint, auth, 0);

    QVERIFY(!result.error.has_value());
    QVERIFY(result.notifications.isEmpty());
    QCOMPARE(result.cursor, qint64(5));
}

void PushNotificationClientTest::unauthorizedFrom401PassesErrorThrough()
{
    FakeRelayServer fake(httpResponse(401, "Unauthorized", "{}"));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    PushNotificationClient client(http);

    const QUrl endpoint(QStringLiteral("http://127.0.0.1:%1/api/notifications/native/pull").arg(fake.port()));
    const RelayAuth auth{ QStringLiteral("device-1"), QStringLiteral("secret-1") };
    const PullResult result = client.pull(endpoint, auth, 0);

    QVERIFY(result.error.has_value());
    QCOMPARE(*result.error, NetworkError::Unauthorized);
    QVERIFY(result.notifications.isEmpty());
}

// The pull queue carries the same data map as the push (PullNotification.Data).
void PushNotificationClientTest::signInChallengesAreRoutedAndMalformedOnesDropped()
{
    const QByteArray body = R"({"deliveryMode":"pull","cursor":3,"notifications":[)"
                             R"({"seq":1,"title":"Approve sign-in","body":"Tap to approve",)"
                             R"("data":{"type":"mfa_challenge","challengeId":"0123456789abcdef0123456789abcdef0123456789abcdef",)"
                             R"("issuedAt":"1760000000000","matchDigits":"42","decoyDigits":"07,93"}},)"
                             R"({"seq":2,"title":"Approve sign-in","body":"Tap to approve",)"
                             R"("data":{"type":"mfa_challenge","challengeId":"bad"}},)"
                             R"({"seq":3,"title":"Alice","body":"Hi","data":{"messageId":"msg-1"}}]})";
    FakeRelayServer fake(httpResponse(200, "OK", body));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    PushNotificationClient client(http);

    const QUrl endpoint(QStringLiteral("http://127.0.0.1:%1/api/notifications/native/pull").arg(fake.port()));
    const PullResult result = client.pull(endpoint, RelayAuth{ QStringLiteral("d"), QStringLiteral("s") }, 0);

    QVERIFY(!result.error.has_value());
    QCOMPARE(result.cursor, qint64(3));
    QCOMPARE(result.notifications.size(), 2);
    const PushNotification& challenge = result.notifications.at(0).notification;
    QVERIFY(challenge.mfaChallenge.has_value());
    QCOMPARE(challenge.mfaChallenge->decoyDigits, QStringList({ QStringLiteral("07"), QStringLiteral("93") }));
    QVERIFY(challenge.title.isEmpty());
    QCOMPARE(result.notifications.at(1).notification.messageId, QStringLiteral("msg-1"));
    QVERIFY(!result.notifications.at(1).notification.mfaChallenge.has_value());
}

QTEST_GUILESS_MAIN(PushNotificationClientTest)
#include "PushNotificationClientTest.moc"
