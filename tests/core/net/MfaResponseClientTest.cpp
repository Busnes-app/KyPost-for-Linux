#include "net/MfaResponseClient.h"

#include "net/HttpClient.h"

#include "FakeRelayServer.h"

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QTest>

// Qt 5 needs this declared to use it as test data; Qt 6 does it implicitly.
Q_DECLARE_METATYPE(MfaResponseOutcome)

// Contract: kypost-server push_mfa_handlers.go handlePushRespond.
class MfaResponseClientTest : public QObject
{
    Q_OBJECT

private slots:
    void approveSendsDeviceHeadersAndTheTappedDigits();
    void denySendsNoDigits();
    void statusCodesMapToOutcomes_data();
    void statusCodesMapToOutcomes();
};

namespace {

MfaResponseResult respondAgainst(FakeRelayServer& fake, bool approve, const QString& digits)
{
    QNetworkAccessManager manager;
    HttpClient http(manager);
    const RelayEndpoint endpoint{ QUrl(QStringLiteral("http://127.0.0.1:%1").arg(fake.port())),
                                  { QStringLiteral("device-1"), QStringLiteral("secret-1") } };
    return MfaResponseClient(http).respond(endpoint, QStringLiteral("chal-1"), approve, digits);
}

} // namespace

void MfaResponseClientTest::approveSendsDeviceHeadersAndTheTappedDigits()
{
    FakeRelayServer fake(httpResponse(200, "OK", R"({"ok":true,"status":"approved"})"));
    const MfaResponseResult result = respondAgainst(fake, true, QStringLiteral("42"));

    QCOMPARE(result.outcome, MfaResponseOutcome::Success);
    QCOMPARE(result.status, QStringLiteral("approved"));
    const QByteArray request = fake.receivedRequest();
    QVERIFY(request.startsWith("POST /api/mfa/push/respond HTTP/1.1"));
    QVERIFY(request.contains("X-Kypost-Device-Id: device-1"));
    QVERIFY(request.contains("X-Kypost-Device-Secret: secret-1"));
    const QJsonObject sent = fake.receivedJsonBody();
    QCOMPARE(sent.keys(), QStringList({ QStringLiteral("approve"), QStringLiteral("challengeId"),
                                        QStringLiteral("matchDigits") }));
    QCOMPARE(sent.value(QStringLiteral("challengeId")).toString(), QStringLiteral("chal-1"));
    QCOMPARE(sent.value(QStringLiteral("approve")).toBool(), true);
    QCOMPARE(sent.value(QStringLiteral("matchDigits")).toString(), QStringLiteral("42"));
}

void MfaResponseClientTest::denySendsNoDigits()
{
    FakeRelayServer fake(httpResponse(200, "OK", R"({"ok":true,"status":"denied"})"));
    const MfaResponseResult result = respondAgainst(fake, false, QStringLiteral("42"));

    QCOMPARE(result.outcome, MfaResponseOutcome::Success);
    const QJsonObject sent = fake.receivedJsonBody();
    QCOMPARE(sent.value(QStringLiteral("approve")).toBool(true), false);
    QCOMPARE(sent.value(QStringLiteral("matchDigits")).toString(), QString());
}

void MfaResponseClientTest::statusCodesMapToOutcomes_data()
{
    QTest::addColumn<int>("code");
    QTest::addColumn<QByteArray>("body");
    QTest::addColumn<MfaResponseOutcome>("outcome");

    QTest::newRow("200 without ok") << 200 << QByteArray(R"({"status":"approved"})") << MfaResponseOutcome::Failure;
    QTest::newRow("409") << 409 << QByteArray(R"({"error":"challenge already resolved","status":"denied"})")
                         << MfaResponseOutcome::AlreadyAnswered;
    QTest::newRow("429 locked") << 429 << QByteArray(R"({"error":"that is not the number","status":"locked"})")
                                << MfaResponseOutcome::Locked;
    QTest::newRow("429 throttle") << 429 << QByteArray("too many failed attempts, try again later\n")
                                  << MfaResponseOutcome::RateLimited;
    QTest::newRow("401") << 401 << QByteArray("invalid or expired challenge\n") << MfaResponseOutcome::Expired;
    QTest::newRow("403") << 403 << QByteArray("push approval is not enabled for this account\n")
                         << MfaResponseOutcome::Forbidden;
    QTest::newRow("400") << 400 << QByteArray("challengeId is required\n") << MfaResponseOutcome::Failure;
    QTest::newRow("500") << 500 << QByteArray("boom") << MfaResponseOutcome::Failure;
}

void MfaResponseClientTest::statusCodesMapToOutcomes()
{
    QFETCH(int, code);
    QFETCH(QByteArray, body);
    QFETCH(MfaResponseOutcome, outcome);

    FakeRelayServer fake(httpResponse(code, "Status", body));
    QCOMPARE(respondAgainst(fake, true, QStringLiteral("42")).outcome, outcome);
}

QTEST_GUILESS_MAIN(MfaResponseClientTest)
#include "MfaResponseClientTest.moc"
