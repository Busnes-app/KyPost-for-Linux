#include "push/PushArrival.h"

#include "db/Database.h"
#include "db/PushDao.h"
#include "domain/DevicePairing.h"
#include "domain/PairingStore.h"
#include "domain/PushRepository.h"
#include "models/PushNotification.h"
#include "net/HttpClient.h"
#include "net/PushNotificationClient.h"
#include "security/WebPushCrypto.h"
#include "stores/CursorStore.h"
#include "stores/SecureStoreFile.h"
#include "stores/SettingsStore.h"

#include <QNetworkAccessManager>
#include <QTemporaryDir>
#include <QTest>

#include <functional>
#include <memory>
#include <utility>

namespace {

constexpr auto kPushKeysKey = "pairing.webPushKeys";
const QByteArray kOldPushJson =
    R"({"title":"t","data":{"messageId":"old-1","senderName":"Previous","emailSubject":"Previous account"}})";

WebPushCrypto::Keys freshKeys()
{
    std::optional<WebPushCrypto::Keys> keys = WebPushCrypto::generate();
    if (!keys)
        qFatal("key generation failed");
    return std::move(*keys);
}

DevicePairing pairingFor(const QString& subscriberId, const QString& deviceId)
{
    DevicePairing pairing;
    pairing.subscriberId = subscriberId;
    pairing.deviceSecret = QStringLiteral("secret-") + deviceId;
    pairing.serverBaseUrl = QStringLiteral("https://relay.example.com");
    pairing.registrationUrl = QStringLiteral("https://relay.example.com/api/notifications/native/register");
    pairing.pairingToken = QStringLiteral("pair-tok");
    pairing.deviceId = deviceId;
    pairing.deviceName = QStringLiteral("My Linux Desktop");
    return pairing;
}

// SecureStoreFile, plus a hook that runs in the middle of the push-key read.
// The keychain backend answers that read from a nested event loop, so
// anything else the GUI thread does -- an account replacement completing --
// can run inside it. The value returned is the one read BEFORE the hook ran,
// which is what the real backend hands back: the previous pairing's keys.
class InterleavingSecureStore : public SecureStore
{
public:
    explicit InterleavingSecureStore(const QString& directoryPath)
        : m_file(directoryPath)
    {
    }

    ReadResult read(const QString& key) const override
    {
        if (key == QLatin1String(kPushKeysKey)) {
            ++m_pushKeyReads;
            const ReadResult before = m_file.read(key);
            if (m_duringPushKeyRead) {
                const std::function<void()> hook = std::exchange(m_duringPushKeyRead, {});
                hook();
            }
            return before;
        }
        return m_file.read(key);
    }
    bool set(const QString& key, const QString& value) override { return m_file.set(key, value); }
    std::optional<QString> get(const QString& key) const override { return m_file.get(key); }
    bool remove(const QString& key) override { return m_file.remove(key); }
    bool contains(const QString& key) const override { return m_file.contains(key); }

    // One-shot.
    void duringNextPushKeyRead(std::function<void()> hook) { m_duringPushKeyRead = std::move(hook); }
    int pushKeyReads() const { return m_pushKeyReads; }

private:
    SecureStoreFile m_file;
    mutable std::function<void()> m_duringPushKeyRead;
    mutable int m_pushKeyReads = 0;
};

// Everything PushRepository needs, on throwaway storage.
struct Fixture
{
    QTemporaryDir secureDir;
    QTemporaryDir cursorDir;
    QTemporaryDir settingsDir;
    Database db;
    std::unique_ptr<PushDao> pushDao;
    std::unique_ptr<InterleavingSecureStore> secureStore;
    std::unique_ptr<PairingStore> pairingStore;
    std::unique_ptr<CursorStore> cursorStore;
    std::unique_ptr<SettingsStore> settingsStore;
    QNetworkAccessManager manager;
    std::unique_ptr<HttpClient> http;
    std::unique_ptr<PushNotificationClient> client;
    std::unique_ptr<PushRepository> repository;

    bool open()
    {
        if (!secureDir.isValid() || !cursorDir.isValid() || !settingsDir.isValid())
            return false;
        if (!db.open(QStringLiteral(":memory:")))
            return false;
        pushDao = std::make_unique<PushDao>(db.handle());
        secureStore = std::make_unique<InterleavingSecureStore>(secureDir.path());
        pairingStore = std::make_unique<PairingStore>(*secureStore);
        cursorStore = std::make_unique<CursorStore>(cursorDir.filePath(QStringLiteral("cursors.ini")));
        settingsStore = std::make_unique<SettingsStore>(settingsDir.filePath(QStringLiteral("settings.ini")));
        http = std::make_unique<HttpClient>(manager);
        client = std::make_unique<PushNotificationClient>(*http);
        repository =
            std::make_unique<PushRepository>(*pushDao, *cursorStore, *client, *pairingStore, *settingsStore);
        return true;
    }
};

} // namespace

class PushArrivalTest : public QObject
{
    Q_OBJECT

private slots:
    void aPushForTheCurrentPairingIsRecordedAndShown();
    void aPushOpenedForAReplacedPairingIsNeitherRecordedNorShown();
    void aPushWithNoPairingIsDroppedUnopened();
};

// Control for the test below: the same path with no replacement delivers, so
// a red result there is the ownership check and not a broken fixture.
void PushArrivalTest::aPushForTheCurrentPairingIsRecordedAndShown()
{
    Fixture f;
    QVERIFY(f.open());
    QVERIFY(f.pairingStore->save(pairingFor(QStringLiteral("sub-old"), QStringLiteral("device-old"))));
    const WebPushCrypto::Keys keys = freshKeys();
    QVERIFY(f.pairingStore->savePushKeys(keys));

    QVector<PushNotification> shown;
    const PushArrival::Outcome outcome = PushArrival::handle(
        WebPushCrypto::encrypt(keys.publicKey, keys.authSecret.bytes(), kOldPushJson), *f.pairingStore,
        *f.repository, [&shown](const PushNotification& payload) { shown.append(payload); }, 1731000000000);

    QCOMPARE(outcome, PushArrival::Outcome::Delivered);
    QCOMPARE(shown.size(), 1);
    QCOMPARE(shown.first().messageId, QStringLiteral("old-1"));
    QCOMPARE(f.repository->history().size(), 1);
}

// The account is replaced while the push keys are being read, and the read
// then returns the previous account's keys. The payload decrypts -- it was
// addressed to those keys -- but it belongs to an account this device no
// longer holds, so it must leave no history row and raise no notification.
void PushArrivalTest::aPushOpenedForAReplacedPairingIsNeitherRecordedNorShown()
{
    Fixture f;
    QVERIFY(f.open());
    QVERIFY(f.pairingStore->save(pairingFor(QStringLiteral("sub-old"), QStringLiteral("device-old"))));
    const WebPushCrypto::Keys oldKeys = freshKeys();
    QVERIFY(f.pairingStore->savePushKeys(oldKeys));

    const WebPushCrypto::Keys newKeys = freshKeys();
    bool replaced = false;
    f.secureStore->duringNextPushKeyRead([&] {
        replaced = f.pairingStore->save(pairingFor(QStringLiteral("sub-new"), QStringLiteral("device-new")))
                   && f.pairingStore->savePushKeys(newKeys);
    });

    QVector<PushNotification> shown;
    const PushArrival::Outcome outcome = PushArrival::handle(
        WebPushCrypto::encrypt(oldKeys.publicKey, oldKeys.authSecret.bytes(), kOldPushJson), *f.pairingStore,
        *f.repository, [&shown](const PushNotification& payload) { shown.append(payload); }, 1731000000000);

    // The replacement really did land mid-read, and the read really did hand
    // back the old keys -- otherwise this would pass for the wrong reason.
    QVERIFY(replaced);
    QCOMPARE(f.pairingStore->currentIdentity().subscriberId, QStringLiteral("sub-new"));
    QVERIFY(!f.pairingStore
                 ->openPushMessage(WebPushCrypto::encrypt(oldKeys.publicKey, oldKeys.authSecret.bytes(), kOldPushJson))
                 .has_value());

    QCOMPARE(outcome, PushArrival::Outcome::PairingChanged);
    QVERIFY(shown.isEmpty());
    QVERIFY(f.repository->history().isEmpty());
}

// A pairing made before push keys existed is read as cleartext, and so is a
// store with no pairing at all. There is no account to file it under, so it
// is dropped before the keys are even read.
void PushArrivalTest::aPushWithNoPairingIsDroppedUnopened()
{
    Fixture f;
    QVERIFY(f.open());

    QVector<PushNotification> shown;
    const PushArrival::Outcome outcome = PushArrival::handle(
        kOldPushJson, *f.pairingStore, *f.repository,
        [&shown](const PushNotification& payload) { shown.append(payload); }, 1731000000000);

    QCOMPARE(outcome, PushArrival::Outcome::NotPaired);
    QCOMPARE(f.secureStore->pushKeyReads(), 0);
    QVERIFY(shown.isEmpty());
    QVERIFY(f.repository->history().isEmpty());
}

QTEST_GUILESS_MAIN(PushArrivalTest)
#include "PushArrivalTest.moc"
