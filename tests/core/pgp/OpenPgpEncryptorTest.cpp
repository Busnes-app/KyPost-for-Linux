#include "pgp/OpenPgpEncryptor.h"

#include "pgp/OpenPgpDecryptor.h"
#include "pgp/OpenPgpKeyImporter.h"

#include "GnupgFixture.h"

#include <QProcess>
#include <QProcessEnvironment>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QTest>

// Two real keyrings, arranged the way a send actually is: the sender holds
// their own secret key plus the recipient's PUBLIC key (imported from the
// relay's discovery ladder, with no web-of-trust path to it), and the
// recipient holds their own secret key. Encrypting in one and decrypting in
// the other is the only evidence that matters here.
class OpenPgpEncryptorTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void theRecipientCanReadWhatTheSenderEncrypted();
    void theMessageIsSignedAndTheSignatureVerifies();
    void aStrangerCannotReadIt();
    void everyRecipientOfATwoWayMessageCanReadIt();
    void oneMissingRecipientKeyFailsTheWholeMessage();
    void aRecipientWhoseKeyGpgHoldsButRefusesAlsoFailsTheWholeMessage();
    void aSenderWithNoSecretKeySendsNothingRatherThanSendingItUnsigned();
    void thereIsNothingToEncryptWithNoRecipients();
    void emptyInputIsRefused();
    void keyPatternsAreNotFingerprints_data();
    void keyPatternsAreNotFingerprints();
    void undersizedRsaKeysAreNotUsedForNewMessages_data();
    void undersizedRsaKeysAreNotUsedForNewMessages();
    void dsaAndElgamalKeysMeetTheSameFloor_data();
    void dsaAndElgamalKeysMeetTheSameFloor();
    void aSignatureDigestBelowSha224IsNotSent();

private:
    QString importInto(const QString& home, const GnupgFixture& from, const QString& uid) const;

    GnupgFixture m_sender;   // holds sender@example.com (secret)
    GnupgFixture m_recipient; // holds recipient@example.com (secret)
    GnupgFixture m_second;    // holds second@example.com (secret)
    // A correspondent whose key is in the sender's keyring and which gpg will
    // not encrypt to. Expired, which is the ordinary way that happens.
    GnupgFixture m_expired;
    QString m_expiredFingerprint;
    QString m_recipientFingerprint;
    QString m_secondFingerprint;
};

void OpenPgpEncryptorTest::undersizedRsaKeysAreNotUsedForNewMessages_data()
{
    QTest::addColumn<QJsonObject>("fixture");
    QTest::addColumn<bool>("signing");
    QFile file(QFINDTESTDATA("../../fixtures/openpgp-rsa-policy.json"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto fixtures = QJsonDocument::fromJson(file.readAll()).object()["fixtures"].toArray();
    QCOMPARE(fixtures.size(), 7);
    for (const auto& value : fixtures) {
        const auto fixture = value.toObject();
        for (bool signing : {false, true}) {
            const auto name = fixture["name"].toString().toLatin1() + (signing ? "-signer" : "-recipient");
            QTest::newRow(name.constData()) << fixture << signing;
        }
    }
}

void OpenPgpEncryptorTest::undersizedRsaKeysAreNotUsedForNewMessages()
{
    QFETCH(QJsonObject, fixture);
    QFETCH(bool, signing);
    GnupgFixture home;
    const auto cleanup = qScopeGuard([&] { GnupgFixture::killAgent(home.path()); });
    const auto runGpg = [&](const QStringList& args, const QByteArray& input = QByteArray()) {
        QProcess process;
        process.start("gpg", QStringList{"--homedir", home.path(), "--batch", "--yes",
            "--pinentry-mode", "loopback", "--passphrase", ""} + args);
        if (!process.waitForStarted(10000) || process.write(input) != input.size())
            return QByteArray();
        process.closeWriteChannel();
        if (!process.waitForFinished(30000) || process.exitCode() != 0)
            return QByteArray();
        return process.readAllStandardOutput();
    };
    // Import public, unprotected synthetic test secrets; never use the user's keyring.
    runGpg({"--import"}, fixture["privateKey"].toString().toUtf8());
    const auto fingerprint = fixture["fingerprint"].toString();
    QVERIFY(GnupgFixture::fingerprintsIn(home.path()).contains(fingerprint));
    const auto before = runGpg({"--export-secret-keys", fingerprint});
    QVERIFY(!before.isEmpty());

    // Verify actual GnuPG packet lengths, including the literal 2047-bit boundary.
    const auto listing = runGpg({"--with-colons", "--list-keys", fingerprint});
    QVERIFY(listing.contains(QByteArray(":") + QByteArray::number(fixture["primaryBits"].toInt()) + ":1:"));
    QVERIFY(listing.contains(QByteArray(":") + QByteArray::number(fixture["subkeyBits"].toInt()) + ":1:"));

    const auto strongSigner = m_sender.fingerprintOf(QStringLiteral("sender@example.com"));
    runGpg({"--import"}, m_sender.exportSecretKeys({QStringLiteral("sender@example.com")}));
    QVERIFY(GnupgFixture::fingerprintsIn(home.path()).contains(strongSigner));
    const bool weakPrimary = fixture["primaryBits"].toInt() < 2048;
    const bool weakRoleSubkey = fixture["subkeyBits"].toInt() < 2048
        && signing == fixture["signingSubkey"].toBool();
    const auto recipients = signing ? QStringList{strongSigner} : QStringList{strongSigner, fingerprint};
    const auto encrypted = signAndEncrypt("new private mail\n", signing ? fingerprint : strongSigner,
                                         recipients, home.path());
    if (weakPrimary || weakRoleSubkey) {
        QCOMPARE(encrypted.status, signing ? PgpEncryptStatus::NoSigningKey : PgpEncryptStatus::RecipientKeyUnusable);
        QVERIFY(encrypted.armoredCiphertext.isEmpty());
        if (!signing)
            QVERIFY(encrypted.unusableRecipients.contains(fingerprint));
    } else {
        QCOMPARE(encrypted.status, PgpEncryptStatus::Encrypted);
        QCOMPARE(OpenPgpDecryptor().decrypt(encrypted.armoredCiphertext.toUtf8(), home.path()).plaintext,
                 QByteArray("new private mail\n"));
    }
    // Historical mail and the exact exported secret key remain readable/intact.
    const auto old = OpenPgpDecryptor().decrypt(fixture["ciphertext"].toString().toUtf8(), home.path());
    QCOMPARE(old.status, PgpDecryptStatus::Decrypted);
    QCOMPARE(old.plaintext, fixture["plaintext"].toString().toUtf8());
    QCOMPARE(runGpg({"--export-secret-keys", fingerprint}), before);
}

void OpenPgpEncryptorTest::dsaAndElgamalKeysMeetTheSameFloor_data()
{
    QTest::addColumn<int>("bits");
    QTest::addColumn<bool>("signing");
    for (int bits : { 1024, 2048 }) {
        for (bool signing : { false, true })
            QTest::newRow(qPrintable(QStringLiteral("dsa-elg%1-%2").arg(bits).arg(signing ? "signer" : "recipient")))
                << bits << signing;
    }
}

// GnuPG still creates DSA-1024 + ElGamal-1024 keys without complaint.
void OpenPgpEncryptorTest::dsaAndElgamalKeysMeetTheSameFloor()
{
    QFETCH(int, bits);
    QFETCH(bool, signing);
    GnupgFixture home;
    QVERIFY(home.build(QStringLiteral("Legacy <legacy@example.com>"), QStringLiteral("dsa%1").arg(bits)));
    const auto cleanup = qScopeGuard([&] { GnupgFixture::killAgent(home.path()); });
    const QString legacy = home.fingerprintOf(QStringLiteral("legacy@example.com"));
    const auto runGpg = [&](const QStringList& args, const QByteArray& input = QByteArray()) {
        QProcess process;
        process.start(QStringLiteral("gpg"), QStringList{ QStringLiteral("--homedir"), home.path(),
                                                          QStringLiteral("--batch"), QStringLiteral("--pinentry-mode"),
                                                          QStringLiteral("loopback"), QStringLiteral("--passphrase"),
                                                          QString() } + args);
        process.waitForStarted(10000);
        process.write(input);
        process.closeWriteChannel();
        return process.waitForFinished(60000) && process.exitCode() == 0;
    };
    QVERIFY(runGpg({ QStringLiteral("--quick-add-key"), legacy, QStringLiteral("elg%1").arg(bits),
                     QStringLiteral("encr"), QStringLiteral("never") }));
    QVERIFY(runGpg({ QStringLiteral("--import") }, m_sender.exportSecretKeys({ QStringLiteral("sender@example.com") })));
    const QString strong = m_sender.fingerprintOf(QStringLiteral("sender@example.com"));

    const auto result = signing ? signAndEncrypt("new private mail\n", legacy, { strong }, home.path())
                                : signAndEncrypt("new private mail\n", strong, { strong, legacy }, home.path());
    if (bits >= 2048) {
        QCOMPARE(result.status, PgpEncryptStatus::Encrypted);
        return;
    }
    QCOMPARE(result.status, signing ? PgpEncryptStatus::NoSigningKey : PgpEncryptStatus::RecipientKeyUnusable);
    QVERIFY(result.armoredCiphertext.isEmpty());
    if (!signing)
        QVERIFY(result.unusableRecipients.contains(legacy));
}

// The digest comes from gpg.conf, which this app does not own.
void OpenPgpEncryptorTest::aSignatureDigestBelowSha224IsNotSent()
{
    GnupgFixture home;
    QVERIFY(home.build(QStringLiteral("Sha One <sha1@example.com>")));
    const auto cleanup = qScopeGuard([&] { GnupgFixture::killAgent(home.path()); });
    const QString self = home.fingerprintOf(QStringLiteral("sha1@example.com"));
    QFile conf(home.path() + QStringLiteral("/gpg.conf"));
    QVERIFY(conf.open(QIODevice::WriteOnly));
    conf.write("digest-algo SHA1\n");
    conf.close();

    const auto result = signAndEncrypt("new private mail\n", self, { self }, home.path());
    QCOMPARE(result.status, PgpEncryptStatus::Failed);
    QVERIFY(result.armoredCiphertext.isEmpty());

    QVERIFY(conf.open(QIODevice::WriteOnly | QIODevice::Truncate));
    conf.write("digest-algo SHA224\n");
    conf.close();
    QCOMPARE(signAndEncrypt("new private mail\n", self, { self }, home.path()).status, PgpEncryptStatus::Encrypted);
}

QString OpenPgpEncryptorTest::importInto(const QString& home, const GnupgFixture& from,
                                          const QString& uid) const
{
    const QByteArray armored = from.exportPublicKey(uid);
    if (armored.isEmpty())
        return {};
    // Through the real importer, not a gpg --import shortcut: this is how the
    // key gets there in production, fingerprint check and all.
    const PgpImportResult result = importPublicKey(armored, from.fingerprintOf(uid), home);
    if (result.status != PgpImportStatus::Imported && result.status != PgpImportStatus::Unchanged)
        return {};
    return result.fingerprint;
}

void OpenPgpEncryptorTest::initTestCase()
{
    if (!OpenPgpDecryptor::engineAvailable())
        QSKIP("no usable gpg on this system -- signing and encryption are NOT covered");
    if (!m_sender.build(QStringLiteral("KyPost Sender <sender@example.com>"))
        || !m_recipient.build(QStringLiteral("KyPost Recipient <recipient@example.com>"))
        || !m_second.build(QStringLiteral("KyPost Second <second@example.com>"))) {
        QSKIP("could not build throwaway GnuPG keyrings -- signing and encryption are NOT covered");
    }

    m_recipientFingerprint =
        importInto(m_sender.path(), m_recipient, QStringLiteral("recipient@example.com"));
    m_secondFingerprint = importInto(m_sender.path(), m_second, QStringLiteral("second@example.com"));
    QVERIFY(!m_recipientFingerprint.isEmpty());
    QVERIFY(!m_secondFingerprint.isEmpty());

    if (m_expired.buildExpired(QStringLiteral("Expired <expired@example.com>")))
        m_expiredFingerprint = importInto(m_sender.path(), m_expired, QStringLiteral("expired@example.com"));
}

void OpenPgpEncryptorTest::cleanupTestCase()
{
    GnupgFixture::killAgent(m_sender.path());
    GnupgFixture::killAgent(m_recipient.path());
    GnupgFixture::killAgent(m_second.path());
    GnupgFixture::killAgent(m_expired.path());
}

// The whole point, end to end, and note what the sender's keyring holds: an
// imported public key with no certification from anyone. That is what every
// key off the discovery ladder looks like, and it is why ALWAYS_TRUST is set.
void OpenPgpEncryptorTest::theRecipientCanReadWhatTheSenderEncrypted()
{
    const QByteArray secret = "the numbers are worse than we said\n";

    const PgpEncryptResult encrypted = signAndEncrypt(
        secret, m_sender.fingerprintOf(QStringLiteral("sender@example.com")), { m_recipientFingerprint }, m_sender.path());

    QCOMPARE(encrypted.status, PgpEncryptStatus::Encrypted);
    QVERIFY(encrypted.armoredCiphertext.startsWith(QStringLiteral("-----BEGIN PGP MESSAGE-----")));
    QVERIFY2(!encrypted.armoredCiphertext.contains(QStringLiteral("worse than we said")),
             "the plaintext is sitting in the ciphertext");

    const PgpDecryptResult decrypted =
        OpenPgpDecryptor().decrypt(encrypted.armoredCiphertext.toUtf8(), m_recipient.path());

    QCOMPARE(decrypted.status, PgpDecryptStatus::Decrypted);
    QCOMPARE(decrypted.plaintext, secret);
}

// Combined sign+encrypt, not encrypt-only. The reading path verifies an
// inline signature, so producing an unsigned message would be a silent
// downgrade that no test of the plaintext would notice.
void OpenPgpEncryptorTest::theMessageIsSignedAndTheSignatureVerifies()
{
    const PgpEncryptResult encrypted =
        signAndEncrypt("signed and sealed\n", m_sender.fingerprintOf(QStringLiteral("sender@example.com")),
                        { m_recipientFingerprint }, m_sender.path());
    QCOMPARE(encrypted.status, PgpEncryptStatus::Encrypted);

    // The recipient needs the sender's public key to check the signature --
    // which is exactly the arrangement on a real reply.
    QVERIFY(!importInto(m_recipient.path(), m_sender, QStringLiteral("sender@example.com")).isEmpty());

    QProcess gpg;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("GNUPGHOME"), m_recipient.path());
    gpg.setProcessEnvironment(env);
    gpg.start(QStringLiteral("gpg"), { QStringLiteral("--batch"), QStringLiteral("--status-fd"),
                                        QStringLiteral("1"), QStringLiteral("--decrypt") });
    QVERIFY(gpg.waitForStarted(10000));
    gpg.write(encrypted.armoredCiphertext.toUtf8());
    gpg.closeWriteChannel();
    QVERIFY(gpg.waitForFinished(30000));

    const QByteArray status = gpg.readAllStandardOutput();
    QVERIFY2(status.contains("GOODSIG"), "the message carried no good signature");
    QVERIFY2(status.contains("VALIDSIG"), "the signature did not validate");
}

void OpenPgpEncryptorTest::aStrangerCannotReadIt()
{
    const PgpEncryptResult encrypted =
        signAndEncrypt("not for you\n", m_sender.fingerprintOf(QStringLiteral("sender@example.com")),
                        { m_recipientFingerprint }, m_sender.path());
    QCOMPARE(encrypted.status, PgpEncryptStatus::Encrypted);

    // m_second holds its own secret key and was not a recipient of this one.
    const PgpDecryptResult decrypted =
        OpenPgpDecryptor().decrypt(encrypted.armoredCiphertext.toUtf8(), m_second.path());

    QCOMPARE(decrypted.status, PgpDecryptStatus::NoSecretKey);
    QVERIFY(decrypted.plaintext.isEmpty());
}

void OpenPgpEncryptorTest::everyRecipientOfATwoWayMessageCanReadIt()
{
    const QByteArray secret = "both of you\n";

    const PgpEncryptResult encrypted =
        signAndEncrypt(secret, m_sender.fingerprintOf(QStringLiteral("sender@example.com")),
                        { m_recipientFingerprint, m_secondFingerprint }, m_sender.path());
    QCOMPARE(encrypted.status, PgpEncryptStatus::Encrypted);

    QCOMPARE(OpenPgpDecryptor().decrypt(encrypted.armoredCiphertext.toUtf8(), m_recipient.path()).plaintext,
             secret);
    QCOMPARE(OpenPgpDecryptor().decrypt(encrypted.armoredCiphertext.toUtf8(), m_second.path()).plaintext,
             secret);
}

// gpgme will encrypt to the recipients it can and report the rest as invalid.
// Taking that would send a message the sender believes went to two people and
// that only one can open -- and on a Bcc split the failing delivery is the one
// nobody sees.
void OpenPgpEncryptorTest::oneMissingRecipientKeyFailsTheWholeMessage()
{
    const QString absent = QStringLiteral("0000000000000000000000000000000000000000");

    const PgpEncryptResult encrypted =
        signAndEncrypt("half a message\n", m_sender.fingerprintOf(QStringLiteral("sender@example.com")),
                        { m_recipientFingerprint, absent }, m_sender.path());

    QCOMPARE(encrypted.status, PgpEncryptStatus::RecipientKeyUnusable);
    QVERIFY2(encrypted.armoredCiphertext.isEmpty(),
             "ciphertext was produced for a message that could not reach everyone");
    QVERIFY2(encrypted.unusableRecipients.contains(absent),
             "the user is not told WHICH recipient could not be encrypted to");
}

// The key is present but expired. Our preflight now rejects it before gpg
// encryption runs; this verifies the outcome, not the post-operation guard.
void OpenPgpEncryptorTest::aRecipientWhoseKeyGpgHoldsButRefusesAlsoFailsTheWholeMessage()
{
    if (m_expiredFingerprint.isEmpty())
        QSKIP("could not build an expired key -- the refused-recipient path is NOT covered");

    const PgpEncryptResult encrypted =
        signAndEncrypt("half a message\n", m_sender.fingerprintOf(QStringLiteral("sender@example.com")),
                        { m_recipientFingerprint, m_expiredFingerprint }, m_sender.path());

    QCOMPARE(encrypted.status, PgpEncryptStatus::RecipientKeyUnusable);
    QVERIFY2(encrypted.armoredCiphertext.isEmpty(),
             "ciphertext was produced for a message that could not reach everyone");
    QVERIFY2(!encrypted.unusableRecipients.isEmpty(),
             "the user is not told WHICH recipient could not be encrypted to");
}

// Falling back to an unsigned message would be a downgrade the recipient
// cannot see, on an account that normally signs.
void OpenPgpEncryptorTest::aSenderWithNoSecretKeySendsNothingRatherThanSendingItUnsigned()
{
    const PgpEncryptResult encrypted =
        signAndEncrypt("who am i\n", QStringLiteral("nobody@example.com"),
                        { m_recipientFingerprint }, m_sender.path());

    QCOMPARE(encrypted.status, PgpEncryptStatus::NoSigningKey);
    QVERIFY(encrypted.armoredCiphertext.isEmpty());
}

void OpenPgpEncryptorTest::thereIsNothingToEncryptWithNoRecipients()
{
    const PgpEncryptResult encrypted =
        signAndEncrypt("hello\n", m_sender.fingerprintOf(QStringLiteral("sender@example.com")), {}, m_sender.path());

    QCOMPARE(encrypted.status, PgpEncryptStatus::Failed);
    QVERIFY(encrypted.armoredCiphertext.isEmpty());
}

void OpenPgpEncryptorTest::emptyInputIsRefused()
{
    QCOMPARE(signAndEncrypt(QByteArray(), m_sender.fingerprintOf(QStringLiteral("sender@example.com")),
                             { m_recipientFingerprint }, m_sender.path())
                 .status,
             PgpEncryptStatus::Failed);
}

void OpenPgpEncryptorTest::keyPatternsAreNotFingerprints_data()
{
    QTest::addColumn<bool>("signerPattern");
    QTest::addColumn<bool>("shortId");
    QTest::newRow("signer-address") << true << false;
    QTest::newRow("recipient-address") << false << false;
    QTest::newRow("signer-short-id") << true << true;
    QTest::newRow("recipient-short-id") << false << true;
}

void OpenPgpEncryptorTest::keyPatternsAreNotFingerprints()
{
    QFETCH(bool, signerPattern);
    QFETCH(bool, shortId);
    const auto signer = m_sender.fingerprintOf(QStringLiteral("sender@example.com"));
    const auto pattern = shortId ? (signerPattern ? signer : m_recipientFingerprint).right(16)
        : (signerPattern ? QStringLiteral("sender@example.com") : QStringLiteral("recipient@example.com"));
    const auto encrypted = signAndEncrypt("private", signerPattern ? pattern : signer,
                                         {signerPattern ? m_recipientFingerprint : pattern}, m_sender.path());
    QCOMPARE(encrypted.status, signerPattern ? PgpEncryptStatus::NoSigningKey : PgpEncryptStatus::RecipientKeyUnusable);
    QVERIFY(encrypted.armoredCiphertext.isEmpty());
}

QTEST_GUILESS_MAIN(OpenPgpEncryptorTest)
#include "OpenPgpEncryptorTest.moc"
