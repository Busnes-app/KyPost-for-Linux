**Repo:** kypost-Linux (+ kypost-server for S1–S3)
**Worktree:** /home/yoshi/git/busnes.app/kypost-Linux (branch main)

# Android parity, postmarketOS, and Ubuntu Touch plan

Date: 2026-10-11. Direction agreed with Yoshi; implementation not started.
Linux baseline `8f62026`. Android reference `kypost-android@94ec8276`.

## Decisions (Yoshi, 2026-10-11)

- Keep and extend this codebase. No rewrite.
- Ubuntu Touch support is back. This reverses `AGENTS.md:140` ("dropped, not merely
  paused"); update AGENTS.md in the first UT PR.
- UT is a **separate thin Qt 5.15 app** (option B) reusing `core/`. It is a stopgap:
  drop it once Ubuntu Touch ships a 26.04 (Qt 6) base and fold UT into the Qt 6 app.
- UT push uses Ubuntu Push (push.ubports.com) as in KyAuth-Linux. kypost-server gains
  that transport.
- Search and a drafts list are in scope, beyond Android.
- Snooze and scheduled send are deferred until the server supports them.

## Server work (kypost-server)

- **S1. MFA over UnifiedPush.** Linux only has UnifiedPush; MFA challenges must reach
  it. Blocks L1.
- **S2. Ubuntu Push transport.** `pushTransport: "ubuntupush"`, POST to
  push.ubports.com with a fixed card, RFC 8291 ciphertext in `data.message`,
  ≤ 4096 B. Model on KyIdentity `feat/linux-authenticator` and
  KyAuth-Linux `docs/push-contract.md`. Blocks U4.
- **S3 (deferred).** Snooze, scheduled send.

Already on the server, no work needed: `GET /api/mail/search` (`q`, `field`,
`mailbox`, `limit`), RFC 8291 for UnifiedPush (`webpush_keys.go`),
`POST /api/mfa/push/respond`.

## Linux (Qt 6) parity — independent PRs

- **L1. MFA approval.** Approval page (time, IP, location, device), number match,
  PIN re-auth, `POST /api/mfa/push/respond`. Needs S1.
- **L2. Encrypted push payloads.** Send `p256dh`/`auth` at registration, decrypt
  aes128gcm in `core/`. Port `webPushDecrypt` and tests from KyAuth-Linux
  `src/core`. Removes the cleartext sender/subject leak in Settings copy.
- **L3. Read/unread.** Mark read on open, unread styling, mark-unread action.
  Correct `docs/PARITY.md`, which wrongly claims "read".
- **L4. Move to folder.** UI for the existing `moveEmails()`.
- **L5. Folder management on mobile.** Create/rename/delete exists in `DesktopRoot.qml`
  only; add it to `MobileRoot.qml` (and the UT app, U3).
- **L6. Search.** `GET /api/mail/search`; results list reuses the email delegate.
- **L7. Drafts list.** Drafts folder view; opening a draft resumes Compose
  (encrypted drafts already reopen via `reopenDecryptedDraft`).
- **L8. Small items.** Mobile pull-to-refresh; notification history and fuller
  pairing status; 3-hour push-silence pull backstop.

## postmarketOS

- **P1.** Verify the aarch64 Flatpak on a Plasma Mobile pmOS device (push via a
  UnifiedPush distributor, camera QR, mobile root). Unproven until run on hardware.
- **P2 (optional).** APKBUILD if native packaging is wanted.

## Ubuntu Touch (separate app, Qt 5.15, UT 24.04-1.x)

- **U0. Spike, before committing.** Inside a confined click, prove or rule out:
  QtWebEngine 5.15 in the framework; bundled SQLCipher; bundled gpg + gpg-agent with
  a socket in the app dir; a database-key store without Secret Service (likely a
  PIN-derived key, as KyAuth's vault). Results decide U2's scope.
- **U1. `core/` dual-builds on Qt 5.15 and Qt 6.** Replace Qt6-only APIs
  (`QStringDecoder`, etc.); CI matrix builds `core/` and its tests on both. Qt 6
  desktop app is unchanged.
- **U2. `ut/` app.** Plain QtQuick Controls 2.15 (no Kirigami/KF), own CMake target,
  `clickable.yaml`, click manifest + AppArmor, mobile UI only.
- **U3.** Feature set = Linux mobile root, including L1–L7 as they land.
- **U4.** Ubuntu Push via `Lomiri.PushNotifications` `PushClient` + pass-through
  push-helper, as KyAuth-Linux. Needs S2.
- **U5.** CI builds the click without `--accept-review-errors`, or records why.

## Order

L2, L3, L4, L5 (no server dependency) → L6, L7 → L8 → P1. S1 → L1. U0 first in the
UT track; U1 can run alongside the L items.

## Cleanup found in review (optional, small)

Delete tracked `mbr_now.cpp` (stale divergent copy), unused `public/` PWA files,
unused native-contact link table code; fix stale comment in `app/CMakeLists.txt`
about `PgpQrScanner`; run `scripts/verify-guards.sh` in CI.
