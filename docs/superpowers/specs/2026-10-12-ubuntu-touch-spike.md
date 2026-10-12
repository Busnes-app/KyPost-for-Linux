# KyPost Ubuntu Touch spike (Qt 5.15, ubuntu-touch-24.04-1.x, clickable 8.9)

Date: 2026-10-12. Research only. Nothing was built and nothing was tested on a device.

Labels: **PROVEN** means a cited primary source or direct inspection. **LIKELY** means
strong indirect evidence. **UNKNOWN** means there is no evidence. Answers to Q1-7 come from a
web-research helper; I relay its labels and did not re-fetch its sources. Where local inspection contradicts it (Q3 driver), local wins and says so. Q8 comes
from my own grep of `kypost-Linux/core`.

## Proven patterns from KyAuth-Linux (direct inspection)

- `clickable.yaml`: `builder: cmake`, `framework: ubuntu-touch-24.04-1.x`, build arg
  `-DKYAUTH_UBUNTU_TOUCH=ON`, `clickable_minimum_required: 8.9.0`.
- `packaging/click/manifest.json.in` declares two hooks. `kyauth` uses apparmor + desktop.
  `push` uses `push-helper.apparmor` (template `ubuntu-push-helper`) + `push-helper.json`
  (`exec`, `app_id: kyauth.busnes_kyauth`).
- App AppArmor policy groups: `networking` and `push-notification-client`.
- `push-helper` is a 6-line Python passthrough. The server sends a fixed notification plus
  an encrypted `message`.
- `qml/UbuntuPush.qml`: `import Lomiri.PushNotifications 0.1`; `PushClient { appId;
  onTokenChanged; onNotificationsChanged; clearPersistent([...]) }`.
- CMake: `find_package(QT NAMES Qt6 Qt5)` then `Qt${QT_VERSION_MAJOR}`, so one source tree
  builds against both Qt 5.15 and Qt 6. KyAuth uses C++17.
- `docs/push-contract.md`: `POST https://push.ubports.com/notify` with
  `{appid, token, expire_on, data}`, and `data` must be at most 4096 bytes (`MaxUnicastPayload`).
- Vault: AES-256-GCM, key from scrypt over the passcode. The README measures a 6-digit PIN
  falling in about 6 h offline on 20 cores. QR pairing: scan with Tagger, then paste.
  KyAuth has no in-app scanning.

## 1. QtWebEngine for HTML mail

- QtWebEngine 5.15.19 ships with the framework, so we don't bundle it. **PROVEN** from
  package metadata: `ubuntu-sdk-libs` depends on `qml-module-qtwebengine`, `-qtwebview` and
  `-morph-web`, and the cross image installs `qtwebengine5-dev`.
  http://repo.ubports.com/dists/24.04-1.x/main/binary-arm64/Packages.gz ·
  https://gitlab.com/clickable/clickable-docker-images/-/blob/master/ubuntu-sdk/generic-cross-build/Dockerfile
- Policy group `webview` (it lets QtWebEngineProcess run under a helper subprofile). **PROVEN**.
  https://docs.ubports.com/en/latest/appdev/platform/apparmor.html ·
  https://gitlab.com/ubports/development/core/apparmor-easyprof-ubuntu/-/raw/ubports/24.04-1.x/data/policygroups/ubuntu/20.04/webview
- JS off: `settings.javascriptEnabled: false` from QML. **PROVEN**.
  https://doc.qt.io/qt-5/qml-qtwebengine-webenginesettings.html
- Blocking remote resources and handling a custom scheme need C++. The QML `WebEngineProfile`
  exposes neither an interceptor nor a scheme handler. You subclass `QQuickWebEngineProfile`,
  call `setRequestInterceptor`, and use `installUrlSchemeHandler`. The scheme must go through
  `QWebEngineUrlScheme::registerScheme()` before `QGuiApplication` is constructed. **PROVEN**.
  https://doc.qt.io/qt-5/qml-qtwebengine-webengineprofile.html ·
  https://doc.qt.io/qt-5/qwebengineurlschemehandler.html
- Prior art: Dekko2 at https://gitlab.com/dekkan/dekko (master 73c29431) does exactly this.
  It turns off JS, clipboard, file-URL access, localStorage and hyperlink auditing. It
  subclasses the profile with an interceptor plus handlers for `cid`, `dekko-part` and
  `dekko-msg`. **PROVEN** from source. Its interceptor **allows any MainFrame request**. Don't
  copy that: block every request whose scheme isn't ours, main frame included, and open links
  externally on navigation instead. The OpenStore 24.04 build matching master is **LIKELY**.
  https://open-store.io/api/v4/apps/dekko2.dekkoproject
- Alternatives:
  - Morph.Web wraps QtWebEngine, so it gains nothing. **PROVEN** (Depends).
  - QtWebView ships but has no interceptor API. **PROVEN** that it ships; the "no interceptor"
    part is Qt API.
  - `Text.RichText` loads nothing remote and runs no JS. How well it renders real mail is
    **UNKNOWN**.
- Required test: render a mail containing `<img src=https://…>` and assert zero requests
  leave the process.

## 2. GnuPG / GPGME in confinement

- gpg, gpg-agent (gnupg 2.4.4) and libgpgme11t64 1.18.0 are in the image through
  `ubuntu-sdk-libs → kirigami2 → … → libkf5wallet5 → libgpgmepp6t64`. **PROVEN** by dependency
  closure only, not on a device. Because the dependency is transitive and not a promised API,
  it could disappear.
- Writable dirs come from the `ubuntu-sdk` template: `~/.local/share/<pkg>/` (mrwklix),
  `~/.cache/<pkg>/`, `~/.config/<pkg>/` and `/run/user/*/<pkg>/`. There is **no** rule for
  `/run/user/*/gnupg`. **PROVEN**.
  https://gitlab.com/ubports/development/core/apparmor-easyprof-ubuntu/-/raw/ubports/24.04-1.x/data/templates/ubuntu/20.04/ubuntu-sdk ·
  https://docs.ubports.com/en/latest/appdev/guides/writeable-dirs.html
- Socket location: gnupg's `_gnupg_socketdir_internal` tries `/run/user/UID/gnupg[/d.<hash>]`
  first. If that fails, "If nothing works fall back to the homedir". **PROVEN** from source.
  That the AppArmor denial actually triggers the fallback on a device is **LIKELY**.
  https://raw.githubusercontent.com/gpg/gnupg/gnupg-2.4.4/common/homedir.c
- Agent socket permitted: `abstractions/base` allows unix create and same-label peers, and the
  agent inherits the app's label. **LIKELY**; needs a device test.
  https://gitlab.com/apparmor/apparmor/-/raw/v4.0.1/profiles/apparmor.d/abstractions/base
- Exec: core runs `gpgconf` through `QProcess` (`OpenPgpKeyImporter.cpp:482,506`), and gpgme
  spawns `gpg`/`gpg-agent` from `/usr/bin`. Whether the template allows executing those exact
  binaries is **UNKNOWN**. Bundling them with `install_bin` (the click dir has `ix`) is the
  fallback, **LIKELY**.
- Pinentry: avoid it. Keep loopback or no-protection keys as core does today. **LIKELY** fine.
- UT apps doing PGP: "GPG-Text" (`gpg-text.com.tino.gpgtext`, focal arm64) and "Enigma"
  (`enigma.hummlbach`, xenial). **PROVEN** they exist (https://open-store.io/api/v4/apps?search=gpg).
  How they run gpg was not checked. Dekko has **no** PGP code (grepped).

## 3. SQLCipher in a click

- `libsqlcipher-dev` 4.5.6 is in noble universe, but its SONAME is renamed. No Qt5 qsqlcipher
  driver package exists. **PROVEN**. https://packages.ubuntu.com/noble/libsqlcipher-dev
- **KyPost needs no driver.** `scripts/build-sqlcipher.sh` builds SQLCipher 4.14.0 with SONAME
  `libsqlite3.so.0`, linked into `kypostcore` (`core/CMakeLists.txt:239-274`). The stock
  `QSQLITE` plugin then resolves to it, as measured with `LD_DEBUG=libs` on desktop. **PROVEN**
  by inspection. The framework ships the stock plugin (`libqt5sql5-sqlite` is in
  ubuntu-sdk-libs, **PROVEN**). That the same trick works there is **LIKELY**. The helper's
  sjemens/qsqlcipher-qt5 suggestion is unnecessary.
- Clickable: use `libraries:` (custom builder → `build-sqlcipher.sh`) or `prebuild`, plus
  `install_lib` into `${CLICK_LD_LIBRARY_PATH}`. **PROVEN** the keys exist
  (https://clickable-ut.dev/en/latest/project-config.html); the page has no 24.04 example. The
  app must have a direct DT_NEEDED on our lib so it loads before any framework lib maps the
  system sqlite.
- Safe failure: `Database.cpp:149-160` refuses a DB when `PRAGMA key` was silently ignored.
  **PROVEN**.

## 4. Secret storage without Secret Service

- gnome-keyring, libsecret and `libqt5keychain1` (QtKeychain 0.14.2) are on the image.
  However, the `ubuntu-sdk` template has no D-Bus rule for `org.freedesktop.secrets` or
  kwallet, so QtKeychain has no reachable backend. Unreachable is **LIKELY**. Whether a keyring
  daemon even runs in the session is **UNKNOWN**.
- Dekko uses Online Accounts (signond, `accounts` group) for OAuth providers. **PROVEN**. That
  is for provider credentials, not arbitrary app secrets. Plain files in `~/.local/share/<pkg>`
  is the common pattern (KyAuth's vault). **LIKELY**; no survey was done.
- Recommendation:
  - Implement core's `SecureStore` (`stores/SecureStore.h`) as one AES-256-GCM file. Seal it
    with core's existing `CredentialCipher` (Argon2id, 64 MiB). Do not use QtKeychain.
  - The same seal wraps the SQLCipher key (`security/DatabaseKeyStore`).
  - Keep backoff and wipe-after-N, and fail closed on an unreadable store.
  - With no hardware keystore, a copied data dir allows offline guessing, and a 6-digit PIN is
    10^6 tries. Recommend a passphrase in the UI.
  - Measure Argon2 per-guess cost on a phone and on a GPU before quoting a number. **UNKNOWN**
    today.

## 5. Camera / QR

- The `camera`, `video` and `content_exchange` policy groups exist (**PROVEN**).
  `qml-module-qtmultimedia` 5.15.13, `libqzxing3` and `qml-module-lomiri-content` are
  ubuntu-sdk-libs deps (**PROVEN**). `qml-module-qzxing` is in the UBports repo but not in the
  closure, so it must be bundled. The QML `Camera` working through `cameraplugin-aal` on a
  device is **LIKELY**.
- Tagger exists on OpenStore (`tagger.belohoub`, focal); its internals are **UNKNOWN**.
- Recommendation: keep KyAuth's flow (Tagger → paste, or image via content-hub). Add in-app
  scanning only on demand, because it adds a `camera` permission.

## 6. Lomiri push

- PushClient 0.1 is registered as both `Lomiri.PushNotifications` and
  `Ubuntu.PushNotifications`. **PROVEN**
  (https://gitlab.com/ubports/development/core/lomiri-push-qml, `pushclient.h`).
  - Properties: `appId`, `token`, `notifications`, `status`, `persistent`, `count`.
  - Signals: `tokenChanged`, `notificationsChanged`, `error`, and others.
  - Slots: `getNotifications()`, `clearPersistent(tags)`.
- Helper contract: `helper <in> <out>` writes `{"message": …, "notification": {"card":
  {summary, body, persist, popup, …}, "tag", …}}`. It has a 5 s max runtime. If the helper
  fails, the payload is passed through as `message` with **no** notification. **PROVEN**
  (https://docs.ubports.com/en/latest/appdev/guides/pushnotifications.html, lomiri-push-service
  `helper_output.go`, `kindpool.go`).
- Card without the app running: the push service launches the helper and presents the card
  itself. **PROVEN** from code (`PostalService.handleHelperResult`).
- Limits: `data` ≤ 4096 bytes (`MaxUnicastPayload`), request body ≤ 6 KiB. **PROVEN**
  (`server/api/handlers.go`).
- Server API: `POST https://push.ubports.com/notify` with `{appid, token, expire_on, data,
  clear_pending, replace_tag}`. **PROVEN** (docs).
- **No sender authentication.** The server only checks that the decoded token starts with
  `appid::`. The token is effectively `base64(appid::deviceId)`, which is deterministic.
  **PROVEN** in source; that push.ubports.com runs the same store is **LIKELY**. Consequences:
  - Encrypt `data` end to end (RFC 8291, as KyAuth does).
  - Use fixed card text ("New mail").
  - Treat every push as a "sync now" hint and fetch over the authenticated relay. A forged
    push then costs one sync and leaks nothing.
  - Don't let the helper decrypt for richer cards. It would need keys outside the PIN gate.

## 7. Qt6 / 26.04 timeline

- Forum, "Changes in Ubuntu Touch 24.04-2.0 for app developers" (undated). **PROVEN** quotes:
  - "this doesn't mean we support applications written in Qt 6 in general"
  - "We intend to stabilize APIs for Qt 6 applications in Ubuntu Touch 26.04-1.0."
  - Clickable 8.9.0 recognises the 24.04-2.x frameworks, and Qt6 libraries are not in the
    images by default.
  https://forums.ubports.com/topic/12474/changes-in-ubuntu-touch-24.04-2.0-for-app-developers
- Q&A 192 (13 June 2026): 24.04-2.0 "ships the first experimental Qt 6 libraries", the first
  Qt6 app is Morph, and "a new clickable SDK with Qt6 support … will be provided". **PROVEN**.
  https://ubports.com/blog/ubports-news-1/ubuntu-touch-q-a-192-4002
- The 24.04-1.3 post says "the support for Qt 5.15 has also ended for some time". **PROVEN**.
  https://ubports.com/blog/ubports-news-1/ubuntu-touch-24-04-1-3-release-and-updates-on-ubuntu-touch-24-04-2-0-3996
- The Lomiri roadmap says "Full Qt6 Migration" around Q2 2026, undated, and doesn't mention
  26.04 (https://ubports.com/lomiri-roadmap). **PROVEN** that the page says this.
- 26.04-1.0 release date: **UNKNOWN**. Expect the Qt5 app to live into 2027.

## 8. Qt6-only APIs in `core/` (grep, not compiled)

Scope: 80 `.cpp` / 97 `.h` files, about 20.2k lines. Nothing was compiled, so this list is
**LIKELY**-complete. A Qt5 CI build is the proof.

| API (Qt version) | Where | Qt5-and-6 replacement |
|---|---|---|
| `QStringDecoder` (6.0) | `pgp/MimeBodyReader.cpp:6,224` | `#if` → `QTextCodec::codecForName` (only `#if` needed) |
| `QByteArrayView` (6.0) | `pgp/OpenPgpKeyImporter.cpp:320,370,374,375,468` | `QByteArray::fromRawData` (no copy: secret key bytes, keep SecureBytes hygiene) |
| `QByteArrayView::first/sliced` | `OpenPgpKeyImporter.cpp:374-375` | `left()` / `mid()` on raw-data `QByteArray` |
| `QLatin1StringView` (6.4) | `db/DatabaseEncryptionMigration.h:41-43`, `db/SecurityWipe.cpp:19` | `QLatin1String` (constexpr in 5.15) |
| `QStringView::sliced` (6.0) | `security/AppLockStore.cpp:203,209` | `mid()` |
| `QList::removeIf` (6.1) | `domain/MfaChallengeTracker.cpp:82` | `erase(std::remove_if(...), end())` |
| `QString::removeIf` (6.1) | `pgp/DeviceEnrollmentCrypto.cpp:173` | `remove(QRegularExpression("\\s"))` or a loop |
| `QTimeZone::UTC` (6.5) | `domain/PushRepository.cpp:48` | `Qt::UTC` |
| `qsizetype` vs `int` sizes | 50 hits / 8 files | Type exists since 5.10, but Qt5 `size()` is `int`. `std::min(limit, m_bytes.size() - m_pos)` (`OpenPgpKeyImporter.cpp:251`) will fail deduction. Others convert implicitly. |
| `Qt6::Core/Network/Sql` | `core/CMakeLists.txt:221-225` | `Qt${QT_VERSION_MAJOR}::...` as KyAuth does |

Checked and fine on 5.15 (no change): `Qt::SkipEmptyParts` (5.14),
`QByteArray::fromBase64Encoding` (5.15), `QNetworkAccessManager::setTransferTimeout` (5.15),
`UserVerifiedRedirectPolicy` (5.9), `QPasswordDigestor` (5.12), `QScopeGuard` (5.12),
`QUuid::WithoutBraces` (5.11), `QSaveFile`, `QSslConfiguration`. QSqlQuery is only stack-built
and passed by reference, with no move or copy. QHash has 3 read-only uses. No `QMultiHash`,
`QAnyStringView`, `QUtf8StringView`, `QStringTokenizer`, `QFuture`/`QPromise`/`QtConcurrent`,
`QMetaType` id changes, `QTextCodec`, `std::span`, ranges, concepts, `<=>`, `std::format`, or
designated initialisers. `import QtCore` does not apply because core has no QML. Core has no
KF6 dependency (only comments mention it).

C++ standard: the top-level `CMakeLists.txt` sets C++20, but core uses no C++20 feature that
grep found. gcc 13 (noble) handles C++20. Qt 5.15 headers under C++20 emit deprecation warnings
(implicit `this` in `[=]`) but compile. **LIKELY.** Dropping core to C++17 is the cheaper path
if they don't.

Behaviour differences to test rather than grep:

- Qt5 `QList<T>` for large T stores pointers. The API is the same, so this is a performance
  difference only.
- `QTextCodec` and `QStringDecoder` support different charset sets. Run the MIME tests on both.
- Qt5 `QString::arg` with multiple args has no variadic overload. Grep found none.

**Port size: about 10 files and about 40-60 changed lines, plus CMake, plus a Qt 5.15 CI job.**
Roughly 1 day. Only one `#if QT_VERSION` is needed (MimeBodyReader); everything else can be
written as one source valid on both Qt versions. Files: `pgp/MimeBodyReader.cpp`,
`pgp/OpenPgpKeyImporter.cpp`, `pgp/DeviceEnrollmentCrypto.cpp`,
`db/DatabaseEncryptionMigration.h`, `db/SecurityWipe.cpp`, `security/AppLockStore.cpp`,
`domain/MfaChallengeTracker.cpp`, `domain/PushRepository.cpp`, `core/CMakeLists.txt`, plus any
`qsizetype`/`int` errors the first Qt5 build reveals (most likely in `pgp/` and
`security/WebPushCrypto.cpp`).

## Recommendation: U1 / U2 scope

**U1 (ship first, smallest attack surface):**

- Make core build on Qt 5.15 and Qt 6 from one source, with a CI job for each, as KyAuth does.
- Thin click app: QML (QtQuick.Controls 2), with `networking` + `push-notification-client`
  only.
- Mail list and reading. Bodies are plain text or `Text.StyledText` after sanitising, with no
  WebEngine, so the `webview` group is not used yet.
- SQLCipher bundled through `build-sqlcipher.sh` as a clickable library, with the fail-closed
  check kept.
- File-based `SecureStore` sealed with Argon2id from a PIN or passphrase. Recommend the
  passphrase in the UI.
- Lomiri push: encrypted payload, fixed card text, and a push is treated only as a sync hint.
  Reuse KyAuth's helper and contract.
- Pairing via Tagger, then paste.
- Device spike in parallel, not in U1 scope: gpg-agent socket and executing `gpgconf` under
  click AppArmor.

**U2:**

- HTML mail through QtWebEngine: JS off, a C++ interceptor that blocks all remote requests
  including main-frame ones, `cid:` and custom scheme handlers, and a network-zero test.
- PGP (decrypt, then sign/encrypt) once the U1 spike proves gpg-agent works under confinement.
  Otherwise, a fallback design is needed.
- Optional in-app QR via QtMultimedia + QZXing (`camera` group) if Tagger-paste is too much
  friction.

## Top 3 risks

1. **gpg-agent under confinement (UNKNOWN).** Core PGP depends on gpgme spawning
   `gpg`/`gpg-agent` and on `QProcess("gpgconf")`. AppArmor may deny the exec or the socket,
   and the `/run/user` → `GNUPGHOME` fallback is only proven in source. If it fails, PGP on UT
   needs a different design.
2. **Unauthenticated push plus HTML rendering mistakes.** push.ubports.com accepts pushes from
   anyone holding appid + token. Any content in the card, or any push the app trusts, is
   spoofable. Separately, copying Dekko's interceptor pattern as-is leaves a main-frame hole.
   Both are design rules that need tests, not assumptions.
3. **Two-app maintenance with an open-ended Qt5 tail.** 26.04-1.0 has no date, so the Qt5 app
   may live years. Smaller related risk: the SQLCipher SONAME shadowing must be confirmed on
   device. It fails closed, so the app breaks rather than leaking data. The PIN-only key is
   offline-crackable without a hardware keystore.
