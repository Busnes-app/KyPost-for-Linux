#include "net/HttpClient.h"
#include "net/NetworkError.h"

#include "FakeRelayServer.h"

#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSslSocket>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <memory>

namespace {

// A real chain for the pooled-connection test: a leaf for 127.0.0.1 issued
// by "KyPost Test Issuer", under a root that is trusted but not served. The
// pin anchors to the ISSUER (HttpClient::pinnedSpkiFromChain), so the chain
// has to be two deep or the pin cannot be computed at all. Generated with openssl,
// valid to 2036; used nowhere else.
const char* kServedLeafPem = R"PEM(
-----BEGIN CERTIFICATE-----
MIIDPDCCAiSgAwIBAgIBAzANBgkqhkiG9w0BAQsFADAdMRswGQYDVQQDDBJLeVBv
c3QgVGVzdCBJc3N1ZXIwHhcNMjYxMDI3MjEyNzE2WhcNMzYxMDI0MjEyNzE2WjAU
MRIwEAYDVQQDDAkxMjcuMC4wLjEwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEK
AoIBAQDJA8qztywdbJqKEVJikP3cIkANUuHeAHXm4t8PaZZF4ROFfy0v5CjFoz+9
tAS7qtkNAwk+JfFxIyYUqsijpNu/rAxHbJf1XFavik1PjnEQvAWgwiINIuhroXdc
z9bSzGoAthNJqR1x24Q6N7VoQ8LX54q8H3R0QWL/55eHA0mnTsZOdneWx19BvPeZ
/M112ecFCOvZ8Xl6ADtJmTiI9SEAADUcNSmbky55bEbfRu5Eh9Te+oedz0rnbNWz
KuSXV77WuzC7pxPrF1EUXTcuGK8v31tOphtel9nQJfPS1L9rvknHDcFLk1tR8V4s
ogCO17fNlLAT02d9QWM9GPLqOdw1AgMBAAGjgY8wgYwwCQYDVR0TBAIwADAOBgNV
HQ8BAf8EBAMCBaAwEwYDVR0lBAwwCgYIKwYBBQUHAwEwGgYDVR0RBBMwEYcEfwAA
AYIJbG9jYWxob3N0MB0GA1UdDgQWBBQB/scun4urNGIpUcMJ8hnX3OAm1zAfBgNV
HSMEGDAWgBS2xJ1hEG1njmFQwlNqPqpNM982QDANBgkqhkiG9w0BAQsFAAOCAQEA
EvyExpiOvz3e7oXQcaZKaOhh8OwMuekAAooRxpyk/fc3YutQbrT6fD+ZDAttaM/k
qs3DkVGaRat70gSDP4Rs9G8n+tEF3sX8GW9WlPKb8uWuXQ1HJyihcjLM+snLyTvL
OwlJQU7fkKhIRFtsEb1X1B71Mj6n9SJzg2kjMJ+29EvWkIEZKu/CPIoi81mn7cvG
hK2/dVLq3CDd3zOu/xGXBuAYVMbwfBRJN3/UYZbpoHWBiDdxI8Im5IecYdRC5PFc
vr1Lo/5XHGopbU17NM11VC07Db+RbGJ1uOiiZcVPOYTbMi+ehTXVFl9Ey9hQL9hE
ZAjK1bfCBGJDW/L4EG111g==
-----END CERTIFICATE-----
)PEM";

const char* kServedIssuerPem = R"PEM(
-----BEGIN CERTIFICATE-----
MIIDFjCCAf6gAwIBAgIBAjANBgkqhkiG9w0BAQsFADAbMRkwFwYDVQQDDBBLeVBv
c3QgVGVzdCBSb290MB4XDTI2MTAyNzIxMjcxNloXDTM2MTAyNDIxMjcxNlowHTEb
MBkGA1UEAwwSS3lQb3N0IFRlc3QgSXNzdWVyMIIBIjANBgkqhkiG9w0BAQEFAAOC
AQ8AMIIBCgKCAQEA8dDUDn9ldHwKlFh4InFFprJ+IOMeVL7Uw9OhjNRaC2Xpc6TS
9LydEEFBz/Izcq0xEpwXAQyud62ritSyJtCJKju62wd9T21s6r9QslIDSN0gUPZW
nL+IqhHI43wYXtFLu6ymlWmJDo9Nv3a+B+aIb+AqflYyPDJN8J2UX+fzgo4mVvuX
mDfZjxDvHf6dEWGXVtZFRJLlcTEKZCH8wKJtJ0GpVU/E1ocM+/SgeihfflIuCT6v
UzOqOSQZavLscAv8AfPGSpp4qWPn9xeulCPhA35hd8jf9g44sRaIHi6iGu/iGcfW
vG/22A6bT9kQ0uZ7qfG54r5g7/nbK8JwGts8NQIDAQABo2MwYTAPBgNVHRMBAf8E
BTADAQH/MA4GA1UdDwEB/wQEAwIBBjAdBgNVHQ4EFgQUtsSdYRBtZ45hUMJTaj6q
TTPfNkAwHwYDVR0jBBgwFoAUXPX1DSLoti6Qcog4nnevx8iHz8QwDQYJKoZIhvcN
AQELBQADggEBAIAAsemDdokHEAwR9fDNJdUuDpu11wFZpKJDgRyIVfEZJdYZ8Aa3
ntVT6gGAsv01UpZgINTc3pWSrCe4vGSjzkH7mnN5adGWfyhX8IM04WAzZj+ZqpZT
lc4a9L2v2K8O0x6VRKJARvIAJb/aM3FzGlyY5QHTfPBttjCfEY0ywxX971oHR5ri
OM05fuUL4zKBVJdr3tlzRu7tUaKVIHIyICzYKGDU2Jl9+OXP4NI3oLlVHJYTXLGi
fOYjnIAHBRSNIWztf0EoccinG318ChmF7ofzudhSR4Io9HUo9Ge2xxDpF9XT25w1
wvkyzgESvSQ67BIU8XOrgUFtJTeCDL/fr14=
-----END CERTIFICATE-----
)PEM";

// The root the issuer chains to. Trusted by the pinned tests, never served.
const char* kTrustedRootPem = R"PEM(
-----BEGIN CERTIFICATE-----
MIIDJzCCAg+gAwIBAgIUY8yXOt++Bud0ylZsETEdVQXadsYwDQYJKoZIhvcNAQEL
BQAwGzEZMBcGA1UEAwwQS3lQb3N0IFRlc3QgUm9vdDAeFw0yNjEwMjcyMTI3MTZa
Fw0zNjEwMjQyMTI3MTZaMBsxGTAXBgNVBAMMEEt5UG9zdCBUZXN0IFJvb3QwggEi
MA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQCXcTgPuqCrRTMFSgtkSwjfwvmr
njGMcrtBahwekR3ReF34JkFWrXy2LcTK+n2kdmqXTlD7cyDCVduIzkDIDpkW4gnm
5HuDavFARVgxSr2wIn5V7IzLd1ZPXKH6XP2+HGA0BRcrLK4xLB/27m4P2RM35euR
5XjvUiQUKGArnKKA7bj1GB9fn4/eMAc2Mi1caj2JOyFRL0FA//9CbS8cBcTpIROz
mI5faGI3jKXz7cjSlAhm5tpRl1RoszbuMMUtXccgNDcC6eyezpJ5mt3bKaEwKtzQ
INjBEb5J+egJtjsncvZNtGiiXa6UkIy7OJUHyL0Set1w/CuIRvpwhavW0+D5AgMB
AAGjYzBhMB0GA1UdDgQWBBRc9fUNIui2LpByiDied6/HyIfPxDAfBgNVHSMEGDAW
gBRc9fUNIui2LpByiDied6/HyIfPxDAPBgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB
/wQEAwIBBjANBgkqhkiG9w0BAQsFAAOCAQEAPuwbb4KG5LGufiucYjljOo0blUZQ
ESfIF5hf9E3fPHXBxrvPSlC11I5nyKBl6VzW8WPcX6/7m3JTX+3dOAnwdIly8wpL
MQmwbdog09/hfkqLoPNGxZQzv29anbJq584n5NO1HqOPzMmuAnUEbJxdgGseQ6YV
MSYHL4BhpQef1YSHbk1cdkN1ydxDlWg+muNVBNm07rmAxpOfVqw9mD6f/EESIW94
WwMLevUTo0D1o6EEzX5zxYy8jmjt4w59YaxEjpd9S5JvAO3TAV2u96Udxv9fVQfo
F5F/vel+1NuGXBwHoOWILhrjlmmHPft9CP6dlY/QUwEkE0EevrPN447zbQ==
-----END CERTIFICATE-----
)PEM";

const char* kServedLeafKeyPem = R"PEM(
-----BEGIN PRIVATE KEY-----
MIIEvgIBADANBgkqhkiG9w0BAQEFAASCBKgwggSkAgEAAoIBAQDJA8qztywdbJqK
EVJikP3cIkANUuHeAHXm4t8PaZZF4ROFfy0v5CjFoz+9tAS7qtkNAwk+JfFxIyYU
qsijpNu/rAxHbJf1XFavik1PjnEQvAWgwiINIuhroXdcz9bSzGoAthNJqR1x24Q6
N7VoQ8LX54q8H3R0QWL/55eHA0mnTsZOdneWx19BvPeZ/M112ecFCOvZ8Xl6ADtJ
mTiI9SEAADUcNSmbky55bEbfRu5Eh9Te+oedz0rnbNWzKuSXV77WuzC7pxPrF1EU
XTcuGK8v31tOphtel9nQJfPS1L9rvknHDcFLk1tR8V4sogCO17fNlLAT02d9QWM9
GPLqOdw1AgMBAAECggEADi5yjUcZuWdeqBQh72VtHNacwA6OHqMCg7wTQmuE/XVB
iDxJRD+blxqV5gy5KG1HUVlFZxP3CpqRIUmi4eqrpy7ckehpzcFf4JQIXBm/mno4
B8KnyNdNiumyDwNJvlxLXoqkkIs6EieLbAO0ocBlaA/ZiZKgMmMk4yAkCv4oItcL
iwt+7O+AElwxoegLcokDPDGp2bIv1xD6IkbIhDZQoqlg7nXU7WzML45p4tcTfdKC
pI8KBTga85cZJJBhsKqzLTvJwymkbkovI/IizSCIvc6V+vDtpMloE2I58RrY6uZO
RWFVSumht3NaEE/dFPmyBAweigf/gjwAFgiLnXBXwQKBgQD1OUD2U7r96IczzziW
WuczjXzeDncmX5IzAy4aBKSxeSriKFG11a9lPrnyYXYb7YYFisE8rMyMo+ySrnp8
ASmePNgRWFwR0/LBVZPaVZZUm5GLfUDsHI30XE7H8h5TS8QT94qWFFqZMIaefDlO
3VrbF2LGsd2Sxv3nBcM8FiKcwQKBgQDR2TE0z7FNuTjwjm//uSpLWDuRqFqS0XIw
DWmAfcfliB1+/3SCdFfGyryDA7UllSAnVUZjOFFxxn69dzB9Mw3ON32Mtx8Mvksb
OFZGctM+tvu56Lg38YjzfHO3bKwaqhmHrRq8Dze6ExKjeEm4rule5ZeMms7ApFU/
+5goO544dQKBgQCrDAmNCDBpRRVxhPwynCuioPFVLn/bq1Qd8vzoNw7tn9fSsk70
Z52VIGCxeVvZ9R7DpNpOyTVQcFLmqX9UGbd+4W/RAXIPGWi9hETCSWq3j1/BDVgI
mY5GwVBOVUrjzPhy+PqY3o+nRNrRl1Kd+PaT1ccXUx97bwPRbgqVUddIwQKBgQCn
yyZIVyYrwsYfu5Ucgj+qXVPMZTR/HxBSEfdhMbIL++mx6auMwGCLaQKRM/69nV9H
dC1DGv4sno4rC0nvYkpQsRgn0yj1tJcznbmGvTvicsgXYFklCnH6V3HjofZ/n7Go
RicRZo2lsKh/o6g3D001fxnN6N8XNI+iCJgi24DpHQKBgE9PAWIWuM+eKtrz5c+q
3M0BLQZre3rtvWnNBTi7cvHvgJ7JDRJS+DG0TB94bScXJ/cTdGqMTkJIWnuN1F+f
4LrKG0KTxnojYKiicWAgyPWmxk+TSkqgajO97zL1Hid8XLz077yhYIShqRgAy2kk
Q1Je/KawVLddQD4wH7ok7cl3
-----END PRIVATE KEY-----
)PEM";

QSslCertificate certOf(const char* pem)
{
    return QSslCertificate(QByteArray(pem), QSsl::Pem);
}

// Not FakeRelayServer's httpResponse(): that one sends Connection: close,
// which is precisely what stops the socket from being pooled.
QByteArray keepAliveResponse(const QByteArray& body)
{
    return "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
        + QByteArray::number(body.size()) + "\r\nConnection: keep-alive\r\n\r\n" + body;
}

// Restores the process-wide default QSslConfiguration, which the pinned
// tests point at the test issuer: HttpClient always verifies the peer.
struct SslDefaultsGuard
{
    QSslConfiguration previous = QSslConfiguration::defaultConfiguration();
    ~SslDefaultsGuard() { QSslConfiguration::setDefaultConfiguration(previous); }
};

// A TLS server that answers every request on the SAME connection, so Qt's
// connection pool keeps the socket and a second request reuses it without a
// second handshake. That reuse is the case QNetworkReply::encrypted cannot
// see, and connectionCount() is what proves the test really took that path.
// It starts TLS itself because QSslServer is Qt 6.4+.
class KeepAliveTlsServer : public QTcpServer
{
public:
    KeepAliveTlsServer()
    {
        m_config = QSslConfiguration::defaultConfiguration();
        m_config.setLocalCertificateChain({ certOf(kServedLeafPem), certOf(kServedIssuerPem) });
        m_config.setPrivateKey(QSslKey(QByteArray(kServedLeafKeyPem), QSsl::Rsa, QSsl::Pem, QSsl::PrivateKey));
        // Qt offers h2 over ALPN by default for https; answer http/1.1 only,
        // because an h2 stream is a different reuse story from the
        // keep-alive one this test is about.
        m_config.setAllowedNextProtocols({ QSslConfiguration::NextProtocolHttp1_1 });
        listen(QHostAddress::LocalHost);
    }

    int connectionCount() const { return m_connectionCount; }
    int requestCount() const { return m_requestCount; }

protected:
    void incomingConnection(qintptr handle) override
    {
        auto* socket = new QSslSocket(this);
        if (!socket->setSocketDescriptor(handle)) {
            delete socket;
            return;
        }
        socket->setSslConfiguration(m_config);
        connect(socket, &QSslSocket::encrypted, this, [this, socket]() { onEncrypted(socket); });
        socket->startServerEncryption();
    }

private:
    void onEncrypted(QSslSocket* socket)
    {
        ++m_connectionCount;
        auto pending = std::make_shared<QByteArray>();
        connect(socket, &QIODevice::readyRead, this, [this, socket, pending]() {
            *pending += socket->readAll();
            // GETs only: the end of the headers is the end of the request.
            for (int end = pending->indexOf("\r\n\r\n"); end >= 0; end = pending->indexOf("\r\n\r\n")) {
                pending->remove(0, end + 4);
                ++m_requestCount;
                socket->write(keepAliveResponse("{\"ok\":true}"));
                socket->flush();
            }
        });
    }

    QSslConfiguration m_config;
    int m_connectionCount = 0;
    int m_requestCount = 0;
};

} // namespace

class HttpClientTest : public QObject
{
    Q_OBJECT

private slots:
    void getSuccessReturnsBodyUnmodifiedAndPreservesExistingQuery();
    void getMapsUnauthorizedFrom401();
    void postSendsJsonBodyAndContentTypeHeader();
    void putSendsJsonBodyAndContentTypeHeader();
    void delSendsQueryParamsWithNoBody();
    void transportFailureWhenNothingListens();
    void transportFailureWhenServerHangs();
    void getFollowsRedirectWhenValidatorApprovesTarget();
    void getDoesNotFollowRedirectWhenValidatorRejectsTarget();
    void crossOriginRedirectIsRefusedByDefault();
    void refusesAResponseLargerThanTheLimit();
    void refusesAChunkedResponseThatDeclaresNoLength();
    void acceptsAResponseExactlyAtTheLimit();
    void perCallLimitOverridesTheClientDefault();
    void aRequestOnAPooledConnectionIsStillPinChecked();
    void aMismatchOnAFreshHandshakeIsRefusedBeforeTheRequestIsSent();
    void aPinnedOriginOverPlaintextIsNotAMismatch();
};

void HttpClientTest::getSuccessReturnsBodyUnmodifiedAndPreservesExistingQuery()
{
    const QByteArray body = "{\"ok\":true}";
    FakeRelayServer fake(httpResponse(200, "OK", body));
    QNetworkAccessManager manager;
    HttpClient client(manager);

    // url already carries a query item; get() must append sub/hash rather
    // than replacing it.
    QUrl url(QStringLiteral("http://127.0.0.1:%1/api/thing").arg(fake.port()));
    url.setQuery(QStringLiteral("existing=1"));

    const HttpClient::HttpResult result =
        client.get(url, { { QStringLiteral("sub"), QStringLiteral("abc") } });

    QVERIFY(!result.error.has_value());
    QCOMPARE(result.statusCode, 200);
    QCOMPARE(result.body, body);
    QVERIFY(result.detail.isEmpty());
    QVERIFY(fake.receivedRequest().contains(
        "GET /api/thing?existing=1&sub=abc HTTP/1.1"));
}

void HttpClientTest::getMapsUnauthorizedFrom401()
{
    FakeRelayServer fake(httpResponse(401, "Unauthorized", "{}"));
    QNetworkAccessManager manager;
    HttpClient client(manager);

    const QUrl url(QStringLiteral("http://127.0.0.1:%1/api/thing").arg(fake.port()));
    const HttpClient::HttpResult result = client.get(url, {});

    QVERIFY(result.error.has_value());
    QCOMPARE(*result.error, NetworkError::Unauthorized);
    QCOMPARE(result.statusCode, 401);
}

void HttpClientTest::postSendsJsonBodyAndContentTypeHeader()
{
    FakeRelayServer fake(httpResponse(200, "OK", "{}"));
    QNetworkAccessManager manager;
    HttpClient client(manager);

    QJsonObject json;
    json[QStringLiteral("challengeId")] = QStringLiteral("chal-1");
    const QByteArray expectedBody = QJsonDocument(json).toJson(QJsonDocument::Compact);

    const QUrl url(QStringLiteral("http://127.0.0.1:%1/api/mfa/respond").arg(fake.port()));
    const HttpClient::HttpResult result =
        client.post(url, { { QStringLiteral("sub"), QStringLiteral("s1") } }, json);

    QVERIFY(!result.error.has_value());
    QCOMPARE(result.statusCode, 200);

    const QByteArray request = fake.receivedRequest();
    QVERIFY(request.contains("POST /api/mfa/respond?sub=s1 HTTP/1.1"));
    QVERIFY(request.contains("Content-Type: application/json"));
    QVERIFY(request.endsWith(expectedBody));
}

void HttpClientTest::putSendsJsonBodyAndContentTypeHeader()
{
    FakeRelayServer fake(httpResponse(200, "OK", "{}"));
    QNetworkAccessManager manager;
    HttpClient client(manager);

    QJsonObject json;
    json[QStringLiteral("name")] = QStringLiteral("NewName");
    const QByteArray expectedBody = QJsonDocument(json).toJson(QJsonDocument::Compact);

    const QUrl url(QStringLiteral("http://127.0.0.1:%1/api/inbox/folders").arg(fake.port()));
    const HttpClient::HttpResult result =
        client.put(url, { { QStringLiteral("sub"), QStringLiteral("s1") } }, json);

    QVERIFY(!result.error.has_value());
    QCOMPARE(result.statusCode, 200);

    const QByteArray request = fake.receivedRequest();
    QVERIFY(request.contains("PUT /api/inbox/folders?sub=s1 HTTP/1.1"));
    QVERIFY(request.contains("Content-Type: application/json"));
    QVERIFY(request.endsWith(expectedBody));
}

void HttpClientTest::delSendsQueryParamsWithNoBody()
{
    FakeRelayServer fake(httpResponse(200, "OK", "{}"));
    QNetworkAccessManager manager;
    HttpClient client(manager);

    const QUrl url(QStringLiteral("http://127.0.0.1:%1/api/inbox/folders").arg(fake.port()));
    const HttpClient::HttpResult result = client.del(url, { { QStringLiteral("folder"), QStringLiteral("OldFolder") } });

    QVERIFY(!result.error.has_value());
    QCOMPARE(result.statusCode, 200);

    const QByteArray request = fake.receivedRequest();
    QVERIFY(request.contains("DELETE /api/inbox/folders?folder=OldFolder HTTP/1.1"));
    QVERIFY(!request.contains("Content-Length:"));
}

void HttpClientTest::transportFailureWhenNothingListens()
{
    // Grab an ephemeral port, then close the listener immediately so
    // nothing is listening on it when the client connects.
    QTcpServer probe;
    QVERIFY(probe.listen(QHostAddress::LocalHost));
    const quint16 freePort = probe.serverPort();
    probe.close();

    QNetworkAccessManager manager;
    HttpClient client(manager);
    const QUrl url(QStringLiteral("http://127.0.0.1:%1/api/thing").arg(freePort));
    const HttpClient::HttpResult result = client.get(url, {});

    QVERIFY(result.error.has_value());
    QCOMPARE(*result.error, NetworkError::Transport);
    QCOMPARE(result.statusCode, 0);
    QVERIFY(!result.detail.isEmpty());
}

void HttpClientTest::transportFailureWhenServerHangs()
{
    // Accepts the connection but never writes a response -- simulates a
    // hung/silent server, which used to leave waitForReply()'s QEventLoop
    // spinning forever since nothing ever emits QNetworkReply::finished.
    // Uses a short (100ms) constructor-injected timeout override so this
    // test stays fast/deterministic rather than waiting out the real
    // 30-second default.
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QTcpSocket* accepted = nullptr;
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&server, &accepted]() {
        accepted = server.nextPendingConnection();
    });

    QNetworkAccessManager manager;
    HttpClient client(manager, 100);

    const QUrl url(QStringLiteral("http://127.0.0.1:%1/api/thing").arg(server.serverPort()));
    const HttpClient::HttpResult result = client.get(url, {});

    QVERIFY(result.error.has_value());
    QCOMPARE(*result.error, NetworkError::Transport);
    QCOMPARE(result.statusCode, 0);
    QVERIFY(!result.detail.isEmpty());

    delete accepted;
}

void HttpClientTest::getFollowsRedirectWhenValidatorApprovesTarget()
{
    const QByteArray finalBody = "{\"ok\":true,\"from\":\"final\"}";
    FakeRelayServer finalServer(httpResponse(200, "OK", finalBody));

    const QByteArray location =
        QStringLiteral("http://127.0.0.1:%1/final").arg(finalServer.port()).toUtf8();
    FakeRelayServer redirectingServer(
        httpResponse(302, "Found", "", "text/plain", { { "Location", location } }));

    QNetworkAccessManager manager;
    HttpClient client(manager);

    QStringList approvedTargets;
    const HttpClient::RedirectValidator approveAll = [&approvedTargets](const QUrl& target) {
        approvedTargets.append(target.toString());
        return true;
    };

    const QUrl url(QStringLiteral("http://127.0.0.1:%1/start").arg(redirectingServer.port()));
    const HttpClient::HttpResult result = client.get(url, {}, {}, approveAll);

    QVERIFY(!result.error.has_value());
    QCOMPARE(result.statusCode, 200);
    QCOMPARE(result.body, finalBody);
    QCOMPARE(approvedTargets.size(), 1);
    QVERIFY(approvedTargets.first().contains(QStringLiteral("/final")));
}

void HttpClientTest::getDoesNotFollowRedirectWhenValidatorRejectsTarget()
{
    // VibeSec regression guard: a redirect target must be re-validated by
    // the caller-supplied validator, not followed blindly -- otherwise a
    // URL that legitimately passes an initial safety check (e.g.
    // isSafeQrTarget) could still redirect the actual request to a
    // link-local/metadata address.
    const QByteArray finalBody = "{\"ok\":true,\"from\":\"final\"}";
    FakeRelayServer finalServer(httpResponse(200, "OK", finalBody));

    const QByteArray location =
        QStringLiteral("http://127.0.0.1:%1/final").arg(finalServer.port()).toUtf8();
    FakeRelayServer redirectingServer(
        httpResponse(302, "Found", "", "text/plain", { { "Location", location } }));

    QNetworkAccessManager manager;
    HttpClient client(manager);

    const HttpClient::RedirectValidator rejectAll = [](const QUrl&) { return false; };

    const QUrl url(QStringLiteral("http://127.0.0.1:%1/start").arg(redirectingServer.port()));
    const HttpClient::HttpResult result = client.get(url, {}, {}, rejectAll);

    // The redirect was never followed, and the rejection surfaces as a
    // clear failure -- not a misleadingly "successful" 302 response the
    // caller might mistake for legitimate data, and definitely not the
    // final server's body.
    QVERIFY(result.error.has_value());
    QVERIFY(result.body != finalBody);
    // ...and it is named for what happened. Aborting mid-redirect leaves
    // the 3xx as the reply's status, which the status-code mapping turns
    // into a generic Server error -- so "the relay tried to send the device
    // secret to another host" and "the relay returned a 500" used to be the
    // same value to every caller and every log line.
    QCOMPARE(*result.error, NetworkError::RedirectRefused);
}

void HttpClientTest::crossOriginRedirectIsRefusedByDefault()
{
    // Qt's default (NoLessSafeRedirectPolicy) follows cross-HOST redirects
    // and strips nothing: every redirect status forwards custom headers --
    // including X-Kypost-Device-Secret -- and 307/308 forward the body too,
    // which on the registration POST is the subscriber id, the pairing token
    // and the UnifiedPush endpoint. A caller that names no validator now gets
    // a same-origin-only default instead.
    const QByteArray finalBody = "{\"ok\":true,\"from\":\"final\"}";
    FakeRelayServer finalServer(httpResponse(200, "OK", finalBody));

    const QByteArray location =
        QStringLiteral("http://127.0.0.1:%1/final").arg(finalServer.port()).toUtf8();
    FakeRelayServer redirectingServer(
        httpResponse(302, "Found", "", "text/plain", { { "Location", location } }));

    QNetworkAccessManager manager;
    HttpClient client(manager);

    const QUrl url(QStringLiteral("http://127.0.0.1:%1/start").arg(redirectingServer.port()));
    const HttpClient::HttpResult result =
        client.get(url, {}, { { QStringLiteral("X-Kypost-Device-Secret"), QStringLiteral("top-secret") } });

    // Refused, and the secret never reached the other origin.
    QVERIFY(result.body != finalBody);
    QVERIFY(!finalServer.receivedRequest().contains("top-secret"));
    QVERIFY(result.error.has_value());
    QCOMPARE(*result.error, NetworkError::RedirectRefused);
}


// A relay is inside this app's threat model everywhere else, so "the relay
// would never send that much" is not a bound. QNetworkReply buffers the whole
// body in memory and readAll() handed over whatever arrived, with nothing
// anywhere saying no -- one hostile response was an unbounded allocation.
void HttpClientTest::refusesAResponseLargerThanTheLimit()
{
    const QByteArray body(4096, 'x');
    FakeRelayServer fake(httpResponse(200, "OK", body));

    QNetworkAccessManager manager;
    HttpClient client(manager, 5000, /*maxResponseBytes=*/1024);

    const HttpClient::HttpResult result =
        client.get(QUrl(QStringLiteral("http://127.0.0.1:%1/big").arg(fake.port())), {});

    QVERIFY(result.error.has_value());
    QCOMPARE(*result.error, NetworkError::ResponseTooLarge);
    // The partial body must NOT come back: half a JSON document is not a
    // smaller JSON document, and returning one is how a size guard becomes a
    // silent data-corruption bug.
    QVERIFY(result.body.isEmpty());
}

// Content-Length is the cheap check and cannot be the only one: it is
// OPTIONAL. A chunked response declares no length at all, so the header guard
// has nothing to look at and the running byte count is the sole defence.
//
// (An under-declared Content-Length is not the gap it looks like -- measured:
// Qt stops reading at the declared length and discards the rest, so a server
// claiming 8 bytes and sending 8192 cannot make this client allocate 8192.
// The first version of this test asserted exactly that and failed, correctly.
// Over-declaring IS caught, at the headers, before any body arrives.)
void HttpClientTest::refusesAChunkedResponseThatDeclaresNoLength()
{
    QByteArray response = "HTTP/1.1 200 OK\r\n";
    response += "Content-Type: application/json\r\n";
    response += "Transfer-Encoding: chunked\r\n";
    response += "Connection: close\r\n\r\n";
    // 16 chunks of 512 bytes = 8192, well past the 1024 ceiling, with no
    // total ever declared.
    for (int i = 0; i < 16; ++i) {
        response += QByteArray::number(512, 16) + "\r\n";
        response += QByteArray(512, 'x') + "\r\n";
    }
    response += "0\r\n\r\n";

    FakeRelayServer fake(response);

    QNetworkAccessManager manager;
    HttpClient client(manager, 5000, /*maxResponseBytes=*/1024);

    const HttpClient::HttpResult result =
        client.get(QUrl(QStringLiteral("http://127.0.0.1:%1/chunked").arg(fake.port())), {});

    QVERIFY(result.error.has_value());
    QCOMPARE(*result.error, NetworkError::ResponseTooLarge);
    QVERIFY(result.body.isEmpty());
}

// The boundary is inclusive: a response exactly at the ceiling is legitimate.
// Pinned because an off-by-one here rejects real mail rather than an attack,
// which is the failure mode nobody notices until a user cannot sync.
void HttpClientTest::acceptsAResponseExactlyAtTheLimit()
{
    const QByteArray body(1024, 'x');
    FakeRelayServer fake(httpResponse(200, "OK", body));

    QNetworkAccessManager manager;
    HttpClient client(manager, 5000, /*maxResponseBytes=*/1024);

    const HttpClient::HttpResult result =
        client.get(QUrl(QStringLiteral("http://127.0.0.1:%1/exact").arg(fake.port())), {});

    QVERIFY(!result.error.has_value());
    QCOMPARE(result.body.size(), 1024);
}

// The inbox window and an attachment download legitimately dwarf the default,
// and say so at their own call sites (RelayMailSource). Everything else keeps
// the tight ceiling.
void HttpClientTest::perCallLimitOverridesTheClientDefault()
{
    const QByteArray body(4096, 'x');
    FakeRelayServer fake(httpResponse(200, "OK", body));

    QNetworkAccessManager manager;
    HttpClient client(manager, 5000, /*maxResponseBytes=*/1024);

    const HttpClient::HttpResult result =
        client.get(QUrl(QStringLiteral("http://127.0.0.1:%1/inbox").arg(fake.port())), {}, {}, {},
                    /*maxResponseBytes=*/8192);

    QVERIFY(!result.error.has_value());
    QCOMPARE(result.body.size(), 4096);
}

// The pin is a property of the REQUEST, not of the connection it happens to
// travel on. QNetworkReply::encrypted fires once per TLS connection, so a
// socket opened while the pin was deliberately suspended --
// DeviceRegistrationService clears it for the duration of every
// registration, and reregisterIfPaired() runs unattended -- stays in Qt's
// pool, and every later authenticated request that reuses it used to skip
// the pin check entirely and send the device secret to whatever answered.
void HttpClientTest::aRequestOnAPooledConnectionIsStillPinChecked()
{
    if (!QSslSocket::supportsSsl())
        QSKIP("no TLS backend available in this build");

    const SslDefaultsGuard sslDefaults;
    QSslConfiguration trusted = QSslConfiguration::defaultConfiguration();
    trusted.setCaCertificates({ certOf(kTrustedRootPem) });
    QSslConfiguration::setDefaultConfiguration(trusted);

    KeepAliveTlsServer server;
    QVERIFY(server.isListening());

    QNetworkAccessManager manager;
    HttpClient client(manager, 5000);

    QList<QByteArray> reported;
    client.setCertificateMismatchHandler(
        [&reported](const QByteArray& observed) { reported.append(observed); });

    const QUrl url(QStringLiteral("https://127.0.0.1:%1/api/thing").arg(server.serverPort()));

    // First request with no pin in force -- a registration window. This is
    // the handshake that puts the connection in the pool.
    const HttpClient::HttpResult first = client.get(url, {});
    QVERIFY2(!first.error.has_value(), qUtf8Printable(first.detail));
    QVERIFY(!first.certificatePinVerified);
    QCOMPARE(first.statusCode, 200);
    QCOMPARE(server.connectionCount(), 1);

    // The pin comes back (~PairAttempt) and this server does not satisfy it.
    client.setCertificatePin(QByteArray(32, '\x01'), url);
    const HttpClient::HttpResult second = client.get(url, {});

    // No second handshake: ::encrypted did not fire for this request, which
    // is what makes this the pooled path and not a fresh connection.
    QCOMPARE(server.connectionCount(), 1);
    QCOMPARE(server.requestCount(), 2);

    QVERIFY(second.error.has_value());
    QCOMPARE(*second.error, NetworkError::CertificateMismatch);
    QVERIFY(!second.certificatePinVerified);
    // Same failure shape as an aborted handshake: nothing the unpinned
    // server said is handed back.
    QVERIFY(second.body.isEmpty());
    QCOMPARE(second.statusCode, 0);
    // ...and the persistent surface is raised exactly once, carrying the
    // SPKI actually presented so the recovery dialog can name it.
    QCOMPARE(reported.size(), 1);
    const QByteArray honestPin =
        HttpClient::pinnedSpkiFromChain({ certOf(kServedLeafPem), certOf(kServedIssuerPem) });
    QCOMPARE(reported.first(), honestPin);

    // The same pooled socket answers normally once the pin is the right one:
    // what fails above is the comparison, not the reuse.
    client.setCertificatePin(honestPin, url);
    const HttpClient::HttpResult third = client.get(url, {});
    QVERIFY2(!third.error.has_value(), qUtf8Printable(third.detail));
    QVERIFY(third.certificatePinVerified);
    QCOMPARE(third.statusCode, 200);
    QCOMPARE(server.connectionCount(), 1);
    QCOMPARE(reported.size(), 1);
}

// Why the ::encrypted handler is still there as well: on a FRESH handshake
// it aborts between the handshake and the request being written, so a server
// that fails the pin never receives the device secret at all. The post-loop
// check cannot do that -- by then the request has been sent -- and a
// mismatch caught here must not also be reported by it.
void HttpClientTest::aMismatchOnAFreshHandshakeIsRefusedBeforeTheRequestIsSent()
{
    if (!QSslSocket::supportsSsl())
        QSKIP("no TLS backend available in this build");

    const SslDefaultsGuard sslDefaults;
    QSslConfiguration trusted = QSslConfiguration::defaultConfiguration();
    trusted.setCaCertificates({ certOf(kTrustedRootPem) });
    QSslConfiguration::setDefaultConfiguration(trusted);

    KeepAliveTlsServer server;
    QVERIFY(server.isListening());

    QNetworkAccessManager manager;
    HttpClient client(manager, 5000);

    QList<QByteArray> reported;
    client.setCertificateMismatchHandler(
        [&reported](const QByteArray& observed) { reported.append(observed); });

    const QUrl url(QStringLiteral("https://127.0.0.1:%1/api/thing").arg(server.serverPort()));
    client.setCertificatePin(QByteArray(32, '\x01'), url);

    const HttpClient::HttpResult result = client.get(url, {});

    QVERIFY(result.error.has_value());
    QCOMPARE(*result.error, NetworkError::CertificateMismatch);
    // Nothing was ever sent: the abort lands between the handshake and the
    // request line. (connectionCount() is not asserted -- the client tears
    // the socket down so promptly that the server side is often not even
    // handed to the accept queue.)
    QCOMPARE(server.requestCount(), 0);
    QCOMPARE(reported.size(), 1);       // once, not once per check
}

// A reply with no peer certificate chain at all -- plaintext, or a
// connection that never got as far as a handshake -- is not an
// impersonation. Reading the chain after the fact must not turn every
// non-TLS reply into the permanent "re-pair this device" banner.
void HttpClientTest::aPinnedOriginOverPlaintextIsNotAMismatch()
{
    FakeRelayServer fake(httpResponse(200, "OK", "{}"));
    QNetworkAccessManager manager;
    HttpClient client(manager);

    const QUrl url(QStringLiteral("http://127.0.0.1:%1/api/thing").arg(fake.port()));
    client.setCertificatePin(QByteArray(32, '\x01'), url);
    int mismatches = 0;
    client.setCertificateMismatchHandler([&mismatches](const QByteArray&) { ++mismatches; });

    const HttpClient::HttpResult result = client.get(url, {});

    QVERIFY(!result.error.has_value());
    QCOMPARE(result.statusCode, 200);
    QCOMPARE(mismatches, 0);
}

QTEST_GUILESS_MAIN(HttpClientTest)
#include "HttpClientTest.moc"
