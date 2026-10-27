// Real TLS connections through HttpClient, the path every relay and QR request
// takes. Each refused row must send zero request bytes; each weak row has a
// control proving the weak server really was usable.

#include "net/HttpClient.h"
#include "net/NetworkError.h"
#include "net/TlsPolicy.h"

#include <QCoreApplication>
#include <QFile>
#include <QHostAddress>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSignalSpy>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslDiffieHellmanParameters>
#include <QSslKey>
#include <QSslSocket>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTest>

#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <map>
#include <memory>

namespace {

using Pkey = std::shared_ptr<EVP_PKEY>;
using Cert = std::shared_ptr<X509>;

Pkey rsaKey(int bits)
{
    return Pkey(EVP_PKEY_Q_keygen(nullptr, nullptr, "RSA", size_t(bits)), EVP_PKEY_free);
}

Pkey ecKey(const char* curve)
{
    return Pkey(EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", curve), EVP_PKEY_free);
}

Pkey namedKey(const char* type)
{
    return Pkey(EVP_PKEY_Q_keygen(nullptr, nullptr, type), EVP_PKEY_free);
}

QByteArray drain(BIO* bio)
{
    char* bytes = nullptr;
    const long size = BIO_get_mem_data(bio, &bytes);
    return QByteArray(bytes, int(size));
}

QSslCertificate qtCert(const Cert& cert)
{
    std::unique_ptr<BIO, decltype(&BIO_free)> bio(BIO_new(BIO_s_mem()), BIO_free);
    PEM_write_bio_X509(bio.get(), cert.get());
    return QSslCertificate(drain(bio.get()), QSsl::Pem);
}

QSslKey qtKey(const Pkey& key)
{
    std::unique_ptr<BIO, decltype(&BIO_free)> bio(BIO_new(BIO_s_mem()), BIO_free);
    PEM_write_bio_PrivateKey(bio.get(), key.get(), nullptr, nullptr, 0, nullptr, nullptr);
    return QSslKey(drain(bio.get()), EVP_PKEY_is_a(key.get(), "EC") ? QSsl::Ec : QSsl::Rsa, QSsl::Pem,
                   QSsl::PrivateKey);
}

// Published RFC 5114 / RFC 7919 groups: no slow parameter generation.
QSslDiffieHellmanParameters dhGroup(const char* group)
{
    std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> ctx(
        EVP_PKEY_CTX_new_from_name(nullptr, "DH", nullptr), EVP_PKEY_CTX_free);
    const OSSL_PARAM params[] = { OSSL_PARAM_construct_utf8_string(OSSL_PKEY_PARAM_GROUP_NAME,
                                                                   const_cast<char*>(group), 0),
                                  OSSL_PARAM_construct_end() };
    EVP_PKEY* raw = nullptr;
    if (!ctx || EVP_PKEY_paramgen_init(ctx.get()) != 1 || EVP_PKEY_CTX_set_params(ctx.get(), params) != 1
        || EVP_PKEY_paramgen(ctx.get(), &raw) != 1)
        qFatal("cannot create DH fixture %s", group);
    const Pkey parameters(raw, EVP_PKEY_free);
    std::unique_ptr<BIO, decltype(&BIO_free)> bio(BIO_new(BIO_s_mem()), BIO_free);
    PEM_write_bio_Parameters(bio.get(), parameters.get());
    return QSslDiffieHellmanParameters::fromEncoded(drain(bio.get()), QSsl::Pem);
}

struct Issued
{
    Pkey key;
    Cert cert;
};

int g_serial = 0;

void addExtension(X509* cert, X509V3_CTX* ctx, int nid, const char* value)
{
    X509_EXTENSION* ext = X509V3_EXT_conf_nid(nullptr, ctx, nid, value);
    if (ext == nullptr || X509_add_ext(cert, ext, -1) != 1)
        qFatal("cannot add certificate extension %d", nid);
    X509_EXTENSION_free(ext);
}

// issuer == nullptr makes a self-signed certificate.
Issued issue(const Pkey& key, const char* commonName, const Issued* issuer, bool ca,
             const EVP_MD* digest = EVP_sha256(), const char* names = "IP:127.0.0.1,DNS:localhost",
             bool expired = false)
{
    const Cert cert(X509_new(), X509_free);
    X509_set_version(cert.get(), 2);
    ASN1_INTEGER_set(X509_get_serialNumber(cert.get()), ++g_serial);
    X509_gmtime_adj(X509_getm_notBefore(cert.get()), expired ? -172800 : -3600);
    X509_gmtime_adj(X509_getm_notAfter(cert.get()), expired ? -86400 : 172800);
    X509_set_pubkey(cert.get(), key.get());
    X509_NAME* subject = X509_get_subject_name(cert.get());
    X509_NAME_add_entry_by_txt(subject, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char*>(commonName), -1,
                               -1, 0);
    X509* signerCert = issuer ? issuer->cert.get() : cert.get();
    X509_set_issuer_name(cert.get(), X509_get_subject_name(signerCert));
    X509V3_CTX ctx;
    X509V3_set_ctx_nodb(&ctx);
    X509V3_set_ctx(&ctx, signerCert, cert.get(), nullptr, nullptr, 0);
    addExtension(cert.get(), &ctx, NID_subject_key_identifier, "hash");
    addExtension(cert.get(), &ctx, NID_basic_constraints, ca ? "critical,CA:TRUE" : "critical,CA:FALSE");
    if (ca) {
        addExtension(cert.get(), &ctx, NID_key_usage, "critical,keyCertSign,cRLSign");
    } else {
        addExtension(cert.get(), &ctx, NID_key_usage, "critical,digitalSignature,keyEncipherment");
        addExtension(cert.get(), &ctx, NID_ext_key_usage, "serverAuth");
        addExtension(cert.get(), &ctx, NID_subject_alt_name, names);
    }
    EVP_PKEY* signer = issuer ? issuer->key.get() : key.get();
    const bool fixedDigest = EVP_PKEY_is_a(signer, "ED25519") || EVP_PKEY_is_a(signer, "ED448");
    if (X509_sign(cert.get(), signer, fixedDigest ? nullptr : digest) <= 0)
        qFatal("cannot sign certificate %s", commonName);
    return { key, cert };
}

struct Fixture
{
    QList<QSslCertificate> served; // leaf first
    QSslKey key;
    QList<QSslCertificate> trust;
};

class TlsServer : public QTcpServer
{
public:
    QSslConfiguration tls;
    QByteArray received;
    int connections = 0;

protected:
    void incomingConnection(qintptr descriptor) override
    {
        ++connections;
        auto* socket = new QSslSocket(this);
        socket->setSslConfiguration(tls);
        if (!socket->setSocketDescriptor(descriptor)) {
            delete socket;
            return;
        }
        connect(socket, &QSslSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QIODevice::readyRead, this, [this, socket]() {
            received += socket->readAll();
            const int end = received.indexOf("\r\n\r\n");
            if (end < 0)
                return;
            int length = 0;
            for (const QByteArray& line : received.left(end).split('\n')) {
                if (line.toLower().startsWith("content-length:"))
                    length = line.mid(line.indexOf(':') + 1).trimmed().toInt();
            }
            if (received.size() < end + 4 + length)
                return;
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 11\r\n"
                          "Connection: keep-alive\r\n\r\n{\"ok\":true}");
        });
        socket->startServerEncryption();
    }
};

QByteArray kMarker = QByteArrayLiteral("synthetic-device-secret-for-a-weak-peer");

} // namespace

class TlsKeyPolicyTest : public QObject
{
    Q_OBJECT

    QSslConfiguration m_original;
    std::map<QString, Fixture> m_fixtures;
    QSslDiffieHellmanParameters m_dh1024;
    QSslDiffieHellmanParameters m_dh2048;

    QSslConfiguration serverConfig(const Fixture& fixture, QSsl::SslProtocol protocol, bool dhe) const
    {
        QSslConfiguration config = m_original;
        config.setProtocol(protocol);
        config.setPeerVerifyMode(QSslSocket::VerifyNone); // no client certificate
        config.setLocalCertificateChain(fixture.served);
        config.setPrivateKey(fixture.key);
        config.setAllowedNextProtocols({ QSslConfiguration::NextProtocolHttp1_1 });
        config.setBackendConfigurationOption(QByteArrayLiteral("CipherString"),
                                             dhe ? QByteArrayLiteral("DHE-RSA-AES128-GCM-SHA256:@SECLEVEL=0")
                                                 : QByteArrayLiteral("ALL:@SECLEVEL=0"));
        return config;
    }

    // What a weakened inherited default would hand every request.
    void inheritWeakDefaults(const QList<QSslCertificate>& trust)
    {
        QSslConfiguration defaults = m_original;
        defaults.setCaCertificates(trust);
        defaults.setProtocol(QSsl::AnyProtocol);
        defaults.setPeerVerifyMode(QSslSocket::VerifyNone);
        defaults.setBackendConfigurationOption(QByteArrayLiteral("CipherString"),
                                               QByteArrayLiteral("ALL:@SECLEVEL=0"));
        QSslConfiguration::setDefaultConfiguration(defaults);
    }

    static QUrl urlFor(const TlsServer& server)
    {
        return QUrl(QStringLiteral("https://127.0.0.1:%1/api/credential").arg(server.serverPort()));
    }

private slots:
    void initTestCase();
    void cleanup() { QSslConfiguration::setDefaultConfiguration(m_original); }
    void requests_data();
    void requests();
    void aWeakConnectionPooledBeforeTheClientIsStillRefused();
    void anchorsBelowTheFloorAreDropped();
    void certificateKeyTypes();
#if QT_VERSION >= QT_VERSION_CHECK(6, 1, 0)
    void httpsIsRefusedWithoutTheOpenSslBackend();
#endif
};

void TlsKeyPolicyTest::initTestCase()
{
    QVERIFY(strongTlsBackendAvailable());
    qInfo("Qt build %s, runtime %s; TLS build %s, runtime %s", QT_VERSION_STR, qVersion(),
          qPrintable(QSslSocket::sslLibraryBuildVersionString()), qPrintable(QSslSocket::sslLibraryVersionString()));
    m_original = QSslConfiguration::defaultConfiguration();

    const Issued root = issue(rsaKey(2048), "KyPost Test Root", nullptr, true);
    const Issued other = issue(rsaKey(2048), "Unrelated Root", nullptr, true);
    const Pkey strongLeafKey = rsaKey(2048);
    const auto leaf = [&](const QString& name, const Issued& signed_, QList<QSslCertificate> extra,
                          QList<QSslCertificate> trust) {
        m_fixtures[name] = { QList<QSslCertificate>{ qtCert(signed_.cert) } + extra, qtKey(signed_.key), trust };
    };
    const QList<QSslCertificate> rootTrust{ qtCert(root.cert) };

    for (int bits : { 1024, 2047, 2048 }) {
        const Pkey key = rsaKey(bits);
        QCOMPARE(EVP_PKEY_get_bits(key.get()), bits);
        leaf(QStringLiteral("rsa%1").arg(bits), issue(key, "leaf", &root, false), {}, rootTrust);
    }
    leaf(QStringLiteral("ec256"), issue(ecKey("P-256"), "leaf", &root, false), {}, rootTrust);
    leaf(QStringLiteral("ec192"), issue(ecKey("P-192"), "leaf", &root, false), {}, rootTrust);
    for (int bits : { 1024, 2047, 2048 }) {
        const Issued intermediate = issue(rsaKey(bits), "Intermediate", &root, true);
        leaf(QStringLiteral("inter%1").arg(bits), issue(strongLeafKey, "leaf", &intermediate, false),
             { qtCert(intermediate.cert) }, rootTrust);
    }
    for (int bits : { 1024, 2047 }) {
        // The weak root is trusted but never sent: only the anchor filter sees it.
        const Issued weakRoot = issue(rsaKey(bits), "Weak Root", nullptr, true);
        leaf(QStringLiteral("root%1").arg(bits), issue(strongLeafKey, "leaf", &weakRoot, false), {},
             { qtCert(weakRoot.cert) });
        // A self-signed leaf trusted directly.
        const Issued direct = issue(rsaKey(bits), "127.0.0.1", nullptr, false);
        leaf(QStringLiteral("direct%1").arg(bits), direct, {}, { qtCert(direct.cert) });
    }
    leaf(QStringLiteral("sha1"), issue(strongLeafKey, "leaf", &root, false, EVP_sha1()), {}, rootTrust);
    leaf(QStringLiteral("expired"),
         issue(strongLeafKey, "leaf", &root, false, EVP_sha256(), "IP:127.0.0.1,DNS:localhost", true), {}, rootTrust);
    leaf(QStringLiteral("wrongname"), issue(strongLeafKey, "leaf", &root, false, EVP_sha256(), "DNS:example.invalid"),
         {}, rootTrust);
    leaf(QStringLiteral("untrusted"), issue(strongLeafKey, "leaf", &other, false), {}, rootTrust);

    m_dh1024 = dhGroup("dh_1024_160");
    m_dh2048 = dhGroup("ffdhe2048");
    QVERIFY(m_dh1024.isValid());
    QVERIFY(m_dh2048.isValid());
}

void TlsKeyPolicyTest::requests_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<int>("protocol");
    QTest::addColumn<QString>("dh");
    QTest::addColumn<bool>("accepted");
    // Whether a weak-defaults client must succeed against this fixture, which
    // proves the server was usable and the refusal is the client's policy.
    QTest::addColumn<bool>("control");

    for (QSsl::SslProtocol protocol : { QSsl::TlsV1_2, QSsl::TlsV1_3 }) {
        const QString suffix = protocol == QSsl::TlsV1_2 ? QStringLiteral("-tls12") : QStringLiteral("-tls13");
        for (const char* name : { "rsa2048", "ec256", "inter2048" })
            QTest::newRow(qPrintable(QLatin1String(name) + suffix)) << QString::fromLatin1(name) << int(protocol)
                                                                     << QString() << true << true;
        for (const char* name : { "rsa1024", "rsa2047", "inter1024", "inter2047", "root1024", "root2047",
                                  "direct1024", "direct2047", "sha1" })
            QTest::newRow(qPrintable(QLatin1String(name) + suffix)) << QString::fromLatin1(name) << int(protocol)
                                                                     << QString() << false << true;
        for (const char* name : { "expired", "wrongname", "untrusted" })
            QTest::newRow(qPrintable(QLatin1String(name) + suffix)) << QString::fromLatin1(name) << int(protocol)
                                                                     << QString() << false << false;
    }
    // EC-192 may not be negotiable at all on this OpenSSL; its literal refusal
    // is proven in certificateKeyTypes().
    QTest::newRow("ec192-tls12") << QStringLiteral("ec192") << int(QSsl::TlsV1_2) << QString() << false << false;
    QT_WARNING_PUSH
    QT_WARNING_DISABLE_DEPRECATED
    QTest::newRow("tls10") << QStringLiteral("rsa2048") << int(QSsl::TlsV1_0) << QString() << false << true;
    QTest::newRow("tls11") << QStringLiteral("rsa2048") << int(QSsl::TlsV1_1) << QString() << false << true;
    QT_WARNING_POP
    QTest::newRow("dh1024-tls12") << QStringLiteral("rsa2048") << int(QSsl::TlsV1_2) << QStringLiteral("dh1024")
                                  << false << true;
    QTest::newRow("dh2048-tls12") << QStringLiteral("rsa2048") << int(QSsl::TlsV1_2) << QStringLiteral("dh2048")
                                  << true << true;
}

void TlsKeyPolicyTest::requests()
{
    QFETCH(QString, name);
    QFETCH(int, protocol);
    QFETCH(QString, dh);
    QFETCH(bool, accepted);
    QFETCH(bool, control);

    const Fixture& fixture = m_fixtures.at(name);
    inheritWeakDefaults(fixture.trust);

    TlsServer server;
    server.tls = serverConfig(fixture, QSsl::SslProtocol(protocol), !dh.isEmpty());
    if (!dh.isEmpty())
        server.tls.setDiffieHellmanParameters(dh == QLatin1String("dh1024") ? m_dh1024 : m_dh2048);
    QVERIFY(server.listen(QHostAddress::LocalHost));
    const QUrl url = urlFor(server);

    if (control) {
        QNetworkAccessManager manager;
        QNetworkRequest request(url);
        QSslConfiguration verified = request.sslConfiguration();
        verified.setPeerVerifyMode(QSslSocket::VerifyPeer);
        request.setSslConfiguration(verified);
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("text/plain"));
        request.setTransferTimeout(5000);
        std::unique_ptr<QNetworkReply> reply(manager.post(request, QByteArrayLiteral("fixture-control")));
        QSignalSpy finished(reply.get(), &QNetworkReply::finished);
        QVERIFY(finished.wait(7000));
        QVERIFY2(reply->error() == QNetworkReply::NoError, qPrintable(reply->errorString()));
        QVERIFY(server.received.contains("fixture-control"));
        server.received.clear();
    }

    QNetworkAccessManager manager;
    HttpClient client(manager, 5000);
    const int connectionsBefore = server.connections;
    for (int attempt = 0; attempt < 2; ++attempt) {
        server.received.clear();
        const HttpClient::HttpResult result =
            client.post(url, {}, QJsonObject{ { QStringLiteral("secret"), QString::fromLatin1(kMarker) } });
        if (accepted) {
            QVERIFY2(!result.error.has_value(), qPrintable(result.detail));
            QCOMPARE(result.statusCode, 200);
            QVERIFY(server.received.startsWith("POST /api/credential HTTP/1.1\r\n"));
            QVERIFY(server.received.contains(kMarker));
        } else {
            QVERIFY(result.error.has_value());
            QCOMPARE(*result.error, NetworkError::Transport);
            QCOMPARE(result.statusCode, 0);
            QTest::qWait(25); // let the server read anything that was sent
            QVERIFY2(server.received.isEmpty(), "a refused peer received request bytes");
        }
    }
    if (accepted)
        QCOMPARE(server.connections - connectionsBefore, 1); // keep-alive survives the policy
}

// The post-request chain check, for a connection the manager pooled before
// HttpClient's handshake check could see it. Qt 5.15 reuses that connection
// (the request is sent, the answer is discarded); Qt 6 opens a new one.
void TlsKeyPolicyTest::aWeakConnectionPooledBeforeTheClientIsStillRefused()
{
    const Fixture& fixture = m_fixtures.at(QStringLiteral("rsa2047"));
    inheritWeakDefaults(fixture.trust);
    TlsServer server;
    server.tls = serverConfig(fixture, QSsl::TlsV1_2, false);
    QVERIFY(server.listen(QHostAddress::LocalHost));
    const QUrl url = urlFor(server);

    QNetworkAccessManager manager;
    {
        QNetworkRequest request(url);
        std::unique_ptr<QNetworkReply> reply(manager.get(request));
        QSignalSpy finished(reply.get(), &QNetworkReply::finished);
        QVERIFY(finished.wait(7000));
        QCOMPARE(reply->error(), QNetworkReply::NoError);
    }
    HttpClient client(manager, 5000);
    const HttpClient::HttpResult result = client.get(url, {});
    QVERIFY(result.error.has_value());
    QCOMPARE(*result.error, NetworkError::Transport);
    QVERIFY(result.body.isEmpty());
    QVERIFY(result.peerSpkiSha256.isEmpty());
    qInfo("connections after the pooled attempt: %d", server.connections);
}

void TlsKeyPolicyTest::anchorsBelowTheFloorAreDropped()
{
    const QSslCertificate weak = m_fixtures.at(QStringLiteral("root2047")).trust.constFirst();
    const QSslCertificate strong = m_fixtures.at(QStringLiteral("rsa2048")).trust.constFirst();
    QNetworkRequest request(QUrl(QStringLiteral("https://127.0.0.1/")));
    QSslConfiguration tls = request.sslConfiguration();
    tls.setCaCertificates({ weak, strong });
    request.setSslConfiguration(tls);
    applyStrongTlsPolicy(request);
    QCOMPARE(request.sslConfiguration().caCertificates(), QList<QSslCertificate>{ strong });

    // An explicitly empty trust list never falls back to anything.
    tls.setCaCertificates({});
    request.setSslConfiguration(tls);
    applyStrongTlsPolicy(request);
    QVERIFY(request.sslConfiguration().caCertificates().isEmpty());
}

void TlsKeyPolicyTest::certificateKeyTypes()
{
    QVERIFY(!tlsCertificateKeyAllowed({}));
    QVERIFY(!tlsCertificateKeyAllowed(QByteArrayLiteral("not a certificate")));
    QVERIFY(!tlsChainKeysAllowed({}));
    const QByteArray strong = m_fixtures.at(QStringLiteral("rsa2048")).served.constFirst().toDer();
    QVERIFY(tlsCertificateKeyAllowed(strong));
    QVERIFY(!tlsCertificateKeyAllowed(strong + QByteArrayLiteral("trailing")));
    for (const char* name : { "rsa1024", "rsa2047", "ec192" })
        QVERIFY2(!tlsCertificateKeyAllowed(m_fixtures.at(QString::fromLatin1(name)).served.constFirst().toDer()),
                 name);
    QVERIFY(tlsCertificateKeyAllowed(m_fixtures.at(QStringLiteral("ec256")).served.constFirst().toDer()));
    // Key types Qt's QSslKey cannot represent, decoded through OpenSSL instead.
    for (const char* type : { "RSA-PSS", "ED25519", "ED448" }) {
        const Pkey key = namedKey(type);
        QVERIFY2(key, type);
        QVERIFY2(tlsCertificateKeyAllowed(qtCert(issue(key, "typed", nullptr, false).cert).toDer()), type);
    }
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 1, 0)
void TlsKeyPolicyTest::httpsIsRefusedWithoutTheOpenSslBackend()
{
    // A fresh process has no TLS plugin loaded; remove the plugin path there.
    QProcess child;
    child.start(QCoreApplication::applicationFilePath(), { QStringLiteral("--without-tls") });
    QVERIFY(child.waitForFinished(15000));
    QVERIFY2(child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0,
             child.readAllStandardError().constData());
}
#endif

int main(int argc, char** argv)
{
    // Weak enough for the server side of the negative fixtures; HttpClient must
    // override this inherited process-wide default before sending anything.
    QTemporaryDir configuration;
    QFile file(configuration.filePath(QStringLiteral("openssl.cnf")));
    if (!file.open(QIODevice::WriteOnly)
        || file.write("openssl_conf=init\n[init]\nssl_conf=tls\n[tls]\nsystem_default=weak\n"
                      "[weak]\nCipherString=ALL:@SECLEVEL=0\nMinProtocol=TLSv1\n")
            < 0)
        return 2;
    file.close();
    qputenv("OPENSSL_CONF", QFile::encodeName(file.fileName()));

    QCoreApplication app(argc, argv);
    if (app.arguments().contains(QStringLiteral("--without-tls"))) {
        QTemporaryDir empty;
        QCoreApplication::setLibraryPaths({ empty.path() });
        QNetworkAccessManager manager;
        HttpClient client(manager, 2000);
        const HttpClient::HttpResult result = client.get(QUrl(QStringLiteral("https://127.0.0.1:9/")), {});
        return result.error == NetworkError::Transport && result.detail.contains(QStringLiteral("OpenSSL 3")) ? 0 : 1;
    }
    TlsKeyPolicyTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "TlsKeyPolicyTest.moc"
