#include "pgp/OpenPgpKeyImporter.h"
#include "pgp/OpenPgpDecryptor.h"
#include "pgp/OpenPgpEncryptor.h"
#include "GnupgFixture.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QTest>

namespace {
PgpKeyringSnapshot snapshot(const QJsonObject& ring)
{
    PgpKeyringSnapshot out;
    out.activeFingerprint = ring["activeFingerprint"].toString();
    out.materialGeneration = ring["materialGeneration"].toInteger();
    for (const auto& value : ring["keyFingerprints"].toArray())
        out.keyFingerprints.append(value.toString());
    return out;
}
QByteArray encoded(const QJsonObject& ring) { return QJsonDocument(ring).toJson(QJsonDocument::Compact); }
// All private keys handled by QJson in this test are PUBLIC synthetic fixtures.
QByteArray runGpg(const QString& home, const QStringList& arguments, const QByteArray& input = {})
{
    QProcess process;
    process.start("gpg", QStringList{"--homedir", home, "--batch", "--yes", "--pinentry-mode", "loopback"} + arguments);
    if (!process.waitForStarted(10000) || process.write(input) != input.size())
        return {};
    process.closeWriteChannel();
    if (!process.waitForFinished(30000) || process.exitCode() != 0)
        return {};
    return process.readAllStandardOutput();
}
}

class OpenPgpKeyringTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void freshImportSurvivesAgentRestartAndSelectsCurrentSigner();
    void malformedRingIsRejectedBeforeDestinationWrites_data();
    void malformedRingIsRejectedBeforeDestinationWrites();
    void snapshotBindingsAreRequired();
    void unpublishedCertificateIsNeverAppliedOrDiscarded();
    void cancellationAfterPartialImportCanBeRetried();
    void failedDestinationImportIsNotSuccess();
    void failureDuringSecondImportPreservesFirstAndRetries();
    void publicOnlyAndDummyPrimaryAreRejected();
    void protectedSecretPacketsAreRejected();
    void compressedKeyPacketsAreRefusedBeforeGpgExpansion();
    void jsonEscapesCaseAndNumberRepresentationsWork();
private:
    QJsonObject m_fixture;
    QJsonObject m_ring;
};

void OpenPgpKeyringTest::initTestCase()
{
    QVERIFY2(OpenPgpDecryptor::engineAvailable(), "GnuPG is required to verify complete-ring custody");
    QFile file(QFINDTESTDATA("../../fixtures/pgp-keyring-v1.json"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    m_fixture = QJsonDocument::fromJson(file.readAll()).object();
    m_ring = m_fixture["ring"].toObject();
    QCOMPARE(m_ring["keys"].toArray().size(), 2);
}

void OpenPgpKeyringTest::freshImportSurvivesAgentRestartAndSelectsCurrentSigner()
{
    QTemporaryDir target;
    QVERIFY(target.isValid());
    const auto home = GnupgFixture::emptyHome(target);
    const auto cleanup = qScopeGuard([&] { GnupgFixture::killAgent(home); });
    const SecureBytes bytes(encoded(m_ring));
    const auto expected = snapshot(m_ring);
    QCOMPARE(importPrivateKeyring(bytes, expected, home), PgpKeyringImportStatus::Imported);
    QCOMPARE(GnupgFixture::fingerprintsIn(home).size(), 2);
    QCOMPARE(importPrivateKeyring(bytes, expected, home), PgpKeyringImportStatus::Unchanged);
    GnupgFixture::killAgent(home); // Next operations start fresh GnuPG/agent processes, reading disk.
    for (const auto* name : {"ciphertext", "hiddenCiphertext"}) {
        const auto decrypted = OpenPgpDecryptor().decrypt(m_fixture[name].toString().toUtf8(), home);
        QCOMPARE(decrypted.status, PgpDecryptStatus::Decrypted);
        QCOMPARE(decrypted.plaintext, m_fixture["plaintext"].toString().toUtf8());
    }
    const auto sent = signAndEncrypt("current signer", expected.activeFingerprint,
                                    {expected.activeFingerprint}, home);
    QCOMPARE(sent.status, PgpEncryptStatus::Encrypted);
    const auto read = OpenPgpDecryptor().decrypt(sent.armoredCiphertext.toUtf8(), home);
    QCOMPARE(read.plaintext, QByteArray("current signer"));
    QVERIFY(read.signature.mathematicallyValid);
    QCOMPARE(read.signature.primaryFingerprint, expected.activeFingerprint);
}

void OpenPgpKeyringTest::malformedRingIsRejectedBeforeDestinationWrites_data()
{
    QTest::addColumn<QByteArray>("payload");
    QTest::addColumn<bool>("bindMutatedSnapshot");
    const auto original = encoded(m_ring);
    auto row = [&](const char* name, const QJsonObject& ring, bool bind = false) {
        QTest::newRow(name) << encoded(ring) << bind;
    };
    QTest::newRow("empty") << QByteArray() << false;
    QTest::newRow("oversized") << original.leftJustified(128 * 1024 + 1, ' ') << false;
    QTest::newRow("truncated") << original.left(original.size() - 1) << false;
    QTest::newRow("trailing-json") << original + "{}" << false;
    QTest::newRow("duplicate-field") << original.left(original.size() - 1) + ",\"format\":\"kypost-pgp-keyring-v1\"}" << false;
    auto ring = m_ring; ring["format"] = "kypost-pgp-keyring-v2"; row("unknown-format", ring);
    ring = m_ring; ring["extra"] = true; row("unknown-field", ring);
    ring = m_ring; ring["materialGeneration"] = 0; row("zero-generation", ring, true);
    ring = m_ring; ring["materialGeneration"] = 1.5; row("fractional-generation", ring, true);
    ring = m_ring; ring["materialGeneration"] = 9007199254740992.0; row("unsafe-generation", ring, true);
    ring = m_ring; ring["materialGeneration"] = "2"; row("string-generation", ring);
    ring = m_ring; ring.remove("activeFingerprint"); row("missing-active", ring);
    auto keys = m_ring["keys"].toArray();
    ring = m_ring; ring["keys"] = QJsonArray(); row("empty-keys", ring);
    ring = m_ring; ring["keys"] = QJsonArray{keys[0], keys[0]}; row("duplicate-primary", ring);
    auto first = keys[0].toObject(); first["fingerprint"] = first["fingerprint"].toString().toLower();
    ring = m_ring; ring["keys"] = QJsonArray{keys[0], first}; row("duplicate-normalized-primary", ring);
    QJsonArray many;
    for (int i = 0; i < 17; ++i) {
        auto key = keys[0].toObject(); key["fingerprint"] = QString::number(i, 16).rightJustified(40, '0');
        many.append(key);
    }
    ring = m_ring; ring["keys"] = many; row("too-many-primary-keys", ring);
    ring = m_ring; ring["keys"] = QJsonArray{keys[1]}; row("active-not-member", ring, true);
    first = keys[0].toObject(); first.remove("privateKey");
    ring = m_ring; ring["keys"] = QJsonArray{first, keys[1]}; row("missing-private-key", ring);
    first = keys[0].toObject(); first["privateKey"] = 42;
    ring["keys"] = QJsonArray{first, keys[1]}; row("private-key-wrong-type", ring);
    first = keys[0].toObject(); first["privateKey"] = first["privateKey"].toString() + keys[1].toObject()["privateKey"].toString();
    ring["keys"] = QJsonArray{first, keys[1]}; row("two-primaries-in-one-armor", ring);
    auto last = keys[1].toObject(); last["privateKey"] = "not an OpenPGP key";
    ring["keys"] = QJsonArray{keys[0], last}; row("invalid-last-member", ring);
    first = keys[0].toObject(); first["privateKey"] = keys[1].toObject()["privateKey"];
    ring["keys"] = QJsonArray{first, keys[1]}; row("packet-fingerprint-mismatch", ring);
    auto inventory = m_ring["keyFingerprints"].toArray();
    ring = m_ring; auto missing = inventory; missing.removeLast(); ring["keyFingerprints"] = missing;
    row("missing-subkey-inventory", ring, true);
    inventory.append(inventory[0].toString().toLower()); ring["keyFingerprints"] = inventory;
    row("duplicate-normalized-inventory", ring, true);
    many = {};
    for (int i = 0; i < 257; ++i) many.append(QString::number(i, 16).rightJustified(40, '0'));
    ring["keyFingerprints"] = many; row("too-many-fingerprints", ring, true);
    auto escaped = original; escaped.replace("-----BEGIN", "\\uD800-----BEGIN");
    QTest::newRow("surrogate-in-armor") << escaped << false;
    escaped = original; escaped.replace("-----BEGIN", "\\x-----BEGIN");
    QTest::newRow("invalid-json-escape") << escaped << false;
    escaped = original; escaped.replace("-----BEGIN", "\n-----BEGIN");
    QTest::newRow("unescaped-control") << escaped << false;
}

void OpenPgpKeyringTest::malformedRingIsRejectedBeforeDestinationWrites()
{
    QFETCH(QByteArray, payload);
    QFETCH(bool, bindMutatedSnapshot);
    QTemporaryDir target;
    QVERIFY(target.isValid());
    const auto home = GnupgFixture::emptyHome(target);
    const auto cleanup = qScopeGuard([&] { GnupgFixture::killAgent(home); });
    auto expected = snapshot(m_ring);
    if (bindMutatedSnapshot) expected = snapshot(QJsonDocument::fromJson(payload).object());
    QCOMPARE(importPrivateKeyring(SecureBytes(std::move(payload)), expected, home), PgpKeyringImportStatus::Rejected);
    QVERIFY(GnupgFixture::fingerprintsIn(home).isEmpty());
}

void OpenPgpKeyringTest::snapshotBindingsAreRequired()
{
    QTemporaryDir target;
    const auto home = GnupgFixture::emptyHome(target);
    const auto cleanup = qScopeGuard([&] { GnupgFixture::killAgent(home); });
    const SecureBytes bytes(encoded(m_ring));
    auto expected = snapshot(m_ring); ++expected.materialGeneration;
    QCOMPARE(importPrivateKeyring(bytes, expected, home), PgpKeyringImportStatus::Rejected);
    expected = snapshot(m_ring); expected.activeFingerprint = m_ring["keys"].toArray()[1].toObject()["fingerprint"].toString();
    QCOMPARE(importPrivateKeyring(bytes, expected, home), PgpKeyringImportStatus::Rejected);
    expected = snapshot(m_ring); expected.keyFingerprints.removeLast();
    QCOMPARE(importPrivateKeyring(bytes, expected, home), PgpKeyringImportStatus::Rejected);
    QVERIFY(GnupgFixture::fingerprintsIn(home).isEmpty());
}

void OpenPgpKeyringTest::unpublishedCertificateIsNeverAppliedOrDiscarded()
{
    auto ring = m_ring;
    auto keys = ring["keys"].toArray();
    auto key = keys[1].toObject(); key["revocationCertificate"] = "unpublished certificate";
    keys[1] = key; ring["keys"] = keys;
    QTemporaryDir target;
    const auto home = GnupgFixture::emptyHome(target);
    const auto cleanup = qScopeGuard([&] { GnupgFixture::killAgent(home); });
    QCOMPARE(importPrivateKeyring(SecureBytes(encoded(ring)), snapshot(ring), home),
             PgpKeyringImportStatus::UnsupportedRevocationCertificate);
    QVERIFY(GnupgFixture::fingerprintsIn(home).isEmpty());
}

void OpenPgpKeyringTest::cancellationAfterPartialImportCanBeRetried()
{
    QTemporaryDir target;
    const auto home = GnupgFixture::emptyHome(target);
    const auto cleanup = qScopeGuard([&] { GnupgFixture::killAgent(home); });
    // Retain an unrelated user's key as well as a partially imported ring.
    GnupgFixture existing;
    QVERIFY(existing.build());
    const auto existingCleanup = qScopeGuard([&] { GnupgFixture::killAgent(existing.path()); });
    const auto other = existing.fingerprintOf("test@example.com");
    QCOMPARE(importPrivateKey(SecureBytes(existing.exportSecretKeys({"test@example.com"})), other, home).status,
             PgpImportStatus::Imported);
    const SecureBytes bytes(encoded(m_ring));
    int checks = 0;
    QCOMPARE(importPrivateKeyring(bytes, snapshot(m_ring), home, [&] { return ++checks < 4; }),
             PgpKeyringImportStatus::Cancelled);
    auto present = GnupgFixture::fingerprintsIn(home);
    QCOMPARE(present.size(), 2); // Two validation checks, first import, then cancel.
    QVERIFY(present.contains(other));
    QVERIFY(present.contains(snapshot(m_ring).activeFingerprint));
    QCOMPARE(importPrivateKeyring(bytes, snapshot(m_ring), home), PgpKeyringImportStatus::Imported);
    QCOMPARE(GnupgFixture::fingerprintsIn(home).size(), 3);
    QCOMPARE(importPrivateKeyring(bytes, snapshot(m_ring), home, [] { return false; }),
             PgpKeyringImportStatus::Cancelled);
    checks = 0;
    QCOMPARE(importPrivateKeyring(bytes, snapshot(m_ring), home, [&] { return ++checks < 5; }),
             PgpKeyringImportStatus::Cancelled); // Last import completed but authority expired.
    QCOMPARE(importPrivateKeyring(bytes, snapshot(m_ring), home), PgpKeyringImportStatus::Unchanged);
}

void OpenPgpKeyringTest::failedDestinationImportIsNotSuccess()
{
    QTemporaryDir target;
    QVERIFY(target.isValid());
    const QString home = target.filePath("not-a-directory");
    QFile file(home); QVERIFY(file.open(QIODevice::WriteOnly)); file.close();
    QCOMPARE(importPrivateKeyring(SecureBytes(encoded(m_ring)), snapshot(m_ring), home),
             PgpKeyringImportStatus::ImportFailed);
    QVERIFY(QFileInfo(home).isFile());
}

void OpenPgpKeyringTest::failureDuringSecondImportPreservesFirstAndRetries()
{
    QTemporaryDir target;
    const auto home = GnupgFixture::emptyHome(target);
    const auto cleanup = qScopeGuard([&] { GnupgFixture::killAgent(home); });
    QDir directory(home);
    const auto privatePath = directory.filePath("private-keys-v1.d");
    int checks = 0;
    bool faultInstalled = false;
    const SecureBytes bytes(encoded(m_ring));
    const auto result = importPrivateKeyring(bytes, snapshot(m_ring), home, [&] {
        if (++checks == 4 && directory.rename("private-keys-v1.d", "saved-private-keys")) {
            QFile obstruction(privatePath);
            faultInstalled = obstruction.open(QIODevice::WriteOnly);
        }
        return true;
    });
    QVERIFY(faultInstalled);
    QCOMPARE(result, PgpKeyringImportStatus::ImportFailed);
    QVERIFY(QFile::remove(privatePath));
    QVERIFY(directory.rename("saved-private-keys", "private-keys-v1.d"));
    GnupgFixture::killAgent(home);
    const auto active = snapshot(m_ring).activeFingerprint;
    const auto sent = signAndEncrypt("first survived", active, {active}, home);
    QCOMPARE(sent.status, PgpEncryptStatus::Encrypted);
    QCOMPARE(importPrivateKeyring(bytes, snapshot(m_ring), home), PgpKeyringImportStatus::Imported);
    QCOMPARE(importPrivateKeyring(bytes, snapshot(m_ring), home), PgpKeyringImportStatus::Unchanged);
    const auto historical = OpenPgpDecryptor().decrypt(m_fixture["ciphertext"].toString().toUtf8(), home);
    QCOMPARE(historical.plaintext, m_fixture["plaintext"].toString().toUtf8());
}

void OpenPgpKeyringTest::publicOnlyAndDummyPrimaryAreRejected()
{
    QTemporaryDir donor;
    const auto donorHome = GnupgFixture::emptyHome(donor);
    const auto cleanupDonor = qScopeGuard([&] { GnupgFixture::killAgent(donorHome); });
    const auto original = m_ring["keys"].toArray()[0].toObject();
    const auto fpr = original["fingerprint"].toString();
    QCOMPARE(importPrivateKey(SecureBytes(original["privateKey"].toString().toUtf8()), fpr, donorHome).status,
             PgpImportStatus::Imported);
    for (const auto* command : {"--export", "--export-secret-subkeys"}) {
        const auto armor = runGpg(donorHome, {"--passphrase", "", "--armor", command, fpr});
        QVERIFY(!armor.isEmpty());
        auto first = original; first["privateKey"] = QString::fromUtf8(armor);
        auto keys = m_ring["keys"].toArray(); keys[0] = first;
        auto ring = m_ring; ring["keys"] = keys;
        QTemporaryDir target;
        const auto home = GnupgFixture::emptyHome(target);
        const auto cleanup = qScopeGuard([&] { GnupgFixture::killAgent(home); });
        QCOMPARE(importPrivateKeyring(SecureBytes(encoded(ring)), snapshot(ring), home),
                 PgpKeyringImportStatus::Rejected);
        QVERIFY(GnupgFixture::fingerprintsIn(home).isEmpty());
    }
}

void OpenPgpKeyringTest::protectedSecretPacketsAreRejected()
{
    QTemporaryDir donor;
    const auto home = GnupgFixture::emptyHome(donor);
    const auto cleanup = qScopeGuard([&] { GnupgFixture::killAgent(home); });
    runGpg(home, {"--passphrase", "public-test-passphrase", "--quick-generate-key", "Protected <test@example.test>", "ed25519", "sign", "never"});
    const auto fpr = GnupgFixture::firstFingerprint(home, "test@example.test");
    QVERIFY(!fpr.isEmpty());
    const auto armor = runGpg(home, {"--passphrase", "public-test-passphrase", "--armor", "--export-secret-keys", fpr});
    QVERIFY(!armor.isEmpty());
    const QJsonObject ring{{"format", "kypost-pgp-keyring-v1"}, {"materialGeneration", 1},
        {"activeFingerprint", fpr}, {"keyFingerprints", QJsonArray{fpr}},
        {"keys", QJsonArray{QJsonObject{{"fingerprint", fpr}, {"privateKey", QString::fromUtf8(armor)}}}}};
    QTemporaryDir target;
    const auto targetHome = GnupgFixture::emptyHome(target);
    const auto cleanupTarget = qScopeGuard([&] { GnupgFixture::killAgent(targetHome); });
    QCOMPARE(importPrivateKeyring(SecureBytes(encoded(ring)), snapshot(ring), targetHome),
             PgpKeyringImportStatus::Rejected);
    QVERIFY(GnupgFixture::fingerprintsIn(targetHome).isEmpty());
}

void OpenPgpKeyringTest::compressedKeyPacketsAreRefusedBeforeGpgExpansion()
{
    QTemporaryDir donor;
    const auto donorHome = GnupgFixture::emptyHome(donor);
    const auto cleanupDonor = qScopeGuard([&] { GnupgFixture::killAgent(donorHome); });
    auto keys = m_ring["keys"].toArray();
    auto first = keys[0].toObject();
    const auto binary = runGpg(donorHome, {"--dearmor"}, first["privateKey"].toString().toUtf8());
    QVERIFY(!binary.isEmpty());
    const QByteArray compressed = QByteArray(1, char(2)) + qCompress(binary).mid(4); // OpenPGP ZLIB algorithm.
    QByteArray packet; packet.append(char(0xc8)); packet.append(char(255));
    for (int shift : {24, 16, 8, 0}) packet.append(char((compressed.size() >> shift) & 255));
    packet += compressed;
    const QByteArray armor = "-----BEGIN PGP PRIVATE KEY BLOCK-----\n\n" + packet.toBase64()
        + "\n-----END PGP PRIVATE KEY BLOCK-----\n";
    // GnuPG 2.4.4 read_block expands PKT_COMPRESSED during import. 2.4.9
    // already refuses it. Require refusal independent of the installed engine;
    // do not assert that the legacy path accepts it on every GnuPG version.
    first["privateKey"] = QString::fromUtf8(armor); keys[0] = first;
    auto ring = m_ring; ring["keys"] = keys;
    QTemporaryDir target;
    const auto home = GnupgFixture::emptyHome(target);
    const auto cleanup = qScopeGuard([&] { GnupgFixture::killAgent(home); });
    QCOMPARE(importPrivateKeyring(SecureBytes(encoded(ring)), snapshot(ring), home),
             PgpKeyringImportStatus::Rejected);
    QVERIFY(GnupgFixture::fingerprintsIn(home).isEmpty());
}

void OpenPgpKeyringTest::jsonEscapesCaseAndNumberRepresentationsWork()
{
    auto bytes = encoded(m_ring);
    for (const auto& fpr : snapshot(m_ring).keyFingerprints)
        bytes.replace(fpr.toLatin1(), fpr.toLatin1().toLower());
    bytes.replace("-----BEGIN", "\\u002d----BEGIN");
    bytes.replace("Generation\":2", "Generation\":2e0");
    QTemporaryDir target;
    const auto home = GnupgFixture::emptyHome(target);
    const auto cleanup = qScopeGuard([&] { GnupgFixture::killAgent(home); });
    QCOMPARE(importPrivateKeyring(SecureBytes(std::move(bytes)), snapshot(m_ring), home),
             PgpKeyringImportStatus::Imported);
}

QTEST_GUILESS_MAIN(OpenPgpKeyringTest)
#include "OpenPgpKeyringTest.moc"
