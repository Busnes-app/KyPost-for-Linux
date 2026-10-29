<p align="center">
  <img src="kypost.png" alt="KyPost" width="160" height="160">
</p>

<h1 align="center">KyPost</h1>

<p align="center">
  <a href="https://www.bestpractices.dev/projects/15929"><img src="https://www.bestpractices.dev/projects/15929/badge" alt="OpenSSF Best Practices"></a>
  <a href="https://scorecard.dev/viewer/?uri=github.com/Busnes-app/KyPost-for-Linux"><img src="https://api.scorecard.dev/projects/github.com/Busnes-app/KyPost-for-Linux/badge" alt="OpenSSF Scorecard"></a>
</p>

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

Read the [release notes](CHANGELOG.md) before upgrading. They cover named versions
and the rolling channel's source builds, with profile/keyring compatibility and
relay requirements. Future channel publications also provide
[current notes](https://busnes-app.github.io/KyPost-for-Linux/release-notes.md)
and per-source notes; verify the package signature as described below.

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

## License

MIT, developed by Busnes.app — see [`LICENSE.txt`](LICENSE.txt).

[![OctoCounts](https://api.octocounts.com/badge/Busnes-app/KyPost-for-Linux/branch/main)](https://octocounts.com/github/Busnes-app/KyPost-for-Linux/tree/main)

## Documentation

- [Features](docs/FEATURES.md) — mail, folders, contacts, PGP, push, security, themes, pairing and localization. <a id="features"></a>
- [Building](docs/BUILDING.md) — native build and dependencies, SQLCipher encryption at rest, the Qt 5.15 core-only build and the Flatpak. <a id="building"></a><a id="encryption-at-rest-needs-sqlcipher"></a><a id="core-only-on-qt-515"></a><a id="flatpak"></a>
- [Architecture](docs/ARCHITECTURE.md) — the source layout, the `core/` boundary and the design records. <a id="architecture"></a>
- [Distribution](docs/DISTRIBUTION.md) — the self-hosted signed Flatpak channel.
- [Strong cryptography](docs/STRONG-CRYPTO.md) — the keys KyPost selects or accepts, and their minimums.
- [Encrypted send](Client_Encrypted_Send.md) — how the client sends encrypted and signed mail through the relay.
- [Android parity](docs/PARITY.md) — which Android features the Linux client has.
- [Manual testing](TESTING.md) — the human verification checklist.
- [Contributing](CONTRIBUTING.md) — how to propose a change and what it must pass.
- [Security policy](SECURITY.md) — how to report a vulnerability privately.
- [Release notes](CHANGELOG.md) — what changed in each release, and its upgrade impact.
