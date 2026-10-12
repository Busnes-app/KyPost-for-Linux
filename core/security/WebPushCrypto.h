#pragma once

#include "security/SecureBytes.h"

#include <QByteArray>
#include <optional>

// RFC 8291 (Web Push message encryption, aes128gcm) for UnifiedPush payloads.
//
// The relay encrypts to the P-256 key and auth secret this device registered
// (kypost-server processor/native_sender.go, UnifiedPushSender.sendEncrypted,
// via webpush-go). Without them the payload crosses the distributor's broker
// in the clear.
//
// Not KUnifiedPush's built-in ContentEncryption: it writes the private key and
// auth secret to a plain QSettings file, and on a failed decrypt it hands the
// raw bytes on as if they were the message. See UnifiedPushConnector.h.
namespace WebPushCrypto {

inline constexpr int kPrivateKeyBytes = 32;
inline constexpr int kPublicKeyBytes = 65; // uncompressed point
inline constexpr int kAuthBytes = 16;
// webpush-go emits exactly one 4096-byte body; RFC 8030 section 7.2 is the
// size every push service must carry. Anything larger is refused unread.
inline constexpr int kMaxBodyBytes = 4096;

struct Keys
{
    SecureBytes privateKey;
    QByteArray publicKey;
    SecureBytes authSecret;
};

// Fresh key pair and auth secret from the system CSPRNG.
std::optional<Keys> generate();

// Single-record aes128gcm body addressed to `keys`. nullopt on any malformed
// header, oversize body, bad padding or authentication failure.
std::optional<QByteArray> decrypt(const Keys& keys, const QByteArray& body);

// The sender side. The app never sends pushes; tests use it to stand in for
// the relay. Empty on failure.
QByteArray encrypt(const QByteArray& receiverPublicKey, const QByteArray& authSecret, const QByteArray& plaintext,
                   int recordSize = kMaxBodyBytes);

} // namespace WebPushCrypto
