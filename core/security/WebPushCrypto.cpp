#include "security/WebPushCrypto.h"

#include <QtEndian>

#include <openssl/core_names.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/param_build.h>
#include <openssl/rand.h>

#include <memory>
#include <utility>

namespace WebPushCrypto {
namespace {

template<typename T, void (*F)(T*)>
struct Free
{
    void operator()(T* p) const { F(p); }
};
using Pkey = std::unique_ptr<EVP_PKEY, Free<EVP_PKEY, EVP_PKEY_free>>;
using PkeyCtx = std::unique_ptr<EVP_PKEY_CTX, Free<EVP_PKEY_CTX, EVP_PKEY_CTX_free>>;
using CipherCtx = std::unique_ptr<EVP_CIPHER_CTX, Free<EVP_CIPHER_CTX, EVP_CIPHER_CTX_free>>;
using ParamBld = std::unique_ptr<OSSL_PARAM_BLD, Free<OSSL_PARAM_BLD, OSSL_PARAM_BLD_free>>;
using Params = std::unique_ptr<OSSL_PARAM, Free<OSSL_PARAM, OSSL_PARAM_free>>;
using Bn = std::unique_ptr<BIGNUM, Free<BIGNUM, BN_clear_free>>;

// RFC 8188 header: salt(16) | rs(4) | idlen(1) | keyid(65, the sender's public key).
constexpr int kSaltBytes = 16;
constexpr int kHeaderBytes = kSaltBytes + 4 + 1 + kPublicKeyBytes;
constexpr int kTagBytes = 16;

const uchar* u(const QByteArray& b) { return reinterpret_cast<const uchar*>(b.constData()); }
uchar* u(QByteArray& b) { return reinterpret_cast<uchar*>(b.data()); }

// Without `priv` this is a public key. Either way the point is checked to be on the curve.
Pkey loadP256(const QByteArray& priv, const QByteArray& pub)
{
    if (pub.size() != kPublicKeyBytes || (!priv.isEmpty() && priv.size() != kPrivateKeyBytes))
        return {};
    ParamBld bld(OSSL_PARAM_BLD_new());
    Bn bn(priv.isEmpty() ? nullptr : BN_bin2bn(u(priv), kPrivateKeyBytes, nullptr));
    if (!bld || !OSSL_PARAM_BLD_push_utf8_string(bld.get(), OSSL_PKEY_PARAM_GROUP_NAME, "prime256v1", 0)
        || !OSSL_PARAM_BLD_push_octet_string(bld.get(), OSSL_PKEY_PARAM_PUB_KEY, pub.constData(), kPublicKeyBytes)
        || (bn && !OSSL_PARAM_BLD_push_BN(bld.get(), OSSL_PKEY_PARAM_PRIV_KEY, bn.get())))
        return {};
    Params params(OSSL_PARAM_BLD_to_param(bld.get()));
    PkeyCtx ctx(EVP_PKEY_CTX_new_from_name(nullptr, "EC", nullptr));
    EVP_PKEY* raw = nullptr;
    if (!params || !ctx || EVP_PKEY_fromdata_init(ctx.get()) != 1
        || EVP_PKEY_fromdata(ctx.get(), &raw, bn ? EVP_PKEY_KEYPAIR : EVP_PKEY_PUBLIC_KEY, params.get()) != 1)
        return {};
    Pkey key(raw);
    PkeyCtx check(EVP_PKEY_CTX_new(key.get(), nullptr));
    if (!check || EVP_PKEY_public_check(check.get()) != 1)
        return {};
    return key;
}

SecureBytes hmac(const QByteArray& key, const QByteArray& data)
{
    SecureBytes out(QByteArray(32, 0));
    unsigned int len = 0;
    if (!HMAC(EVP_sha256(), key.constData(), int(key.size()), u(data), size_t(data.size()), u(out.bytes()), &len)
        || len != 32)
        return {};
    return out;
}

// HKDF-Expand for a single block (length <= 32).
SecureBytes hkdfExpand(const SecureBytes& prk, const QByteArray& info, int length)
{
    SecureBytes block = hmac(prk.bytes(), info + '\x01');
    block.bytes().truncate(length);
    return block;
}

std::optional<QByteArray> aes128Gcm(bool encrypt, const SecureBytes& key, const SecureBytes& iv,
                                    const QByteArray& input)
{
    if (key.bytes().size() != 16 || iv.bytes().size() != 12 || (!encrypt && input.size() < kTagBytes))
        return std::nullopt;
    const QByteArray body = encrypt ? input : input.left(input.size() - kTagBytes);
    QByteArray tag = encrypt ? QByteArray(kTagBytes, 0) : input.right(kTagBytes);
    QByteArray out(body.size(), 0);
    CipherCtx ctx(EVP_CIPHER_CTX_new());
    int len = 0;
    int tail = 0;
    if (!ctx
        || EVP_CipherInit_ex(ctx.get(), EVP_aes_128_gcm(), nullptr, u(key.bytes()), u(iv.bytes()), encrypt ? 1 : 0)
            != 1
        || EVP_CipherUpdate(ctx.get(), u(out), &len, u(body), int(body.size())) != 1
        || (!encrypt && EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, kTagBytes, tag.data()) != 1)
        || EVP_CipherFinal_ex(ctx.get(), u(out) + len, &tail) != 1)
        return std::nullopt;
    if (encrypt) {
        if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, kTagBytes, tag.data()) != 1)
            return std::nullopt;
        out += tag;
    }
    return out;
}

struct ContentKeys
{
    SecureBytes key;
    SecureBytes nonce;
};

// RFC 8291 section 3.4: ECDH between `ownPriv`/`ownPub` and `peerPub`, then the content key and nonce.
// uaPub/asPub are the receiver's and sender's public keys, whichever side we are.
std::optional<ContentKeys> contentKeys(const QByteArray& ownPriv, const QByteArray& ownPub, const QByteArray& peerPub,
                                       const QByteArray& uaPub, const QByteArray& asPub, const QByteArray& auth,
                                       const QByteArray& salt)
{
    Pkey own = loadP256(ownPriv, ownPub);
    Pkey peer = loadP256({}, peerPub);
    if (!own || !peer)
        return std::nullopt;
    PkeyCtx ctx(EVP_PKEY_CTX_new(own.get(), nullptr));
    size_t len = 32;
    SecureBytes ecdh(QByteArray(32, 0));
    if (!ctx || EVP_PKEY_derive_init(ctx.get()) != 1 || EVP_PKEY_derive_set_peer(ctx.get(), peer.get()) != 1
        || EVP_PKEY_derive(ctx.get(), u(ecdh.bytes()), &len) != 1 || len != 32)
        return std::nullopt;
    const SecureBytes ikm = hkdfExpand(hmac(auth, ecdh.bytes()), QByteArray("WebPush: info", 14) + uaPub + asPub, 32);
    const SecureBytes prk = hmac(salt, ikm.bytes());
    ContentKeys keys{ hkdfExpand(prk, QByteArray("Content-Encoding: aes128gcm", 28), 16),
                      hkdfExpand(prk, QByteArray("Content-Encoding: nonce", 24), 12) };
    if (keys.key.bytes().size() != 16 || keys.nonce.bytes().size() != 12)
        return std::nullopt;
    return keys;
}

} // namespace

std::optional<Keys> generate()
{
    Pkey key(EVP_EC_gen("P-256"));
    BIGNUM* raw = nullptr;
    if (!key || EVP_PKEY_get_bn_param(key.get(), OSSL_PKEY_PARAM_PRIV_KEY, &raw) != 1)
        return std::nullopt;
    const Bn bn(raw);
    Keys out{ SecureBytes(QByteArray(kPrivateKeyBytes, 0)), QByteArray(kPublicKeyBytes, 0),
              SecureBytes(QByteArray(kAuthBytes, 0)) };
    size_t len = 0;
    if (BN_bn2binpad(bn.get(), u(out.privateKey.bytes()), kPrivateKeyBytes) != kPrivateKeyBytes
        || EVP_PKEY_get_octet_string_param(key.get(), OSSL_PKEY_PARAM_PUB_KEY, u(out.publicKey), kPublicKeyBytes,
                                           &len)
            != 1
        || len != size_t(kPublicKeyBytes) || RAND_bytes(u(out.authSecret.bytes()), kAuthBytes) != 1)
        return std::nullopt;
    return out;
}

std::optional<QByteArray> decrypt(const Keys& keys, const QByteArray& body)
{
    if (keys.authSecret.bytes().size() != kAuthBytes || body.size() > kMaxBodyBytes
        || body.size() < kHeaderBytes + kTagBytes + 1 || quint8(body.at(20)) != kPublicKeyBytes)
        return std::nullopt;
    const quint32 rs = qFromBigEndian<quint32>(body.constData() + kSaltBytes);
    const QByteArray record = body.mid(kHeaderBytes);
    // One record only, as RFC 8291 section 4 requires of the sender.
    if (rs < quint32(kTagBytes + 2) || quint32(record.size()) > rs)
        return std::nullopt;
    const QByteArray senderPub = body.mid(21, kPublicKeyBytes);

    const std::optional<ContentKeys> ck = contentKeys(keys.privateKey.bytes(), keys.publicKey, senderPub,
                                                       keys.publicKey, senderPub, keys.authSecret.bytes(),
                                                       body.left(kSaltBytes));
    if (!ck)
        return std::nullopt;
    std::optional<QByteArray> plain = aes128Gcm(false, ck->key, ck->nonce, record);
    if (!plain)
        return std::nullopt;
    // Zero padding, then the last-record delimiter 0x02 (RFC 8188 section 2).
    qsizetype end = plain->size() - 1;
    while (end >= 0 && plain->at(end) == 0)
        --end;
    if (end < 0 || plain->at(end) != '\x02')
        return std::nullopt;
    plain->truncate(end);
    return plain;
}

QByteArray encrypt(const QByteArray& receiverPublicKey, const QByteArray& authSecret, const QByteArray& plaintext,
                   int recordSize)
{
    // webpush-go's layout: rs = recordSize, and the whole body is recordSize bytes.
    const qsizetype pad = recordSize - kHeaderBytes - kTagBytes - 1 - plaintext.size();
    if (authSecret.size() != kAuthBytes || pad < 0)
        return {};
    const std::optional<Keys> sender = generate();
    if (!sender)
        return {};
    QByteArray salt(kSaltBytes, 0);
    if (RAND_bytes(u(salt), kSaltBytes) != 1)
        return {};
    const std::optional<ContentKeys> ck = contentKeys(sender->privateKey.bytes(), sender->publicKey,
                                                       receiverPublicKey, receiverPublicKey, sender->publicKey,
                                                       authSecret, salt);
    const std::optional<QByteArray> record =
        ck ? aes128Gcm(true, ck->key, ck->nonce, plaintext + '\x02' + QByteArray(pad, 0)) : std::nullopt;
    if (!record)
        return {};
    QByteArray header = salt;
    header.resize(kSaltBytes + 4);
    qToBigEndian<quint32>(quint32(recordSize), header.data() + kSaltBytes);
    header += char(kPublicKeyBytes);
    return header + sender->publicKey + *record;
}

} // namespace WebPushCrypto
