#pragma once

#include "pgp/OpenPgpDecryptor.h"
#include "security/SecureBytes.h"
#include <QJsonObject>
#include <QStringList>

class SecureStore;
// Install once at the composition root, before the network worker starts.
// The store outlives all PGP operations. No fallback to a plaintext file store.
void setGoPgpSecretStore(SecureStore* store);
struct GoPgpReply
{
    int status = 1;
    SecureBytes data;
    QByteArray metadata;
};
// Borrowed inputs, owned output. Both Go and C copies are cleared before free.
// Operations: 1 inspect, 2 decrypt, 3 detached verify, 4 X-Wing generate,
// 5 derive public key, 6 open library framing, 7 seal library framing,
// 8 strictly decode and open a v6 JSON envelope.
GoPgpReply goPgpCall(int operation, const QByteArray& data = {}, const QByteArray& keys = {},
                     const QByteArray& extra = {}, qint64 limit = OpenPgpDecryptor::kMaxPlaintextBytes);
QJsonObject goKeyInfo(const QByteArray& key);
bool storeGoPgpKey(const QByteArray& key, const QString& expectedFingerprint, const QString& home = {});
SecureBytes loadGoPgpKeys(const QString& home = {});
PgpDecryptResult goPgpDecrypt(const QByteArray& ciphertext, const QString& home, qint64 limit);
PgpVerifyResult goPgpVerify(const QByteArray& data, const QByteArray& signature, const QString& home,
                            qint64 limit);
bool goPgpOwnKey(const QString& fingerprint, const QString& home = {});
bool goPgpHasKey(const QString& fingerprint, const QString& home = {});
