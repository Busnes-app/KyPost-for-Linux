#include "pgp/GoPgpBackend.h"
#include "pgp/OpenPgpDecryptor.h"
#include "pgp/OpenPgpKeyImporter.h"
#include "stores/SecureStore.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QScopeGuard>
#include <QTest>

// Only public test secrets; no real Secret Service or user GNUPGHOME touched.
class BinaryStore : public SecureStore
{
  public:
    QMap<QString, QString> text;
    QMap<QString, QByteArray> binary;
    bool refuse = false;
    bool corruptWrites = false;
    bool set(const QString& key, const QString& value) override
    {
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
    BinaryStore store;
    setGoPgpSecretStore(&store);
    const auto clear = qScopeGuard([] { setGoPgpSecretStore(nullptr); });
    for (const auto* name : {"v6-pqc", "v4-pqc"})
    {
        auto key = fixture(qPrintable(QStringLiteral("openpgp-v6/%1.sec.asc").arg(QLatin1String(name))));
        QVERIFY(!key.isEmpty());
        const auto info = goKeyInfo(key);
        const auto fpr = info.value("fingerprint").toString();
        QCOMPARE(importPrivateKey(SecureBytes(key), fpr, "fixture-home").status, PgpImportStatus::Imported);
        const auto cipher =
            fixture(qPrintable(QStringLiteral("openpgp-v6/%1.msg.asc").arg(QLatin1String(name))));
        const auto opened = OpenPgpDecryptor().decrypt(cipher, "fixture-home");
        QCOMPARE(opened.status, PgpDecryptStatus::Decrypted);
        QCOMPARE(opened.plaintext, fixture("openpgp-v6/message.txt"));
        QVERIFY(goPgpOwnKey(fpr, "fixture-home"));
        QCOMPARE(publicKeyFingerprint(info.value("publicKey").toString().toUtf8()), fpr);
        // A public-key refresh must not overwrite the private key.
        QVERIFY(storeGoPgpKey(info.value("publicKey").toString().toUtf8(), fpr, "fixture-home"));
        QVERIFY(goPgpOwnKey(fpr, "fixture-home"));
    }
    auto signedBytes = fixture("openpgp-v6/message.txt");
    const auto signature = fixture("openpgp-v6/v6-pqc.sig.asc");
    const auto verified = OpenPgpDecryptor().verifyDetached(signedBytes, signature, "fixture-home");
    QCOMPARE(verified.status, PgpVerifyStatus::Checked);
    QVERIFY(verified.signature.mathematicallyValid);
    QVERIFY(!verified.signature.belowStrengthFloor);
    QVERIFY(!verified.signature.primaryFingerprint.isEmpty());
    signedBytes[0] = char(signedBytes.at(0) ^ 1);
    QVERIFY(!OpenPgpDecryptor()
                 .verifyDetached(signedBytes, signature, "fixture-home")
                 .signature.mathematicallyValid);
    store.refuse = true;
    QVERIFY(loadGoPgpKeys("fixture-home").isEmpty());
}
void GoPgpBackendTest::importsV2OnlyAfterCheckingTheWholeSnapshot()
{
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
    QCOMPARE(importDeviceDelivery(6, SecureBytes(key), expected, "ring-home"),
             PgpKeyringImportStatus::Rejected);
    QVERIFY(store.binary.isEmpty());
    auto wrong = expected;
    wrong.materialGeneration++;
    QCOMPARE(importDeviceDelivery(6, SecureBytes(QJsonDocument(ring).toJson()), wrong, "ring-home"),
             PgpKeyringImportStatus::Rejected);
    QVERIFY(store.binary.isEmpty());
    QCOMPARE(importDeviceDelivery(6, SecureBytes(QJsonDocument(ring).toJson()), expected, "ring-home"),
             PgpKeyringImportStatus::Imported);
    // No process-local key cache: a fresh store object can reopen the persisted
    // entries.
    BinaryStore restarted;
    restarted.binary = store.binary;
    restarted.text = store.text;
    setGoPgpSecretStore(&restarted);
    QCOMPARE(OpenPgpDecryptor().decrypt(fixture("openpgp-v6/v6-pqc.msg.asc"), "ring-home").status,
             PgpDecryptStatus::Decrypted);
    setGoPgpSecretStore(&store);
    auto largeRing = QJsonDocument(ring).toJson();
    largeRing.append(QByteArray(200 * 1024, ' '));
    QCOMPARE(importDeviceDelivery(6, SecureBytes(largeRing), expected, "ring-home"),
             PgpKeyringImportStatus::Imported);
    QCOMPARE(importDeviceDelivery(6, SecureBytes(QByteArray(512 * 1024 + 1, ' ')), expected, "ring-home"),
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
    QCOMPARE(importDeviceDelivery(6, SecureBytes(QJsonDocument(dual).toJson()), dualExpected, "ring-home"),
             PgpKeyringImportStatus::Imported);
    QCOMPARE(OpenPgpDecryptor().decrypt(fixture("openpgp-v6/v4-pqc.msg.asc"), "ring-home").status,
             PgpDecryptStatus::Decrypted);
    auto missingSelection = dualExpected;
    missingSelection.activeFingerprint.clear();
    QCOMPARE(
        importDeviceDelivery(6, SecureBytes(QJsonDocument(dual).toJson()), missingSelection, "ring-home"),
        PgpKeyringImportStatus::Rejected);
    BinaryStore broken;
    broken.corruptWrites = true;
    setGoPgpSecretStore(&broken);
    QCOMPARE(importDeviceDelivery(6, SecureBytes(QJsonDocument(ring).toJson()), expected, "broken-home"),
             PgpKeyringImportStatus::ImportFailed);
    QVERIFY(broken.text.isEmpty()); // Unverified writes never reach the durable index.
    setGoPgpSecretStore(&store);
    const auto bytes = store.binary;
    QCOMPARE(importDeviceDelivery(6, SecureBytes(QJsonDocument(ring).toJson()), expected, "ring-home",
                                  [] { return false; }),
             PgpKeyringImportStatus::Cancelled);
    QCOMPARE(store.binary, bytes);
    store.refuse = true;
    QCOMPARE(importDeviceDelivery(6, SecureBytes(QJsonDocument(ring).toJson()), expected, "ring-home"),
             PgpKeyringImportStatus::ImportFailed);
    store.refuse = false;
    ring["active"] = QJsonObject{{"v4", fpr}};
    QCOMPARE(importDeviceDelivery(6, SecureBytes(QJsonDocument(ring).toJson()), expected, "ring-home"),
             PgpKeyringImportStatus::Rejected);
}
QTEST_GUILESS_MAIN(GoPgpBackendTest)
#include "GoPgpBackendTest.moc"
