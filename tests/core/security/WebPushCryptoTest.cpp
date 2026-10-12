#include "security/WebPushCrypto.h"

#include <QTest>

using namespace WebPushCrypto;

namespace {

QByteArray b64(const char* s)
{
    return QByteArray::fromBase64(s, QByteArray::Base64UrlEncoding);
}

// RFC 8291 section 5 / appendix A.
Keys rfcReceiver()
{
    return { SecureBytes(b64("q1dXpw3UpT5VOmu_cf_v6ih07Aems3njxI-JWgLcM94")),
             b64("BCVxsr7N_eNgVRqvHtD0zTZsEc6-VV-JvLexhqUzORcxaOzi6-AYWXvTBHm4bjyPjs7Vd8pZGH6SRpkNtoIAiw4"),
             SecureBytes(b64("BTBZMqHH6r4Tts7J_aSIgg")) };
}

QByteArray rfcBody()
{
    return b64("DGv6ra1nlYgDCS1FRnbzlwAAEABBBP4z9KsN6nGRTbVYI_c7VJSPQTBtkgcy27mlmlMoZIIgDll6e3vCYLocInmYWA"
               "mS6TlzAC8wEqKK6PBru3jl7A_yl95bQpu6cVPTpK4Mqgkf1CXztLVBSt2Ks3oZwbuwXPXLWyouBWLVWGNWQexSgS"
               "xsj_Qulcy4a-fN");
}

Keys fresh()
{
    std::optional<Keys> keys = generate();
    if (!keys)
        qFatal("key generation failed");
    return std::move(*keys);
}

} // namespace

class WebPushCryptoTest : public QObject
{
    Q_OBJECT

private slots:
    void rfc8291Vector()
    {
        QCOMPARE(decrypt(rfcReceiver(), rfcBody()), std::optional<QByteArray>("When I grow up, I want to be a watermelon"));
    }

    void tamperingAndWrongKeysAreRefused()
    {
        QByteArray body = rfcBody();
        body[body.size() - 1] = char(body.at(body.size() - 1) ^ 1);
        QVERIFY(!decrypt(rfcReceiver(), body));

        Keys wrongAuth = rfcReceiver();
        wrongAuth.authSecret = SecureBytes(QByteArray(kAuthBytes, 'x'));
        QVERIFY(!decrypt(wrongAuth, rfcBody()));
        QVERIFY(!decrypt(fresh(), rfcBody()));

        QByteArray offCurve = rfcBody();
        offCurve[30] = char(offCurve.at(30) ^ 1); // sender public key
        QVERIFY(!decrypt(rfcReceiver(), offCurve));
        QByteArray salt = rfcBody();
        salt[0] = char(salt.at(0) ^ 1);
        QVERIFY(!decrypt(rfcReceiver(), salt));
    }

    void malformedHeadersAreRefused()
    {
        QVERIFY(!decrypt(rfcReceiver(), {}));
        QVERIFY(!decrypt(rfcReceiver(), rfcBody().left(90)));
        QByteArray idlen = rfcBody();
        idlen[20] = 64;
        QVERIFY(!decrypt(rfcReceiver(), idlen));
    }

    void relayShapedBodyRoundTrips()
    {
        // webpush-go's default: rs 4096, whole body padded to 4096.
        const Keys receiver = fresh();
        QCOMPARE(receiver.privateKey.bytes().size(), kPrivateKeyBytes);
        QCOMPARE(receiver.publicKey.size(), kPublicKeyBytes);
        QCOMPARE(receiver.publicKey.at(0), '\x04');
        QCOMPARE(receiver.authSecret.bytes().size(), kAuthBytes);

        const QByteArray json = R"({"title":"t","data":{"messageId":"m"}})";
        const QByteArray body = encrypt(receiver.publicKey, receiver.authSecret.bytes(), json);
        QCOMPARE(body.size(), kMaxBodyBytes);
        QCOMPARE(decrypt(receiver, body), std::optional<QByteArray>(json));
        QVERIFY(generate()->publicKey != receiver.publicKey);
    }

    void oversizeBodiesAreRefusedUnread()
    {
        const Keys receiver = fresh();
        const QByteArray body = encrypt(receiver.publicKey, receiver.authSecret.bytes(), "x", kMaxBodyBytes + 1);
        QCOMPARE(body.size(), kMaxBodyBytes + 1);
        QVERIFY(!decrypt(receiver, body));
    }

    void aRecordLongerThanItsDeclaredSizeIsRefused()
    {
        // rs is not authenticated; only the single-record check catches a lie in it.
        const Keys receiver = fresh();
        QByteArray body = encrypt(receiver.publicKey, receiver.authSecret.bytes(), "x", 200);
        QVERIFY(decrypt(receiver, body));
        body[19] = char(100);
        QVERIFY(!decrypt(receiver, body));
    }
};

QTEST_GUILESS_MAIN(WebPushCryptoTest)
#include "WebPushCryptoTest.moc"
