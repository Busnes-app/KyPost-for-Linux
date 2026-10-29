# Repository architecture

[Product overview](../README.md)

Commands and unlinked file paths below are relative to the repository root.

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
              THREADING.md, SETUP.md, RENAME_NOTES.md, STRONG-CRYPTO.md
```

The `core/` boundary permits Qt Core, Network, and Sql only. A new dependency in `core/` breaks
this rule most easily. See `AGENTS.md` Section 5.

[`AGENTS.md`](../AGENTS.md) and [`docs/PARITY.md`](PARITY.md) describe what exists today —
the rules a change breaks most easily, and the acceptance matrix against the Android client.
[`Linux_QT_Client_Plan.md`](../Linux_QT_Client_Plan.md) is the original architectural design
record; it predates several locked decisions, so where it disagrees with those two, it is the
stale one. [`TESTING.md`](../TESTING.md) is the manual verification checklist.
[`docs/STRONG-CRYPTO.md`](STRONG-CRYPTO.md) lists every key KyPost selects or accepts,
the enforced minimums (RSA-2048, EC-224, SHA-224) and what is out of scope.
