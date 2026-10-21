<p align="center">
  <img src="kypost.png" alt="KyPost" width="160" height="160">
</p>

<h1 align="center">KyPost</h1>

<p align="center">
  A relay-only email client for KDE Plasma and Plasma Mobile, built with Qt6/Kirigami.
</p>

---

## What is this?

KyPost is the Linux desktop and KDE Mobile client for a relay-based mail service. It does not
use IMAP or SMTP. The KyPost relay backend carries all mail, contact, and push-notification
traffic. One codebase targets two UI surfaces:

- **Linux Desktop** — KDE Plasma, packaged as a Flatpak, with a 3-column sidebar, list, and
  detail layout.
- **KDE Mobile** — Plasma Mobile, the same Flatpak, with a bottom-tab push-navigation layout.

KyPost is a sibling client to an Android app and to a SwiftUI macOS/iOS app. All three clients
use the same Go relay backend.

## Features

- **Inbox, message detail, and an HTML composer** — reply, reply-all, forward, and send, with
  attachments and drafts. A sandboxed `WebEngineView` renders each email body with JavaScript
  and remote images disabled.
- **Server-side folders** — the standard mailboxes and any subfolder. The client can create,
  rename, and delete these folders.
- **Contacts** — synced list and detail views. The client creates and edits contacts offline
  and queues the changes. It also handles group membership and duplicate entries.
- **PGP support** — the client exchanges public keys through QR codes. Scan or show a key with
  the camera. The code carries the fingerprint of the key this device holds, and a scan saves a
  key as verified only if it matches; a device holding no private key shows a code that cannot be
  verified in person. Webmail can seal the account key to a
  temporary ECDH device key; KyPost imports it into the user's GnuPG keyring and clears the
  transient key bytes. Once a key is enrolled, end-to-end encrypted mail is **decrypted and
  signed on this device** through the user's own `gpg-agent` (GPGME): KyPost never sees the
  OpenPGP passphrase, so hardware tokens and smartcards work unchanged. Sending signs and
  encrypts locally, gives each blind recipient their own ciphertext, keeps the real subject
  inside the ciphertext, and refuses to send at all if any recipient has no usable key — there
  is no silent downgrade to plaintext. Mail this device holds no key for is still explained and
  routed to webmail.
- **Compose autocomplete** — type a name or an address in Compose, then select a synced
  contact.
- **Push notifications over [UnifiedPush](https://unifiedpush.org/)** — two tiers with
  fallback: the system distributor, then 90-second polling. Mail still arrives when no
  UnifiedPush distributor is installed. Only the push server of your own distributor sees a
  notification.
- **Security** — an optional PIN lock with a lockout, a configurable background-lock grace
  period, and a configurable erase-after-N-failures threshold (including "never", which
  declines only the erase — the rate limit always stays). You can encrypt the pairing
  credential behind that PIN. The local database is **encrypted at rest** with SQLCipher; an
  existing plaintext profile is converted on the next launch. The client pins the relay's TLS
  chain on first use, anchored to the issuer rather than the leaf so a routine certificate
  renewal is not reported as an impersonation. Hostile Location Protection writes nothing to
  disk, and refuses to turn on at all if it cannot first erase what is already there.
- **17 themes plus Follow system** — copied byte for byte from the Android app. The default
  follows your desktop between Busnes Light and Busnes Dark; text follows your system font size.
- **Device pairing** — paste a link, or use a `kypost://` deep link.
- **Localized** — the code wraps every user-facing string for translation (`po/`).

## Standalone client setup

The client needs a separate [KyPost server](https://github.com/Busnes-app/KyPost-Server)
over HTTPS and an everyday mailbox account. It cannot use IMAP/SMTP directly. Install the
server first, configure its mail service, then build this client or install from its signed Flatpak remote.

Launch KyPost, generate a device pairing link in the server’s web security settings, paste
it into the client and confirm the server. Check mail access and a controlled send/reply.
UnifiedPush is optional; polling remains available. PGP key enrollment is a separate step
and requires the account’s key/recovery material. Preserve your profile, OS keyring and
GnuPG keyring on updates. See [setup](docs/SETUP.md) and [manual testing](TESTING.md).

## Installing

> **Available channel (checked 2026-10-20):** GitHub release `v0.4.2`, the
> `gh-pages` branch and the public `.flatpakrepo` endpoint exist (HTTP 200).
> Install from the signed remote below for updates. This availability check is not
> a fresh installation or signer-verification result; follow
> [distribution verification](docs/DISTRIBUTION.md) before relying on the package.

KyPost has its own signed Flatpak remote:

```sh
flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
flatpak remote-add --if-not-exists kypost https://busnes-app.github.io/KyPost-for-Linux/kypost.flatpakrepo
flatpak install kypost com.kysecurity.mail
```

The normal `flatpak update` command then installs new versions. The Flathub remote supplies the
`org.kde.Platform//6.11` runtime. KyPost uses its own distribution channel rather than a Flathub listing. Each
[release](https://github.com/Busnes-app/KyPost-for-Linux/releases) also attaches a
single-file `.flatpak` bundle, but a bundle gives you no automatic updates. For more
information, see [`docs/DISTRIBUTION.md`](docs/DISTRIBUTION.md).

## Building

The app needs Qt6 and KF6. Ubuntu Touch will get a separate Qt 5.15 app that shares `core/`, so
`core/` also builds on Qt 5.15 (see [Core only, on Qt 5.15](#core-only-on-qt-515)). Use one
out-of-tree build directory:

```sh
cmake -B build -S . -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build
```

Dependencies, with Arch package names: `qt6-base`, `qt6-declarative`, `qt6-webengine`,
`qt6-multimedia`, `kirigami` (KF6), `knotifications` (KF6), `kstatusnotifieritem` (KF6),
`kdbusaddons` (KF6), `ki18n` (KF6), `qtkeychain-qt6`, `kunifiedpush`, `zxing-cpp`, `openssl`,
`argon2`, `gpgme`. For the Ubuntu and KDE neon archive equivalents, see
[`.github/workflows/ci.yml`](.github/workflows/ci.yml).

`openssl` supplies AES-256-GCM and `argon2` the memory-hard key derivation behind the
credential seal — Qt exposes neither. See `core/security/CredentialCipher.h`. `gpgme` is the C
API, not the `gpgmepp` C++ wrapper, and it is what delegates OpenPGP custody to the user's
`gpg-agent`.

### Encryption at rest needs SQLCipher

The command above builds a working client whose database is **not** encrypted — configure
reports `SQLCipher: not configured (KYPOST_SQLCIPHER_ROOT unset) -- encrypted databases
unavailable`. SQLCipher has to carry the SONAME `libsqlite3.so.0` and be built with
`SQLITE_ENABLE_COLUMN_METADATA`, which distro packages generally do not do, so this repo builds
it — the same way CI and the Flatpak manifest do:

```sh
./scripts/build-sqlcipher.sh "$HOME/.local/opt/kypost-sqlcipher"
cmake -B build -S . -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DKYPOST_SQLCIPHER_ROOT="$HOME/.local/opt/kypost-sqlcipher"
cmake --build build
ctest --test-dir build --output-on-failure
```

The first build recipe is for evaluation without database encryption. Encryption tests may
skip there: a green `ctest` result with skips is not encryption coverage. Use the SQLCipher
build above for an encrypted profile and verify that configure reports SQLCipher enabled.
`AGENTS.md` §6h explains the SONAME/metadata requirements. Fake-relay tests and demo
screenshots do not qualify live mail, push or physical-device PGP interoperability.

### Core only, on Qt 5.15

`-DKYPOST_CORE_ONLY=ON` builds only `core/` and its tests, with no KF6 and no app. This is the
build the CI `core-qt5` job runs. It needs `qt5-base` (Ubuntu: `qtbase5-dev`, `libqt5sql5-sqlite`)
and the same SQLCipher as above. `-DQT_DIR` selects Qt 5 when Qt 6 is also installed. Configure
prints the version it used, for example `kypostcore: Qt 5.15.13`:

```sh
./scripts/build-sqlcipher.sh "$HOME/.local/opt/kypost-sqlcipher"
cmake -B build-qt5 -S . -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DKYPOST_CORE_ONLY=ON \
  -DQT_DIR=/usr/lib/cmake/Qt5 -DKYPOST_SQLCIPHER_ROOT="$HOME/.local/opt/kypost-sqlcipher"
cmake --build build-qt5
ctest --test-dir build-qt5 --output-on-failure
```

Without `KYPOST_CORE_ONLY` the build is always Qt 6, whatever `QT_DIR` says.

### Flatpak

```sh
flatpak-builder --user --force-clean --install-deps-from=flathub \
  build-flatpak packaging/flatpak/com.kysecurity.mail.yaml
flatpak-builder --run build-flatpak packaging/flatpak/com.kysecurity.mail.yaml kypost
```

This manifest is the packaging target for Linux Desktop and for Plasma Mobile. The Click and
Ubuntu Touch directory (`packaging/click/`) is a deliberate empty placeholder. It stays empty
until the Ubuntu Touch app exists.

CI builds this manifest for every pull request. Publishing a signed OSTree repository
to `gh-pages` on `main` is conditional on the configured signing/publishing secrets;
without them the build/smoke checks run and publication is skipped. See [the publishing workflow](.github/workflows/flatpak.yml). The `flatpak remote-add`
command above uses that published repository.

## Architecture

```
core/       — libkypostcore: models, SQLite DAOs, stores, relay networking, domain
              repositories, theme data. QtCore/QtNetwork/QtSql only — no QtGui/QtQuick/
              QtDBus/KUnifiedPush/KNotifications.
app/        — main.cpp, push/ (KUnifiedPush + KNotifications glue), platform/ (SecureStore
              backends), pgp/, contacts/, mail/, pairing/, qml/ (AppShell, pages,
              reusable components, bundled icons)
tests/      — QtTest, stubbed HttpClient/FakeRelayServer; ctest-driven, plus the QML suite
              (QmlTests) and tests/guards.tsv, the list of security guards proven load-bearing
packaging/  — flatpak/ (manifest, desktop file, AppStream metainfo, notifyrc, icons),
              dbus/ (session D-Bus activation service), click/ (empty placeholder, deferred)
po/         — gettext translation catalogs
scripts/    — build-sqlcipher.sh, verify-guards.sh, verify-version.sh, relay verification
docs/       — PARITY.md (the authoritative Android-parity matrix), DISTRIBUTION.md,
              THREADING.md, SETUP.md, RENAME_NOTES.md
```

The `core/` boundary permits Qt Core, Network, and Sql only. A new dependency in `core/` breaks
this rule most easily. See `AGENTS.md` Section 5.

[`AGENTS.md`](AGENTS.md) and [`docs/PARITY.md`](docs/PARITY.md) describe what exists today —
the rules a change breaks most easily, and the acceptance matrix against the Android client.
[`Linux_QT_Client_Plan.md`](Linux_QT_Client_Plan.md) is the original architectural design
record; it predates several locked decisions, so where it disagrees with those two, it is the
stale one. [`TESTING.md`](TESTING.md) is the manual verification checklist.

## License

MIT, developed by Busnes.app — see [`LICENSE.txt`](LICENSE.txt).
