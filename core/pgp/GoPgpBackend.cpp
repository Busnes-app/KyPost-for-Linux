#include "pgp/GoPgpBackend.h"
#include "kypostpgp.h"
#include "pgp/PgpFingerprint.h"
#include "stores/SecureStore.h"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QScopeGuard>
#include <optional>

namespace
{
SecureStore* secretStore = nullptr;
QString prefix(const QString& home)
{
    return QStringLiteral("pgp.go.") +
           QString::fromLatin1(QCryptographicHash::hash(home.toUtf8(), QCryptographicHash::Sha256).toHex()) +
           QLatin1Char('.');
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
        (info.value(QStringLiteral("private")).toBool() && !info.value(QStringLiteral("unlocked")).toBool()))
        return false;
    const QString blobId =
        QString::fromLatin1(
            QCryptographicHash::hash(inspected.data.bytes(), QCryptographicHash::Sha256).toHex())
            .toUpper();
    auto prior = keyIndex(home);
    if (!prior)
        return false;
    if (prior->contains(blobId))
    {
        const auto held = secretStore->readBytes(prefix(home) + blobId);
        return held.status == SecureStore::ReadStatus::Found && held.value.bytes() == inspected.data.bytes();
    }
    QStringList index = *prior;
    if (!index.contains(blobId))
        index.prepend(blobId);
    if (index.size() > 256)
        return false;
    qint64 total = 0;
    for (const auto& id : *prior)
    {
        auto held = secretStore->readBytes(prefix(home) + id);
        if (held.status != SecureStore::ReadStatus::Found || held.value.bytes().size() > 512 * 1024)
            return false;
        total += held.value.bytes().size();
    }
    if (!prior->contains(blobId))
        total += inspected.data.bytes().size();
    if (total > 16 * 512 * 1024)
        return false;
    const QString name = prefix(home) + blobId;
    // Immutable, content-addressed entries retain historical subkeys and secrets.
    // NetworkExecutor is the single index writer; no other process writes this
    // namespace.
    if (!secretStore->setBytes(name, inspected.data.bytes()))
        return false;
    const auto persisted = secretStore->readBytes(name);
    if (persisted.status != SecureStore::ReadStatus::Found ||
        persisted.value.bytes() != inspected.data.bytes())
        return false;
    QJsonArray array;
    for (const auto& fpr : index)
        array.append(fpr);
    const QString value = QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
    if (!secretStore->set(prefix(home) + QStringLiteral("index"), value))
        return false;
    const auto check = secretStore->read(prefix(home) + QStringLiteral("index"));
    return check.found() && check.value == value;
}
SecureBytes loadGoPgpKeys(const QString& home)
{
    SecureBytes out;
    if (!secretStore)
        return out;
    const auto index = keyIndex(home);
    // Reserve the bounded aggregate once, so growth leaves no private copies.
    out.bytes().reserve(16 * 512 * 1024);
    if (!index)
        return {};
    for (const auto& fpr : *index)
    {
        auto key = secretStore->readBytes(prefix(home) + fpr);
        if (key.status != SecureStore::ReadStatus::Found || key.value.bytes().size() > 512 * 1024 ||
            out.bytes().size() + key.value.bytes().size() > 16 * 512 * 1024)
            return {};
        out.bytes().append(key.value.bytes());
    }
    return out;
}
PgpDecryptResult goPgpDecrypt(const QByteArray& ciphertext, const QString& home, qint64 limit)
{
    auto keys = loadGoPgpKeys(home);
    auto result = goPgpCall(2, ciphertext, keys.bytes(), {}, limit);
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
    auto keys = loadGoPgpKeys(home);
    auto result = goPgpCall(3, data, keys.bytes(), signature, limit);
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
    const auto index = keyIndex(home);
    if (!index)
        return false;
    for (const auto& id : *index)
    {
        auto held = secretStore->readBytes(prefix(home) + id);
        if (held.status != SecureStore::ReadStatus::Found)
            return false;
        const auto info = goKeyInfo(held.value.bytes());
        if (info.value(QStringLiteral("fingerprint")).toString() == fingerprint &&
            info.value(QStringLiteral("private")).toBool() && info.value(QStringLiteral("unlocked")).toBool())
            return true;
    }
    return false;
}
