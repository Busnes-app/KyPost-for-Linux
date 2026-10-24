# Assurance case

[Product overview](../README.md) · [Security](../SECURITY.md) · [Verifying releases](VERIFYING-RELEASES.md)

This is the argument that KyPost for Linux meets its security requirements: what it protects,
from whom, where the trust boundaries are, and why the design and the code hold at each one.
The rules behind it are in [AGENTS.md](../AGENTS.md) (sections 4 to 6k), and the user contract
is in [CONTRIBUTING.md](../CONTRIBUTING.md#the-user-contract). Paths are relative to the
repository root.

## What KyPost must protect

KyPost is a relay-only mail client: it talks only to the user's paired KyPost relay. Its
security requirements:

1. **Only the paired relay receives the device credential.** It goes nowhere else, even on
   a redirect or a changed certificate. The one exception is automatic re-registration (see
   [Residual risk](#residual-risk)).
2. **Mail and keys at rest are unreadable without this user's session.** The database is
   encrypted, and the device secret and database key live in the Secret Service.
3. **Hostile mail cannot act.** It cannot run script, load remote content the user did not
   ask for, navigate the reader, or reach another app through a link.
4. **OpenPGP keys stay with the user's `gpg-agent`.** KyPost asks GnuPG to sign and decrypt.
   It passes a private key to GnuPG when the key is enrolled and does not keep it.
5. **Losing the device gives up as little as possible:** the app lock, the PIN lockout and
   wipe, and Hostile Location Protection.
6. **The app never archives, deletes or moves mail by itself**
   ([CONTRIBUTING.md](../CONTRIBUTING.md#the-user-contract)).

## Threat model

| Attacker | Can | Wants |
|---|---|---|
| Network attacker, including one with a locally trusted CA | Intercept TLS, redirect | Steal the device secret or pairing token, read mail |
| Mail sender | Send any HTML, links, images and OpenPGP data | Track opens, run script, phish through the app's own UI |
| Compromised relay | Return any response | Exhaust memory, read client-encrypted mail |
| Push distributor | See push traffic | Read content, inject notifications |
| Someone at the unlocked session, or with the disk | Open the app, guess the PIN, copy files | Read mail and contacts |

Out of scope: the relay server itself
([its SECURITY.md](https://github.com/Busnes-app/KyPost-Server/blob/main/SECURITY.md)), and an
attacker who already runs code as the user. They can read the Secret Service, as any app in
the session can.

## Trust boundaries

1. **Device → relay.** HTTPS. A pairing link may use `http` only for a loopback host
   (`app/pairing/PairingController.cpp`). Redirects are allowed only within the requested
   origin by default, and replies are capped at 8 MiB (`core/net/HttpClient.cpp`). The pin is
   taken from the pairing link's `pin=` when there is one. Without `pin=`, the certificate that
   served registration is pinned on first use. The stored pin is the SHA-256 of the public key
   of the certificate that issued the server's leaf (`HttpClient::pinnedSpkiFromChain`); a pin
   of the leaf's own key also matches. On a new connection the pin is checked when the TLS
   handshake completes, before the request body is written. A reused keep-alive connection
   has no handshake, so its reply is checked after the request was sent.
2. **Mail content → reader.** Qt WebEngine with JavaScript off, a request interceptor that
   blocks remote loads until the user asks, every navigation except the app's own load
   refused, and links opened only for `http`, `https` and `mailto`
   (`app/qml/pages/EmailDetail.qml`, `app/mail/RemoteContentInterceptor.h`,
   `app/qml/utils/format.js`).
3. **App → its storage.** The database is SQLCipher, keyed from the Secret Service
   (`core/db/Database.cpp`, `core/security/DatabaseKeyStore.h`). The device secret is in the
   Secret Service through QtKeychain (`app/platform/SecureStoreKeychain.h`) and, with the
   credential gate on, sealed under the app PIN (`core/security/CredentialCipher.cpp`).
4. **App → GnuPG.** OpenPGP goes through GPGME to the user's `gpg-agent` (`core/pgp/`).
5. **Push distributor → app.** A pairing with push keys accepts only RFC 8291 `aes128gcm`
   payloads that decrypt under them; anything else is dropped. A pairing made without push
   keys takes plaintext notifications, but a sign-in challenge must always arrive encrypted
   (`PairingStore::openPushMessage`, `app/push/PushPayloadParser.cpp`).

## Secure design principles applied

- **Least privilege.** Of Qt, `core/` may link only Core, Network and Sql; its other
  libraries are libcrypto, libargon2, GPGME and, optionally, SQLCipher (AGENTS.md section 5,
  `core/CMakeLists.txt`). UI, D-Bus and the UnifiedPush connector live in `app/`.
- **Fail-safe defaults.** A pin that cannot be anchored refuses the connection. A `pin=` that
  cannot be parsed refuses the pairing link, and a supplied pin that was not enforced refuses
  to save the pairing (`DeviceRegistrationService::finishPair`). A keyring that cannot be
  read never leads to minting a new database key (`DatabaseKeyStore`). An Argon2id run that
  fails is an error, never a weaker key. An optimised build without SQLCipher stops at
  configure unless `-DKYPOST_ALLOW_PLAINTEXT_DB=ON` is given (`CMakeLists.txt`).
- **Complete mediation.** The pin is checked on the handshake and again after every reply, so
  a pooled keep-alive connection is checked too (after its request is sent, as above).
- **Economy of mechanism.** Standard, maintained libraries: OpenSSL, libargon2, SQLCipher,
  GPGME, QtKeychain.
- **Open design.** Argon2id, AES-256-GCM, SHA-256 SPKI pins, RFC 8291 and OpenPGP. All
  published.
- **Psychological acceptability.** Every trade-off is written where the user reads it
  (CONTRIBUTING.md, "Security Trade-Offs"). For example, a build without SQLCipher is
  documented as unencrypted ([docs/BUILDING.md](BUILDING.md)) and shows a "database not
  encrypted" banner (`app/qml/components/StatusBanner.qml`).

## Common weaknesses and how they are countered

| Weakness | Counter | Where |
|---|---|---|
| TLS interception | Issuer-SPKI pin from `pin=` or first use; checked on `encrypted` for a new connection, and after each reply | `core/net/HttpClient.cpp` |
| Weak TLS keys | A chain below RSA 2048 or EC 224 bits is refused, on the handshake and after the reply | `core/net/HttpClient.cpp` |
| Credential leak through a redirect | `UserVerifiedRedirectPolicy` with a same-origin validator by default | `core/net/HttpClient.cpp` |
| Memory exhaustion by a hostile relay | Replies over 8 MiB (`kMaxResponseBytes`) are aborted | `core/net/HttpClient.h`, `core/net/HttpClient.cpp` |
| Script in mail (XSS) | `settings.javascriptEnabled: false` | `app/qml/pages/EmailDetail.qml` |
| Tracking pixels and remote loads | `RemoteContentInterceptor` blocks requests until images are loaded on request | `app/mail/RemoteContentInterceptor.h` |
| Meta-refresh and in-place navigation | Only the app's own `loadHtml` navigation passes | `EmailDetail.qml` (`onNavigationRequested`) |
| Links into other apps (such as `kypost://` pairing) | Scheme allowlist: `http`, `https`, `mailto` | `app/qml/utils/format.js` |
| Markup injection in QML text | A `qmllint` gate fails any `Text` or `Label` that shows a non-literal without a safe stated format. It runs on Qt 6.11 and later, which is the Flatpak build; the `ci.yml` jobs use Qt 6.10 and skip it. The only rich-text uses (two, in the address autocomplete) escape HTML first | `tests/qml/qmllint-gate.sh`, `tests/CMakeLists.txt`, `app/qml/components/AutocompleteDropdown.qml` |
| SQL injection | `QSqlQuery::prepare` with bound values. Statements built with `arg()` take only the hex database key, schema version numbers, table names from `sqlite_master` (quoted), or the quoted path of the working copy in `ATTACH DATABASE` | `core/db/` |
| Mail at rest | SQLCipher, key in the Secret Service, `PRAGMA secure_delete = ON` for the wipe | `core/db/Database.cpp` |
| PIN guessing | 6 to 64 characters; Argon2id (64 MiB, 3 passes); 3 free attempts, then delays; wipe after 10 by default (5 to 20, or never) | `core/security/PinPolicy.h`, `core/security/CredentialCipher.h`, `core/security/LockoutPolicy.h` |
| Weak randomness | `QRandomGenerator::system()` for the database key, salts and IVs; OpenSSL `RAND_bytes` for push salts | `core/security/DatabaseKeyStore.cpp`, `AppLockStore.cpp`, `CredentialCipher.cpp`, `WebPushCrypto.cpp` |
| Memory-safety defects | `-Wall -Wextra -Wshadow`, `_FORTIFY_SOURCE=3` in optimised builds, `_GLIBCXX_ASSERTIONS`, stack protector, full RELRO; the whole suite also runs under ASan and UBSan in CI (leak detection off) | `CMakeLists.txt`, `.github/workflows/ci.yml` (`build-and-test (sanitizers)`) |
| A guard silently removed | 196 guards in `tests/guards.tsv`; `scripts/verify-guards.sh` removes each and requires its named test to fail. It is run by hand, not in CI | [CONTRIBUTING.md](../CONTRIBUTING.md#a-security-guard-is-not-proven-until-removing-it-turns-a-test-red) |
| Forged or plaintext push | RFC 8291 decryption under the pairing's keys; an unencrypted sign-in challenge is dropped | `core/security/WebPushCrypto.h`, `app/push/PushPayloadParser.cpp` |
| Tampered packages | GPG-signed OSTree commits, summary and release bundles | `.github/workflows/flatpak.yml` (`publish`), [VERIFYING-RELEASES.md](VERIFYING-RELEASES.md) |

## Residual risk

- **Automatic re-registration is trust on first use again.** At startup, and when the push
  endpoint or the transport tier changes (`app/main.cpp`), the client re-registers without the stored pin and pins whatever key
  answers. A network attacker with a locally trusted CA who waits for that moment can read
  the pairing token and have their key stored as the pin. Qt's normal chain validation still
  applies. Why this window stays open is recorded at
  `DeviceRegistrationService::reregistrationParams` (`core/domain/DeviceRegistrationService.cpp`).
- **Pairing without `pin=` is trust on first use,** and the pin names the issuing CA's key,
  so a certificate from the same issuer passes it ([SECURITY.md](../SECURITY.md)).
- **The Flatpak remote follows `main`,** and a bundle is checked only if the user passes
  `--gpg-file` ([VERIFYING-RELEASES.md](VERIFYING-RELEASES.md)).
- **A pairing made without push keys** takes plaintext mail notifications through the
  distributor (sign-in challenges are still refused unless encrypted).
- **A build without SQLCipher** keeps an unencrypted database ([docs/BUILDING.md](BUILDING.md)).
- No C++ static analyser (such as clang-tidy, cppcheck or CodeQL) runs in CI; the only static
  check is the `qmllint` gate in the Flatpak build. No fuzzer runs. No independent security
  audit has taken place.

## Evidence

- **Tests:** `ctest` runs the QtTest suites, among them `HttpClientTest`,
  `CertificatePinSinkTest`, `CertificatePinAnchorTest`, `PairingControllerTest`,
  `DeviceRegistrationServiceTest`, `CredentialCipherTest`,
  `AppLockStoreTest`, `LockoutPolicyTest`, `PinPolicyTest`, `DatabaseKeyStoreTest`,
  `WebPushCryptoTest`, `SecurityWipeTest` and `WipeTripwireTest`. It also runs the QML suite
  and, on Qt 6.11 and later, the `qmllint` gate (`tests/CMakeLists.txt`).
- **CI on every pull request:** in `ci.yml`, a Release build with the suite and, in parallel,
  the same suite under ASan and UBSan; `core/` on Qt 5.15 (`core-qt5`). In `flatpak.yml`, the
  Flatpak built and tested on x86_64 and aarch64. OpenSSF Scorecard (`scorecard.yml`) runs
  weekly and on pushes to `main`, not on pull requests.
- **Guards:** `tests/guards.tsv` and `scripts/verify-guards.sh`.
- **Review:** every pull request needs green CI and an adversarial review
  ([CONTRIBUTING.md](../CONTRIBUTING.md#every-pr-must-pass-hostile-ai-code-review)).

This case is updated when a trust boundary or a security requirement changes. A pull request
that moves one must update this page in the same change.
