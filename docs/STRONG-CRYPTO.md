# Strong cryptography profile

[Overview](../README.md) · [Security policy](../SECURITY.md)

This page covers the keys that KyPost's own code selects or accepts. The minimums come from
NIST's 2012 guidance through 2030, as used by the OpenSSF Best Practices `crypto_keylength`
criterion: symmetric 112 bits, RSA and finite-field DH 2048, discrete-log key 224, elliptic
curve 224, hash 224.

**KyPost ships one profile and enforces it by default.** No setting, environment variable or
build option weakens it. Smaller keys are refused without any configuration.

## What is enforced

| Boundary | Default | Smaller keys refused? | Source |
|---|---|---|---|
| HTTPS to the relay: mail, contacts, folders, push registration, MFA, PGP payloads, enrollment, version check | TLS 1.2 or later, peer verification, OpenSSL `SECLEVEL=2` | Yes. Refused before the request is written, for every certificate the server presents **and** every trusted root: RSA/DSA under 2048 actual bits (2047 included), EC under 224, unknown key types. `SECLEVEL=2` also refuses DH-1024, SHA-1 certificate signatures and TLS 1.0/1.1 (tested); OpenSSL documents that it refuses ECDHE groups under 224 bits (not separately tested). | `core/net/TlsPolicy.cpp`, `core/net/HttpClient.cpp` |
| HTTPS key fetch from a scanned PGP QR code | Same as above (same `HttpClient`) | Yes | same |
| Missing or non-OpenSSL TLS backend | HTTPS is refused with an install message | n/a | `HttpClient::refuseBeforeSending` |
| New OpenPGP signing and encryption | The keys you hold in GnuPG | Yes. The primary key and every usable subkey for the role need RSA, DSA or ElGamal of 2048 bits, or ECC of 224. One weak recipient refuses the whole message. A signature that gpg makes with a digest weaker than SHA-224 (for example `digest-algo SHA1` in `gpg.conf`) is discarded, not sent. | `core/pgp/PgpKeyStrength.h`, `core/pgp/OpenPgpEncryptor.cpp` |
| Incoming OpenPGP signatures | Verified by GnuPG | Yes. A valid signature from a weak key or with SHA-1/RIPEMD-160/MD5 shows "too weak to trust" and is never credited to the sender. | `core/pgp/OpenPgpDecryptor.cpp`, `core/pgp/EncryptedMessageReader.cpp` |
| Device enrollment envelope | RFC 9180 HPKE: DHKEM(P-256, HKDF-SHA256), AES-256-GCM | Only this suite is accepted | `core/pgp/DeviceEnrollmentCrypto.cpp` |
| Web Push payloads | RFC 8291: P-256 ECDH, HKDF-SHA256, AES-128-GCM | Fixed by the standard | `core/security/WebPushCrypto.cpp` |
| Local mail database | SQLCipher 4 defaults (AES-256, HMAC-SHA512 page authentication) with a random 256-bit key held in the OS secret store | Fixed | `core/security/DatabaseKeyStore.cpp`, `scripts/build-sqlcipher.sh` |
| Device credential behind the PIN gate | Argon2id (64 MiB, 3 passes) to a 256-bit AES-GCM key | Fixed | `core/security/CredentialCipher.cpp` |
| App-lock PIN verifier | Argon2id, 256-bit output. Older PBKDF2-HMAC-SHA256 records are verified once and upgraded. | Fixed | `core/security/AppLockStore.cpp` |
| Relay certificate pin | SHA-256 of the issuer's public key | Fixed | `core/net/HttpClient.cpp` |

`SECLEVEL=2` alone is not enough: OpenSSL 3.6 rounds its strength estimate, so it accepts an
RSA-2047 certificate. KyPost therefore checks each certificate's actual key size as well.

### Deliberate exceptions

- **Decrypting mail you already have.** A weak key you still hold can decrypt old mail. The
  sender chose that key when the message was encrypted; refusing to decrypt now adds no
  protection and would lose your mail. Weak keys can still be imported for the same reason.
  KyPost never deletes or rotates keys.
- **OpenPGP v4 fingerprints are SHA-1.** The in-person QR check compares the fingerprint in
  the code with one GnuPG computes from the key itself. The v4 format defines that hash, so it
  is not configurable. Forging a match needs a second preimage, not a collision. v6
  fingerprints (SHA-256) are also accepted.

## Configuring KyPost so smaller keys are completely disabled

The defaults already refuse smaller keys on every network connection and for every new
signature, encryption and signature check. To also stop legacy keys from decrypting:

1. Keep **Show images** off (the default). Remote images in a message are fetched by Qt
   WebEngine's own network stack, which this profile does not configure.
2. Delete the secret part of any RSA, DSA or ElGamal key under 2048 bits from GnuPG
   (`gpg --delete-secret-keys <fingerprint>`) once you no longer need the mail encrypted to it.
   Expiring a key is not enough: GnuPG still decrypts with expired keys. Mail encrypted only to
   a deleted key becomes unreadable, which is why KyPost will not do this for you.
3. Pair only with relays whose whole certificate chain uses RSA-2048/EC-224 or stronger.
   KyPost refuses any other relay, so this is a hosting requirement rather than a setting.

## Behaviour you may notice

- Requests use HTTP/1.1 and full TLS handshakes (no session resumption), so every certificate
  is checked before any data is sent. Keep-alive connections are still reused.
- KyPost reads the OS trust store once at startup. A root CA added later needs a restart.
  Roots under the floor are ignored. If the store cannot be read, HTTPS fails closed.
- A server whose chain contains a weak certificate, or that relies on a weak root, fails with
  a transport error that names the key minimum.

## Out of scope

These are not code that KyPost produces. They are named here so nobody assumes otherwise:

- The KyPost relay (KyPost-Server), its TLS termination and CDN, and upstream mail servers.
- Qt, OpenSSL, GnuPG/gpg-agent, SQLCipher, Argon2 and Qt WebEngine/Chromium themselves.
- The contents of the operating-system trust store and the certificate authorities in it.
- The UnifiedPush distributor and push servers, which run their own TLS outside KyPost.
- Identity providers and anything opened in the system browser (links, update and donation
  pages).
- OpenPGP key generation. KyPost does not generate OpenPGP keys; GnuPG's defaults apply.

## Verification

`TlsKeyPolicyTest` runs real TLS servers through `HttpClient` with an inherited weak default
configuration: RSA-1024/2047 leaves, intermediates and unsent trusted roots, directly trusted
weak leaves, SHA-1 signatures, TLS 1.0/1.1, DH-1024 and EC-192, each refused with zero request
bytes, plus RSA-2048, P-256, strong-intermediate and DH-2048 controls under TLS 1.2 and 1.3.
`OpenPgpEncryptorTest` and `EncryptedMessageReaderTest` cover the OpenPGP floors with real
GnuPG keys. `tests/guards.tsv` lists which guards are proven by mutation and which overlap.
