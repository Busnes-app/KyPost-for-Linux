#include "net/PgpBootstrapClient.h"

#include "net/HttpClient.h"
#include "net/RelayAuth.h"

#include "FakeRelayServer.h"

#include <QNetworkAccessManager>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

class PgpBootstrapClientTest : public QObject
{
    Q_OBJECT

private slots:
    void fingerprintAdmission_data();
    void fingerprintAdmission();
    void parsesIdentityAndProtection();
    void ignoresTheBrowserOnlyFields();
    void failureIsNotAnEmptySuccess();
};

void PgpBootstrapClientTest::fingerprintAdmission_data()
{
    QTest::addColumn<QString>("wire");
    QTest::addColumn<QString>("expected");
    QTest::newRow("address") << QStringLiteral("user@example.com") << QString();
    QTest::newRow("short-key-id") << QStringLiteral("0123456789ABCDEF") << QString();
    QTest::newRow("non-hex") << QString(40, QLatin1Char('Z')) << QString();
    QTest::newRow("trailing-newline") << (QString(40, QLatin1Char('A')) + QLatin1Char('\n')) << QString();
    QTest::newRow("v4") << QString(40, QLatin1Char('a')) << QString(40, QLatin1Char('A'));
    QTest::newRow("v6") << QString(64, QLatin1Char('B')) << QString(64, QLatin1Char('B'));
}

void PgpBootstrapClientTest::fingerprintAdmission()
{
    QFETCH(QString, wire);
    QFETCH(QString, expected);
    const QJsonObject body{{"hasIdentity", true}, {"protection", "client"}, {"fingerprint", wire}};
    FakeRelayServer fake(httpResponse(200, "OK", QJsonDocument(body).toJson()));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    const auto result = PgpBootstrapClient(http).fetch(
        QUrl(QStringLiteral("http://127.0.0.1:%1").arg(fake.port())), {});
    QVERIFY(result.ok);
    QCOMPARE(result.fingerprint, expected);
}

void PgpBootstrapClientTest::parsesIdentityAndProtection()
{
    FakeRelayServer fake(httpResponse(200, "OK", R"({"hasIdentity":true,"protection":"client"})"));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    PgpBootstrapClient client(http);

    const QUrl serverBaseUrl(QStringLiteral("http://127.0.0.1:%1").arg(fake.port()));
    const RelayAuth auth{ QStringLiteral("device-1"), QStringLiteral("secret-1") };
    const PgpBootstrapResult result = client.fetch(serverBaseUrl, auth);

    QCOMPARE(result.ok, true);
    QCOMPARE(result.hasIdentity, true);
    QCOMPARE(result.protection, QStringLiteral("client"));
}

// bootstrap carries wrappedPrivateKey, unlockRequired, signerPublicKeys and
// more that exist for the browser. Unknown/unused fields must not break
// parsing, and nothing here may start depending on them.
void PgpBootstrapClientTest::ignoresTheBrowserOnlyFields()
{
    FakeRelayServer fake(httpResponse(
        200, "OK",
        R"({"hasIdentity":false,"protection":"server","wrappedPrivateKey":"xxx","unlockRequired":true,)"
        R"("signerPublicKeys":[],"payloadEndpoint":"/x","somethingAddedLater":42})"));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    PgpBootstrapClient client(http);

    const QUrl serverBaseUrl(QStringLiteral("http://127.0.0.1:%1").arg(fake.port()));
    const RelayAuth auth{ QStringLiteral("device-1"), QStringLiteral("secret-1") };
    const PgpBootstrapResult result = client.fetch(serverBaseUrl, auth);

    QCOMPARE(result.ok, true);
    QCOMPARE(result.hasIdentity, false);
    QCOMPARE(result.protection, QStringLiteral("server"));
}

// A failure must be distinguishable from a successful "no identity", or the
// compose screen cannot honor "couldn't check is not no".
void PgpBootstrapClientTest::failureIsNotAnEmptySuccess()
{
    FakeRelayServer fake(httpResponse(503, "Service Unavailable", "unavailable", "text/plain"));
    QNetworkAccessManager manager;
    HttpClient http(manager);
    PgpBootstrapClient client(http);

    const QUrl serverBaseUrl(QStringLiteral("http://127.0.0.1:%1").arg(fake.port()));
    const RelayAuth auth{ QStringLiteral("device-1"), QStringLiteral("secret-1") };
    const PgpBootstrapResult result = client.fetch(serverBaseUrl, auth);

    QCOMPARE(result.ok, false);
    QVERIFY(result.error.has_value());
}

QTEST_GUILESS_MAIN(PgpBootstrapClientTest)
#include "PgpBootstrapClientTest.moc"
