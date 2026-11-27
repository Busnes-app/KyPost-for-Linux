#include "pgp/GoPgpBackend.h"
#include "pgp/OpenPgpDecryptor.h"
#include "pgp/OpenPgpEncryptor.h"
#include "GnupgFixture.h"
#include "pgp/OpenPgpKeyImporter.h"
#include "stores/SecureStore.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QProcess>
#include <QScopeGuard>
#include <QTest>
#include <functional>

// Only public test secrets; no real Secret Service or user GNUPGHOME touched.
class BinaryStore : public SecureStore
{
  public:
    QMap<QString, QString> text;
    QMap<QString, QByteArray> binary;
    bool refuse = false;
    bool corruptWrites = false;
    std::function<void(const QString&)> onBeforeSet;
    bool set(const QString& key, const QString& value) override
    {
        if (onBeforeSet)
            onBeforeSet(key);
        text[key] = value;
        return true;
    }
    std::optional<QString> get(const QString& key) const override
    {
        return text.contains(key) ? std::optional<QString>(text[key]) : std::nullopt;
    }
    bool remove(const QString& key) override
    {
        text.remove(key);
        binary.remove(key);
        return true;
    }
    bool contains(const QString& key) const override
    {
        return text.contains(key) || binary.contains(key);
    }
    bool setBytes(const QString& key, const QByteArray& value) override
    {
        if (refuse)
            return false;
        binary[key] = value;
        if (corruptWrites && !binary[key].isEmpty())
            binary[key][0] = char(binary[key].at(0) ^ 1);
        return true;
    }
    BytesResult readBytes(const QString& key) const override
    {
        if (refuse)
            return {};
        if (!binary.contains(key))
            return {ReadStatus::Absent, {}};
        return {ReadStatus::Found, SecureBytes(QByteArray(binary[key].constData(), binary[key].size()))};
    }
};
class GoPgpBackendTest : public QObject
{
    Q_OBJECT
  private slots:
    void readsAndVerifiesSharedFixtures();
    void importsV2OnlyAfterCheckingTheWholeSnapshot();
    void singleKeyCannotSatisfyConvertedSnapshot();
    void mixedV4EnrollmentPreservesSigning();
    void classicalRevocationInvalidatesRetainedMixedV4Key();
    void unknownKeyV6DetachedSignaturePreservesCheckedResult();
    void corruptedGoStoreFailsClosedWithoutGpgFallback();
    void reentrantImportsPreserveBothIndexedKeys();
};
static QByteArray fixture(const char* name)
{
    const QString path = QStringLiteral(KYPOST_GO_TESTDATA) + QLatin1Char('/') + QLatin1String(name);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}
void GoPgpBackendTest::readsAndVerifiesSharedFixtures()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto home = GnupgFixture::emptyHome(directory);
    const auto stopAgent = qScopeGuard([&] { GnupgFixture::killAgent(home); });
    BinaryStore store;
    setGoPgpSecretStore(&store);
    const auto clear = qScopeGuard([] { setGoPgpSecretStore(nullptr); });
    for (const auto* name : {"v6-pqc", "v4-pqc"})
    {
        auto key = fixture(qPrintable(QStringLiteral("openpgp-v6/%1.sec.asc").arg(QLatin1String(name))));
        QVERIFY(!key.isEmpty());
        const auto info = goKeyInfo(key);
        const auto fpr = info.value("fingerprint").toString();
        QCOMPARE(importPrivateKey(SecureBytes(key), fpr, home).status, PgpImportStatus::Imported);
        const auto cipher =
            fixture(qPrintable(QStringLiteral("openpgp-v6/%1.msg.asc").arg(QLatin1String(name))));
        const auto opened = OpenPgpDecryptor().decrypt(cipher, home);
        QCOMPARE(opened.status, PgpDecryptStatus::Decrypted);
        QCOMPARE(opened.plaintext, fixture("openpgp-v6/message.txt"));
        QVERIFY(goPgpOwnKey(fpr, home));
        QCOMPARE(publicKeyFingerprint(info.value("publicKey").toString().toUtf8()), fpr);
        // A public-key refresh must not overwrite the private key.
        QVERIFY(storeGoPgpKey(info.value("publicKey").toString().toUtf8(), fpr, home));
        QVERIFY(goPgpOwnKey(fpr, home));
    }
    auto signedBytes = fixture("openpgp-v6/message.txt");
    const auto signature = fixture("openpgp-v6/v6-pqc.sig.asc");
    const auto verified = OpenPgpDecryptor().verifyDetached(signedBytes, signature, home);
    QCOMPARE(verified.status, PgpVerifyStatus::Checked);
    QVERIFY(verified.signature.mathematicallyValid);
    QVERIFY(!verified.signature.belowStrengthFloor);
    QVERIFY(!verified.signature.primaryFingerprint.isEmpty());
    signedBytes[0] = char(signedBytes.at(0) ^ 1);
    QVERIFY(!OpenPgpDecryptor()
                 .verifyDetached(signedBytes, signature, home)
                 .signature.mathematicallyValid);
    store.refuse = true;
    QVERIFY(loadGoPgpKeys(home).isEmpty());
}
void GoPgpBackendTest::importsV2OnlyAfterCheckingTheWholeSnapshot()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto home = GnupgFixture::emptyHome(directory);
    const auto stopAgent = qScopeGuard([&] { GnupgFixture::killAgent(home); });
    BinaryStore store;
    setGoPgpSecretStore(&store);
    const auto clear = qScopeGuard([] { setGoPgpSecretStore(nullptr); });
    const auto key = fixture("openpgp-v6/v6-pqc.sec.asc");
    const auto info = goKeyInfo(key);
    const auto fpr = info.value("fingerprint").toString();
    PgpKeyringSnapshot expected;
    expected.activeFingerprint = fpr;
    expected.activeV6 = fpr;
    expected.materialGeneration = 2;
    for (const auto& fp : info.value("fingerprints").toArray())
        expected.keyFingerprints << fp.toString();
    QJsonObject ring{
        {"format", "kypost-pgp-keyring-v2"},
        {"materialGeneration", 2},
        {"active", QJsonObject{{"v6", fpr}}},
        {"keyFingerprints", info.value("fingerprints")},
        {"keys", QJsonArray{QJsonObject{{"fingerprint", fpr}, {"privateKey", QString::fromUtf8(key)}}}}};
    // Positive generation requires the complete ring, even if one valid
    // private key is delivered instead. It must not reach durable storage.
    QCOMPARE(importDeviceDelivery(6, SecureBytes(key), expected, home),
             PgpKeyringImportStatus::Rejected);
    QVERIFY(store.binary.isEmpty());
    auto wrong = expected;
    wrong.materialGeneration++;
    QCOMPARE(importDeviceDelivery(6, SecureBytes(QJsonDocument(ring).toJson()), wrong, home),
             PgpKeyringImportStatus::Rejected);
    QVERIFY(store.binary.isEmpty());
    QCOMPARE(importDeviceDelivery(6, SecureBytes(QJsonDocument(ring).toJson()), expected, home),
             PgpKeyringImportStatus::Imported);
    // No process-local key cache: a fresh store object can reopen the persisted
    // entries.
    BinaryStore restarted;
    restarted.binary = store.binary;
    restarted.text = store.text;
    setGoPgpSecretStore(&restarted);
    QCOMPARE(OpenPgpDecryptor().decrypt(fixture("openpgp-v6/v6-pqc.msg.asc"), home).status,
             PgpDecryptStatus::Decrypted);
    setGoPgpSecretStore(&store);
    auto largeRing = QJsonDocument(ring).toJson();
    largeRing.append(QByteArray(200 * 1024, ' '));
    QCOMPARE(importDeviceDelivery(6, SecureBytes(largeRing), expected, home),
             PgpKeyringImportStatus::Imported);
    QCOMPARE(importDeviceDelivery(6, SecureBytes(QByteArray(512 * 1024 + 1, ' ')), expected, home),
             PgpKeyringImportStatus::Rejected);
    // Dual active certificates bind to a v4 bootstrap without dropping v6.
    auto dual = ring;
    const auto historical = fixture("openpgp-v6/v4-pqc.sec.asc");
    const auto historicalInfo = goKeyInfo(historical);
    const auto v4 = historicalInfo.value("fingerprint").toString();
    auto dualExpected = expected;
    dualExpected.activeFingerprint = v4;
    dualExpected.activeV4 = v4;
    auto inventory = info.value("fingerprints").toArray();
    for (const auto& f : historicalInfo.value("fingerprints").toArray())
    {
        inventory.append(f);
        dualExpected.keyFingerprints.append(f.toString());
    }
    dual["active"] = QJsonObject{{"v4", v4}, {"v6", fpr}};
    dual["keyFingerprints"] = inventory;
    auto members = dual["keys"].toArray();
    members.append(QJsonObject{{"fingerprint", v4}, {"privateKey", QString::fromUtf8(historical)}});
    dual["keys"] = members;
    QCOMPARE(importDeviceDelivery(6, SecureBytes(QJsonDocument(dual).toJson()), dualExpected, home),
             PgpKeyringImportStatus::Imported);
    QCOMPARE(OpenPgpDecryptor().decrypt(fixture("openpgp-v6/v4-pqc.msg.asc"), home).status,
             PgpDecryptStatus::Decrypted);
    auto missingSelection = dualExpected;
    missingSelection.activeFingerprint.clear();
    QCOMPARE(
        importDeviceDelivery(6, SecureBytes(QJsonDocument(dual).toJson()), missingSelection, home),
        PgpKeyringImportStatus::Rejected);
    BinaryStore broken;
    broken.corruptWrites = true;
    setGoPgpSecretStore(&broken);
    QCOMPARE(importDeviceDelivery(6, SecureBytes(QJsonDocument(ring).toJson()), expected, "broken-home"),
             PgpKeyringImportStatus::ImportFailed);
    QVERIFY(broken.text.isEmpty()); // Unverified writes never reach the durable index.
    setGoPgpSecretStore(&store);
    const auto bytes = store.binary;
    QCOMPARE(importDeviceDelivery(6, SecureBytes(QJsonDocument(ring).toJson()), expected, home,
                                  [] { return false; }),
             PgpKeyringImportStatus::Cancelled);
    QCOMPARE(store.binary, bytes);
    store.refuse = true;
    QCOMPARE(importDeviceDelivery(6, SecureBytes(QJsonDocument(ring).toJson()), expected, home),
             PgpKeyringImportStatus::ImportFailed);
    store.refuse = false;
    ring["active"] = QJsonObject{{"v4", fpr}};
    QCOMPARE(importDeviceDelivery(6, SecureBytes(QJsonDocument(ring).toJson()), expected, home),
             PgpKeyringImportStatus::Rejected);
}
void GoPgpBackendTest::singleKeyCannotSatisfyConvertedSnapshot()
{
    BinaryStore store;
    setGoPgpSecretStore(&store);
    const auto clear = qScopeGuard([] { setGoPgpSecretStore(nullptr); });
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto home = GnupgFixture::emptyHome(directory);
    const auto stopAgent = qScopeGuard([&] { GnupgFixture::killAgent(home); });
    const auto active = fixture("openpgp-v6/v4-pqc.sec.asc");
    const auto historical = fixture("openpgp-v6/v6-pqc.sec.asc");
    const auto activeInfo = goKeyInfo(active);
    const auto historicalInfo = goKeyInfo(historical);
    PgpKeyringSnapshot expected;
    expected.activeFingerprint = activeInfo.value("fingerprint").toString();
    expected.activeV4 = expected.activeFingerprint;
    expected.activeV6 = historicalInfo.value("fingerprint").toString();
    expected.materialGeneration = 7;
    for (const auto& info : {activeInfo, historicalInfo})
        for (const auto& fp : info.value("fingerprints").toArray())
            expected.keyFingerprints << fp.toString();
    QCOMPARE(importDeviceDelivery(6, SecureBytes(active), expected, home),
             PgpKeyringImportStatus::Rejected);
    QVERIFY(store.binary.isEmpty());
    QVERIFY(store.text.isEmpty());
    QVERIFY(GnupgFixture::fingerprintsIn(home).isEmpty());
}

void GoPgpBackendTest::mixedV4EnrollmentPreservesSigning()
{
    BinaryStore store;
    setGoPgpSecretStore(&store);
    const auto clear = qScopeGuard([] { setGoPgpSecretStore(nullptr); });
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto home = GnupgFixture::emptyHome(directory);
    const auto stopAgent = qScopeGuard([&] { GnupgFixture::killAgent(home); });
    const auto key = fixture("openpgp-v6/v4-pqc.sec.asc");
    const auto info = goKeyInfo(key);
    const auto fpr = info.value("fingerprint").toString();
    PgpKeyringSnapshot expected;
    expected.activeFingerprint = fpr;
    expected.activeV4 = fpr;
    expected.materialGeneration = 2;
    for (const auto& fp : info.value("fingerprints").toArray())
        expected.keyFingerprints << fp.toString();
    const QJsonObject ring{
        {"format", "kypost-pgp-keyring-v2"}, {"materialGeneration", 2},
        {"active", QJsonObject{{"v4", fpr}}},
        {"keyFingerprints", info.value("fingerprints")},
        {"keys", QJsonArray{QJsonObject{{"fingerprint", fpr}, {"privateKey", QString::fromUtf8(key)}}}}};
    QCOMPARE(importDeviceDelivery(6, SecureBytes(QJsonDocument(ring).toJson()), expected, home),
             PgpKeyringImportStatus::Imported);
    QCOMPARE(ownKeyFingerprint("v4-pqc@example.org", home), fpr);
    QTemporaryDir recipientDirectory;
    QVERIFY(recipientDirectory.isValid());
    const auto recipientHome = GnupgFixture::emptyHome(recipientDirectory);
    const auto stopRecipient = qScopeGuard([&] { GnupgFixture::killAgent(recipientHome); });
    QCOMPARE(importPublicKey(info.value("publicKey").toString().toUtf8(), fpr, recipientHome).status,
             PgpImportStatus::Imported);
    QVERIFY(GnupgFixture::fingerprintsIn(recipientHome).contains(fpr));
    const auto sealed = signAndEncrypt("enrollment signing regression", fpr, {fpr}, home);
    QCOMPARE(sealed.status, PgpEncryptStatus::Encrypted);
    const auto opened = OpenPgpDecryptor().decrypt(sealed.armoredCiphertext.toUtf8(), home);
    QCOMPARE(opened.status, PgpDecryptStatus::Decrypted);
    QCOMPARE(opened.plaintext, QByteArray("enrollment signing regression"));
    QVERIFY(opened.signature.mathematicallyValid);
    QCOMPARE(OpenPgpDecryptor().decrypt(fixture("openpgp-v6/v4-pqc.msg.asc"), home).status,
             PgpDecryptStatus::Decrypted);
}

void GoPgpBackendTest::classicalRevocationInvalidatesRetainedMixedV4Key()
{
    BinaryStore store;
    setGoPgpSecretStore(&store);
    const auto clear = qScopeGuard([] { setGoPgpSecretStore(nullptr); });
    QTemporaryDir signerDir, verifierDir;
    QVERIFY(signerDir.isValid() && verifierDir.isValid());
    const auto signerHome = GnupgFixture::emptyHome(signerDir);
    const auto verifierHome = GnupgFixture::emptyHome(verifierDir);
    const auto stopSigner = qScopeGuard([&] { GnupgFixture::killAgent(signerHome); });
    const auto stopVerifier = qScopeGuard([&] { GnupgFixture::killAgent(verifierHome); });

    const auto secretKey = fixture("openpgp-v6/v4-pqc.sec.asc");
    const auto info = goKeyInfo(secretKey);
    const auto fpr = info.value("fingerprint").toString();
    const auto publicKey = info.value("publicKey").toString().toUtf8();
    QVERIFY(info.value("modern").toBool());
    QCOMPARE(importPrivateKey(SecureBytes(secretKey), fpr, signerHome).status, PgpImportStatus::Imported);
    QCOMPARE(importPublicKey(publicKey, fpr, verifierHome).status, PgpImportStatus::Imported);

    auto runGpg = [](const QString& home, const QStringList& args, const QByteArray& stdinData = {}) {
        QProcess gpg;
        gpg.start(QStringLiteral("gpg"),
                  QStringList{QStringLiteral("--homedir"), home, QStringLiteral("--batch"),
                              QStringLiteral("--yes"), QStringLiteral("--no-tty")} + args);
        if (!gpg.waitForStarted(10000))
            return QPair<int, QByteArray>{-1, {}};
        if (!stdinData.isEmpty())
            gpg.write(stdinData);
        gpg.closeWriteChannel();
        if (!gpg.waitForFinished(15000))
            return QPair<int, QByteArray>{-2, {}};
        return QPair<int, QByteArray>{gpg.exitCode(), gpg.readAllStandardOutput()};
    };

    const QByteArray body("revocation regression payload");
    const auto signedResult = runGpg(
        signerHome,
        {QStringLiteral("--armor"), QStringLiteral("--detach-sign"), QStringLiteral("-u"), fpr}, body);
    QCOMPARE(signedResult.first, 0);
    const QByteArray signature = signedResult.second;
    QVERIFY(!signature.isEmpty());

    const auto before = OpenPgpDecryptor().verifyDetached(body, signature, verifierHome);
    QCOMPARE(before.status, PgpVerifyStatus::Checked);
    QVERIFY(before.signature.present);
    QVERIFY(before.signature.mathematicallyValid);
    QVERIFY(!before.signature.keyUnavailable);

    // Revoke the primary key in GnuPG and export its classical-only certificate
    // (GnuPG drops the PQC subkey, so goKeyInfo().modern is false).
    QProcess genRevoke;
    genRevoke.start(QStringLiteral("gpg"),
                    {QStringLiteral("--homedir"), signerHome, QStringLiteral("--yes"),
                     QStringLiteral("--no-tty"), QStringLiteral("--command-fd"), QStringLiteral("0"),
                     QStringLiteral("--status-fd"), QStringLiteral("2"), QStringLiteral("--pinentry-mode"),
                     QStringLiteral("loopback"), QStringLiteral("--passphrase"), QString(),
                     QStringLiteral("--gen-revoke"), fpr});
    QVERIFY(genRevoke.waitForStarted(10000));
    genRevoke.write("y\n1\n\ny\n");
    genRevoke.closeWriteChannel();
    QVERIFY(genRevoke.waitForFinished(15000));
    QCOMPARE(genRevoke.exitCode(), 0);
    const QByteArray revokeCert = genRevoke.readAllStandardOutput();
    QVERIFY(!revokeCert.isEmpty());
    QCOMPARE(runGpg(signerHome, {QStringLiteral("--import")}, revokeCert).first, 0);
    const auto exportedRevoked = runGpg(signerHome, {QStringLiteral("--armor"), QStringLiteral("--export"), fpr});
    QCOMPARE(exportedRevoked.first, 0);
    const QByteArray classicalRevoked = exportedRevoked.second;
    const auto revokedInfo = goKeyInfo(classicalRevoked);
    QVERIFY(!revokedInfo.value("modern").toBool());
    QVERIFY(revokedInfo.value("revoked").toBool());

    const auto refresh = importPublicKey(classicalRevoked, fpr, verifierHome);
    QVERIFY(refresh.status == PgpImportStatus::Imported || refresh.status == PgpImportStatus::Unchanged);

    const auto after = OpenPgpDecryptor().verifyDetached(body, signature, verifierHome);
    QCOMPARE(after.status, PgpVerifyStatus::Checked);
    QVERIFY(after.signature.present);
    QVERIFY(!after.signature.keyUnavailable);
    QVERIFY(!after.signature.mathematicallyValid);
}

void GoPgpBackendTest::unknownKeyV6DetachedSignaturePreservesCheckedResult()
{
    BinaryStore store;
    setGoPgpSecretStore(&store);
    const auto clear = qScopeGuard([] { setGoPgpSecretStore(nullptr); });
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto home = GnupgFixture::emptyHome(directory);
    const auto stopAgent = qScopeGuard([&] { GnupgFixture::killAgent(home); });

    // Unknown-key v6 detached signature must stay Checked + keyUnavailable even
    // when GnuPG cannot parse v6 signatures (both with an empty Go store and
    // with an unrelated retained key).
    const auto body = fixture("openpgp-v6/message.txt");
    const auto v6Sig = fixture("openpgp-v6/v6-pqc.sig.asc");
    QVERIFY(!body.isEmpty() && !v6Sig.isEmpty());

    const auto emptyStoreResult = OpenPgpDecryptor().verifyDetached(body, v6Sig, home);
    QCOMPARE(emptyStoreResult.status, PgpVerifyStatus::Checked);
    QVERIFY(emptyStoreResult.signature.present);
    QVERIFY(emptyStoreResult.signature.keyUnavailable);
    QVERIFY(!emptyStoreResult.signature.mathematicallyValid);

    const auto unrelated = fixture("openpgp-v6/v4-pqc.sec.asc");
    const auto unrelatedFpr = goKeyInfo(unrelated).value("fingerprint").toString();
    QVERIFY(storeGoPgpKey(unrelated, unrelatedFpr, home));

    const auto populatedStoreResult = OpenPgpDecryptor().verifyDetached(body, v6Sig, home);
    QCOMPARE(populatedStoreResult.status, PgpVerifyStatus::Checked);
    QVERIFY(populatedStoreResult.signature.present);
    QVERIFY(populatedStoreResult.signature.keyUnavailable);
    QVERIFY(!populatedStoreResult.signature.mathematicallyValid);
}

void GoPgpBackendTest::corruptedGoStoreFailsClosedWithoutGpgFallback()
{
    BinaryStore store;
    setGoPgpSecretStore(&store);
    const auto clear = qScopeGuard([] { setGoPgpSecretStore(nullptr); });
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto home = GnupgFixture::emptyHome(directory);
    const auto stopAgent = qScopeGuard([&] { GnupgFixture::killAgent(home); });

    const auto body = fixture("openpgp-v6/message.txt");
    const auto v6Sig = fixture("openpgp-v6/v6-pqc.sig.asc");
    const auto v6Ciphertext = fixture("openpgp-v6/v6-pqc.msg.asc");
    const auto v4Pub = fixture("openpgp-v6/v4-pqc.pub.asc");
    QVERIFY(!body.isEmpty() && !v6Sig.isEmpty() && !v6Ciphertext.isEmpty() && !v4Pub.isEmpty());

    const QString fpr = goKeyInfo(v4Pub).value("fingerprint").toString();
    QVERIFY(!fpr.isEmpty());
    QCOMPARE(importPublicKey(v4Pub, fpr, home).status, PgpImportStatus::Imported);

    // Locked / unreadable secret store must fail closed (EngineUnavailable) on
    // both verifyDetached and decrypt rather than silently falling back to GnuPG.
    store.refuse = true;
    const auto lockedVerify = OpenPgpDecryptor().verifyDetached(body, v6Sig, home);
    QCOMPARE(lockedVerify.status, PgpVerifyStatus::EngineUnavailable);
    const auto lockedDecrypt = OpenPgpDecryptor().decrypt(v6Ciphertext, home);
    QCOMPARE(lockedDecrypt.status, PgpDecryptStatus::EngineUnavailable);

    // Missing referenced key blob must likewise fail closed.
    store.refuse = false;
    QVERIFY(!store.binary.isEmpty());
    const QString blobKey = store.binary.firstKey();
    store.binary.clear();
    const auto missingBlobVerify = OpenPgpDecryptor().verifyDetached(body, v6Sig, home);
    QCOMPARE(missingBlobVerify.status, PgpVerifyStatus::EngineUnavailable);
    const auto missingBlobDecrypt = OpenPgpDecryptor().decrypt(v6Ciphertext, home);
    QCOMPARE(missingBlobDecrypt.status, PgpDecryptStatus::EngineUnavailable);

    // Empty blob returned as Found must fail closed (EngineUnavailable), not fall back to GnuPG.
    store.binary[blobKey] = QByteArray{};
    const auto emptyBlobVerify = OpenPgpDecryptor().verifyDetached(body, v6Sig, home);
    QCOMPARE(emptyBlobVerify.status, PgpVerifyStatus::EngineUnavailable);
    const auto emptyBlobDecrypt = OpenPgpDecryptor().decrypt(v6Ciphertext, home);
    QCOMPARE(emptyBlobDecrypt.status, PgpDecryptStatus::EngineUnavailable);

    // Malformed / tampered blob returned as Found must fail SHA-256 verification and fail closed.
    store.binary[blobKey] = QByteArray("corrupted-openpgp-blob-payload");
    const auto malformedBlobVerify = OpenPgpDecryptor().verifyDetached(body, v6Sig, home);
    QCOMPARE(malformedBlobVerify.status, PgpVerifyStatus::EngineUnavailable);
    const auto malformedBlobDecrypt = OpenPgpDecryptor().decrypt(v6Ciphertext, home);
    QCOMPARE(malformedBlobDecrypt.status, PgpDecryptStatus::EngineUnavailable);
}

void GoPgpBackendTest::reentrantImportsPreserveBothIndexedKeys()
{
    BinaryStore store;
    setGoPgpSecretStore(&store);
    const auto clear = qScopeGuard([] { setGoPgpSecretStore(nullptr); });
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto home = GnupgFixture::emptyHome(directory);
    const auto stopAgent = qScopeGuard([&] { GnupgFixture::killAgent(home); });

    const auto v4Sec = fixture("openpgp-v6/v4-pqc.sec.asc");
    const auto v6Sec = fixture("openpgp-v6/v6-pqc.sec.asc");
    QVERIFY(!v4Sec.isEmpty() && !v6Sec.isEmpty());
    const QString v4Fpr = goKeyInfo(v4Sec).value("fingerprint").toString();
    const QString v6Fpr = goKeyInfo(v6Sec).value("fingerprint").toString();
    QVERIFY(!v4Fpr.isEmpty() && !v6Fpr.isEmpty());

    // Simulate a nested Qt event loop inside SecretStore::set("...index") where
    // a second import commits completely before the outer set() overwrites the
    // index with its earlier snapshot.
    bool injected = false;
    bool innerOk = false;
    store.onBeforeSet = [&](const QString& key) {
        if (injected || !key.endsWith(QStringLiteral(".index")))
            return;
        injected = true;
        innerOk = storeGoPgpKey(v6Sec, v6Fpr, home);
    };

    QVERIFY(storeGoPgpKey(v4Sec, v4Fpr, home));
    QVERIFY(injected);
    QVERIFY(innerOk);

    // Both keys must remain indexed and usable after the overlapping writes.
    QVERIFY(goPgpOwnKey(v4Fpr, home));
    QVERIFY(goPgpOwnKey(v6Fpr, home));
    const auto v4Msg = OpenPgpDecryptor().decrypt(fixture("openpgp-v6/v4-pqc.msg.asc"), home);
    const auto v6Msg = OpenPgpDecryptor().decrypt(fixture("openpgp-v6/v6-pqc.msg.asc"), home);
    QCOMPARE(v4Msg.status, PgpDecryptStatus::Decrypted);
    QCOMPARE(v6Msg.status, PgpDecryptStatus::Decrypted);
}

QTEST_GUILESS_MAIN(GoPgpBackendTest)
#include "GoPgpBackendTest.moc"
