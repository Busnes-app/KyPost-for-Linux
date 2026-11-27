#include "pgp/GoPgpBackend.h"
#include "kypostpgp.h"
#include "pgp/PgpFingerprint.h"
#include "stores/SecureStore.h"
#include <QCryptographicHash>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QScopeGuard>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

namespace
{
SecureStore* secretStore = nullptr;
struct IndexCommitGate
{
    std::mutex mu;
    std::condition_variable cv;
    std::thread::id ownerThread{};
    int recursionDepth = 0;
    quint64 epoch = 0;
    QHash<QString, QStringList> activeBlobs;
};
IndexCommitGate& indexCommitGate()
{
    static IndexCommitGate gate;
    return gate;
}
QString prefix(const QString& home)
{
    return QStringLiteral("pgp.go.") +
           QString::fromLatin1(QCryptographicHash::hash(home.toUtf8(), QCryptographicHash::Sha256).toHex()) +
           QLatin1Char('.');
}
QString blobHash(const QByteArray& bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()).toUpper();
}
bool validBlob(const SecureStore::BytesResult& held, const QString& expectedId)
{
    return held.status == SecureStore::ReadStatus::Found && !held.value.bytes().isEmpty() &&
           held.value.bytes().size() <= 512 * 1024 && blobHash(held.value.bytes()) == expectedId;
}
std::optional<QStringList> keyIndex(const QString& home)
{
    if (!secretStore)
        return {};
    const auto index = secretStore->read(prefix(home) + QStringLiteral("index"));
    if (index.failed())
        return std::nullopt;
    QStringList out;
    if (index.status == SecureStore::ReadStatus::Absent)
        return out;
    const auto document = QJsonDocument::fromJson(index.value.toUtf8());
    if (!document.isArray() || document.array().size() > 256)
        return std::nullopt;
    for (const auto value : document.array())
    {
        const QString fpr = normalizedFingerprint(value.toString().toLatin1());
        if (fpr.isEmpty() || out.contains(fpr))
            return std::nullopt;
        out.append(fpr);
    }
    return out;
}
PgpSignature signatureOf(const QByteArray& metadata)
{
    const auto object = QJsonDocument::fromJson(metadata).object();
    PgpSignature out;
    out.present = object.value(QStringLiteral("present")).toBool();
    out.mathematicallyValid = object.value(QStringLiteral("valid")).toBool();
    out.keyUnavailable = object.value(QStringLiteral("unavailable")).toBool();
    out.belowStrengthFloor = object.value(QStringLiteral("weak")).toBool();
    out.fingerprint = object.value(QStringLiteral("fingerprint")).toString();
    out.primaryFingerprint = object.value(QStringLiteral("primary")).toString();
    return out;
}
} // namespace
void setGoPgpSecretStore(SecureStore* store)
{
    secretStore = store;
}

GoPgpReply goPgpCall(int operation, const QByteArray& data, const QByteArray& keys, const QByteArray& extra,
                     qint64 limit)
{
    const auto result = KyPgpCall(operation, const_cast<char*>(data.constData()), size_t(data.size()),
                                  const_cast<char*>(keys.constData()), size_t(keys.size()),
                                  const_cast<char*>(extra.constData()), size_t(extra.size()), limit);
    const auto release = qScopeGuard([&] {
        KyPgpFree(result.data, result.size);
        KyPgpFree(result.metadata, result.metadata_size);
    });
    GoPgpReply out;
    out.status = result.status;
    if (!result.status)
    {
        out.data = SecureBytes(QByteArray(static_cast<const char*>(result.data), int(result.size)));
        out.metadata = QByteArray(static_cast<const char*>(result.metadata), int(result.metadata_size));
    }
    return out;
}
QJsonObject goKeyInfo(const QByteArray& key)
{
    const auto inspected = goPgpCall(1, key);
    return inspected.status == 0 ? QJsonDocument::fromJson(inspected.metadata).object() : QJsonObject{};
}
bool storeGoPgpKey(const QByteArray& key, const QString& expectedFingerprint, const QString& home)
{
    if (!secretStore)
        return false;
    auto inspected = goPgpCall(1, key);
    const auto info = QJsonDocument::fromJson(inspected.metadata).object();
    const QString fingerprint = info.value(QStringLiteral("fingerprint")).toString();
    if (inspected.status || fingerprint.isEmpty() ||
        fingerprint != normalizedFingerprint(expectedFingerprint.toLatin1()) ||
        (info.value(QStringLiteral("private")).toBool() && !info.value(QStringLiteral("unlocked")).toBool()) ||
        inspected.data.bytes().isEmpty() || inspected.data.bytes().size() > 512 * 1024)
        return false;
    const QString blobId = blobHash(inspected.data.bytes());
    const QString name = prefix(home) + blobId;
    const QString indexKey = prefix(home) + QStringLiteral("index");

    auto& gate = indexCommitGate();
    {
        std::unique_lock<std::mutex> lock(gate.mu);
        gate.cv.wait(lock, [&] {
            return gate.recursionDepth == 0 || gate.ownerThread == std::this_thread::get_id();
        });
        gate.ownerThread = std::this_thread::get_id();
        ++gate.recursionDepth;
    }
    const auto releaseGate = qScopeGuard([&] {
        std::lock_guard<std::mutex> lock(gate.mu);
        if (--gate.recursionDepth == 0)
        {
            gate.ownerThread = {};
            gate.activeBlobs.clear();
            gate.cv.notify_all();
        }
    });

    auto initialPrior = keyIndex(home);
    if (!initialPrior)
        return false;
    if (initialPrior->contains(blobId))
    {
        const auto held = secretStore->readBytes(name);
        return validBlob(held, blobId) && held.value.bytes() == inspected.data.bytes();
    }
    if (initialPrior->size() >= 256)
        return false;

    // Immutable, content-addressed entries retain historical subkeys and secrets.
    if (!secretStore->setBytes(name, inspected.data.bytes()))
        return false;
    const auto persisted = secretStore->readBytes(name);
    if (!validBlob(persisted, blobId) || persisted.value.bytes() != inspected.data.bytes())
        return false;

    bool addedActive = false;
    {
        std::lock_guard<std::mutex> lock(gate.mu);
        auto& active = gate.activeBlobs[home];
        if (!active.contains(blobId))
        {
            active.append(blobId);
            addedActive = true;
        }
    }
    bool committed = false;
    const auto rollbackActive = qScopeGuard([&] {
        if (!committed && addedActive)
        {
            std::lock_guard<std::mutex> lock(gate.mu);
            gate.activeBlobs[home].removeAll(blobId);
        }
    });

    // Serialize index updates across cross-thread calls and nested event loops
    // pumped inside SecretStore operations: if a reentrant import commits while
    // we are suspended, re-read and merge all active blobs before returning.
    for (int attempt = 0; attempt < 64; ++attempt)
    {
        quint64 startEpoch = 0;
        {
            std::lock_guard<std::mutex> lock(gate.mu);
            startEpoch = ++gate.epoch;
        }

        const auto prior = keyIndex(home);
        if (!prior)
            return false;

        QStringList index = *prior;
        {
            std::lock_guard<std::mutex> lock(gate.mu);
            for (const auto& id : gate.activeBlobs.value(home))
            {
                if (!index.contains(id))
                    index.prepend(id);
            }
        }
        if (index.size() > 256)
            return false;

        qint64 total = 0;
        for (const auto& id : index)
        {
            const auto held = secretStore->readBytes(prefix(home) + id);
            if (!validBlob(held, id))
                return false;
            total += held.value.bytes().size();
            if (total > 16 * 512 * 1024)
                return false;
        }

        {
            std::lock_guard<std::mutex> lock(gate.mu);
            if (gate.epoch != startEpoch)
                continue;
            if (index == *prior)
            {
                committed = true;
                return true;
            }
        }

        QJsonArray array;
        for (const auto& fpr : index)
            array.append(fpr);
        const QString value = QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
        if (!secretStore->set(indexKey, value))
            return false;
        const auto check = secretStore->read(indexKey);

        std::lock_guard<std::mutex> lock(gate.mu);
        if (gate.epoch == startEpoch)
        {
            committed = check.found() && check.value == value;
            return committed;
        }
    }
    return false;
}
namespace
{
std::optional<SecureBytes> tryLoadGoPgpKeys(const QString& home)
{
    SecureBytes out;
    if (!secretStore)
        return out;
    const auto index = keyIndex(home);
    if (!index)
        return std::nullopt;
    if (index->isEmpty())
        return out;
    // Reserve the bounded aggregate once, so growth leaves no private copies.
    out.bytes().reserve(16 * 512 * 1024);
    for (const auto& fpr : *index)
    {
        auto key = secretStore->readBytes(prefix(home) + fpr);
        if (!validBlob(key, fpr) || out.bytes().size() + key.value.bytes().size() > 16 * 512 * 1024)
            return std::nullopt;
        out.bytes().append(key.value.bytes());
    }
    return out;
}
} // namespace

SecureBytes loadGoPgpKeys(const QString& home)
{
    auto loaded = tryLoadGoPgpKeys(home);
    return loaded ? std::move(*loaded) : SecureBytes{};
}
PgpDecryptResult goPgpDecrypt(const QByteArray& ciphertext, const QString& home, qint64 limit)
{
    auto keys = tryLoadGoPgpKeys(home);
    if (!keys)
        return {PgpDecryptStatus::EngineUnavailable, {}, {}};
    if (keys->isEmpty())
        return {PgpDecryptStatus::NoSecretKey, {}, {}};
    auto result = goPgpCall(2, ciphertext, keys->bytes(), {}, limit);
    PgpDecryptResult out;
    out.status = result.status == 0   ? PgpDecryptStatus::Decrypted
                 : result.status == 2 ? PgpDecryptStatus::NoSecretKey
                 : result.status == 3 ? PgpDecryptStatus::TooLarge
                 : result.status == 4 ? PgpDecryptStatus::EngineUnavailable
                                      : PgpDecryptStatus::Malformed;
    if (!result.status)
    {
        out.plaintext = QByteArray(result.data.bytes().constData(), result.data.bytes().size());
        out.signature = signatureOf(result.metadata);
    }
    return out;
}
PgpVerifyResult goPgpVerify(const QByteArray& data, const QByteArray& signature, const QString& home,
                            qint64 limit)
{
    auto keys = tryLoadGoPgpKeys(home);
    if (!keys)
        return {PgpVerifyStatus::EngineUnavailable, {}};
    auto result = goPgpCall(3, data, keys->bytes(), signature, limit);
    PgpVerifyResult out;
    out.status = result.status == 0   ? PgpVerifyStatus::Checked
                 : result.status == 3 ? PgpVerifyStatus::TooLarge
                                      : PgpVerifyStatus::Malformed;
    if (!result.status)
        out.signature = signatureOf(result.metadata);
    return out;
}
bool goPgpOwnKey(const QString& fingerprint, const QString& home)
{
    if (!secretStore)
        return false;
    const QString target = normalizedFingerprint(fingerprint.toLatin1());
    if (target.isEmpty())
        return false;
    const auto index = keyIndex(home);
    if (!index)
        return false;
    for (const auto& id : *index)
    {
        auto held = secretStore->readBytes(prefix(home) + id);
        if (!validBlob(held, id))
            return false;
        const auto info = goKeyInfo(held.value.bytes());
        if (info.value(QStringLiteral("fingerprint")).toString() == target &&
            info.value(QStringLiteral("private")).toBool() && info.value(QStringLiteral("unlocked")).toBool())
            return true;
    }
    return false;
}
bool goPgpHasKey(const QString& fingerprint, const QString& home)
{
    if (!secretStore)
        return false;
    const QString target = normalizedFingerprint(fingerprint.toLatin1());
    if (target.isEmpty())
        return false;
    const auto index = keyIndex(home);
    if (!index)
        return true; // Fail closed so a corrupted index cannot silently skip revocation updates.
    for (const auto& id : *index)
    {
        auto held = secretStore->readBytes(prefix(home) + id);
        if (!validBlob(held, id))
            return true;
        const auto info = goKeyInfo(held.value.bytes());
        const QString heldFpr = info.value(QStringLiteral("fingerprint")).toString();
        if (heldFpr.isEmpty() || heldFpr == target)
            return true;
    }
    return false;
}
