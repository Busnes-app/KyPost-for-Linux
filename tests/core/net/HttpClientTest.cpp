#include "net/HttpClient.h"
#include "net/NetworkError.h"

#include "FakeRelayServer.h"
#include "TestTls.h"

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

using namespace TestTls;

// Not FakeRelayServer's httpResponse(): that one sends Connection: close,
// which is precisely what stops the socket from being pooled.
QByteArray keepAliveResponse(const QByteArray& body)
{
    return "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
        + QByteArray::number(body.size()) + "\r\nConnection: keep-alive\r\n\r\n" + body;
}

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
    void aTruncatedSuccessIsATransportFailure_data();
    void aTruncatedSuccessIsATransportFailure();
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
    void aRequestPinRefusesAnotherTrustedIssuerAndLeavesTheClientPinAlone();
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

void HttpClientTest::aTruncatedSuccessIsATransportFailure_data()
{
    QTest::addColumn<bool>("closeMidBody");
    QTest::newRow("connection dropped") << true;
    QTest::newRow("transfer stalled") << false;
}

// A 200 whose body never finished is not a 200. The status branch used to win
// over QNetworkReply's own error, so the first 10 bytes of a 1000-byte
// attachment came back as a success and were saved as the whole file.
void HttpClientTest::aTruncatedSuccessIsATransportFailure()
{
    QFETCH(bool, closeMidBody);
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QTcpSocket* accepted = nullptr;
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&server, &accepted, closeMidBody]() {
        accepted = server.nextPendingConnection();
        QObject::connect(accepted, &QTcpSocket::readyRead, accepted, [accepted, closeMidBody]() {
            accepted->readAll();
            accepted->write("HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n"
                            "Content-Length: 1000\r\n\r\n0123456789");
            accepted->flush();
            if (closeMidBody)
                accepted->disconnectFromHost();
        });
    });

    QNetworkAccessManager manager;
    HttpClient client(manager, 300);

    const QUrl url(QStringLiteral("http://127.0.0.1:%1/api/attachment").arg(server.serverPort()));
    const HttpClient::HttpResult result = client.get(url, {});

    QVERIFY(result.error.has_value());
    QCOMPARE(*result.error, NetworkError::Transport);
    QVERIFY(result.body.isEmpty());

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

// getPinnedTo(): the pin a caller hands in is enforced like the client's own
// -- a server under another, equally trusted issuer is refused at the
// handshake, before the request goes out -- but it is that request's alone:
// the client's pin is not touched and the paired account's mismatch handler
// does not fire.
void HttpClientTest::aRequestPinRefusesAnotherTrustedIssuerAndLeavesTheClientPinAlone()
{
    if (!QSslSocket::supportsSsl())
        QSKIP("no TLS backend available in this build");

    const SslDefaultsGuard sslDefaults;
    trustRoots({ certOf(kTrustedRootPem), certOf(kTrustedRootBPem) });

    TlsRelayServer server(httpResponse(200, "OK", "{\"ok\":true}"));
    QVERIFY(server.isListening());
    server.serveChainB();

    QNetworkAccessManager manager;
    HttpClient client(manager, 5000);
    int reported = 0;
    client.setCertificateMismatchHandler([&reported](const QByteArray&) { ++reported; });
    const QUrl paired(QStringLiteral("https://paired.example"));
    const QByteArray pairedPin(32, '\x02');
    client.setCertificatePin(pairedPin, paired);

    const QUrl url(QStringLiteral("https://127.0.0.1:%1/api/thing").arg(server.serverPort()));
    const QByteArray pinA =
        HttpClient::pinnedSpkiFromChain({ certOf(kServedLeafPem), certOf(kServedIssuerPem) });
    const HttpClient::HttpResult refused =
        client.getPinnedTo({ pinA, url }, url, { { QStringLiteral("X-Secret"), QStringLiteral("s") } });

    QVERIFY(refused.error.has_value());
    QCOMPARE(*refused.error, NetworkError::CertificateMismatch);
    QVERIFY(server.receivedRequests().isEmpty());
    QCOMPARE(reported, 0);
    QCOMPARE(client.certificatePinState().spkiSha256, pairedPin);
    QCOMPARE(client.certificatePinState().origin, paired);

    server.serveChainA();
    const HttpClient::HttpResult served = client.getPinnedTo({ pinA, url }, url, {});
    QVERIFY2(!served.error.has_value(), qUtf8Printable(served.detail));
    QVERIFY(served.certificatePinVerified);
    QCOMPARE(server.receivedRequests().size(), 1);
}

QTEST_GUILESS_MAIN(HttpClientTest)
#include "HttpClientTest.moc"
