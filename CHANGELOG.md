# Release notes

[Overview](README.md) · [Distribution and verification](docs/DISTRIBUTION.md)

## Reading these notes

Tagged Flatpak downloads and the rolling signed update channel are separate
publication paths. Current GitHub Releases are v0.4.2 and v0.4.3, both with
x86_64/aarch64 assets. Older repository tags and the 0.4.0 AppStream milestone
are described below without inventing missing GitHub assets.

The channel also published builds between those versions. The source-build
archive records all **51 distinct source revisions** for which public Flatpak
workflow steps showed a successful channel push or release attachment in the
377 runs inspected on 2026-10-26. Repeated builds of the same source are grouped;
each archive entry links one checked successful run. Dates there are that run's
UTC date, possibly a later rebuild, not necessarily first publication or the
tag's date. Summaries compare successive published source revisions; the initial
one includes preceding source-only development. No old asset/signature
availability or fresh installation result is inferred from a past successful job.

Historical summaries were reconstructed from pinned source intervals. For an
upgrade, read the named-version summaries and any intervening source builds.
Keep the profile, OS secret store and GnuPG keyring. [Distribution guidance](docs/DISTRIBUTION.md)
explains the signed remote and the standalone-bundle update-origin limitation.

## Current source

### Changes

- Device enrollment accepts only RFC 9180 HPKE envelopes: base mode,
  DHKEM(P-256, HKDF-SHA256) 0x0010, HKDF-SHA256 0x0001, AES-256-GCM 0x0002;
  `info` binds the version, device ID and OpenPGP fingerprint. KyPost advertises `envelopeVersions: [4,5]` and refuses a delivered v2/v3
  envelope with an update-the-server message. The legacy ECDH/HKDF/GCM opener
  is deleted; Linux never stored or sealed v2/v3 data.

### Upgrade impact

- Enrollment needs a KyPost server that seals v4/v5 when advertised; current
  KyPost-Server main does. An older server sending v2/v3 now fails enrollment
  without importing anything; update it, then enroll again. Already-imported
  GnuPG keys, profiles and historical mail are unchanged.

## Source notes at e6cb4e5d

These are the prior source summary, preserved from
[`e6cb4e5d`](https://github.com/Busnes-app/KyPost-for-Linux/commit/e6cb4e5d).
Their presence does not establish that this source was distributed.

### Changes

- Refuse new OpenPGP signing/encryption with RSA primary keys or usable
  operation-specific RSA subkeys below 2048 actual bits, including 2047-bit keys.
- Exercise the real GnuPG path with weak primary/signing/encryption subkeys,
  strong controls, all-recipient refusal and retained historical mail/secret keys.
- Record the PIN-focus mutation's overlapping protection honestly: retain both
  application focus handlers without claiming each is independently proved.

### Upgrade impact

- A weak sender/recipient key can now block a new protected message or draft.
  Arrange replacement through existing account/key management and verify the
  fingerprint; never bypass refusal with plaintext or delete the old keyring.
- Existing keys, profile formats and historical ciphertext are unchanged;
  old mail remains decryptable. Incoming signature verification, non-RSA
  algorithms and TLS are outside this guard's scope.

## Source notes at 01c1fdd1

These are the prior source summary, preserved from
[`01c1fdd1`](https://github.com/Busnes-app/KyPost-for-Linux/commit/01c1fdd1).
Their presence does not establish that this source was distributed.

### Changes

- Add human summaries and upgrade guidance for named releases and all observed
  published source builds, including the rolling channel between tags.
- Require fresh source notes before publication. Future tag releases attach
  their exact named notes; the channel serves current and per-source notes.

### Upgrade impact

- This notes/workflow change introduces no application, profile or key-format
  migration. Read the full history's post-v0.4.3 source entries when upgrading the channel:
  they include calendar RSVP and HPKE enrollment changes absent from that tag.
- Release verification needs Python 3 and a Git checkout with its comparison
  parent. The desktop application gains no runtime Python dependency.

## Maintaining notes

Every proposed main update needs a new Current source change summary and upgrade
impact, including documentation, dependency and workflow-only publications.
Archive the previous shipped source summary under its known commit; keep history.
For a new version, also add an exact `## v<version>` section with Changes and
Upgrade impact, and keep CMake/AppStream/tag versions in agreement. Include
major changes, compatibility/migration steps and known advisory identifiers;
state when no migration is needed.

Run `python3 -B -m unittest discover -s scripts -p test_release_notes.py` and
`python3 scripts/release-notes.py --base origin/main` after fetching the target.
CI uses `HEAD^` with a two-commit checkout: the tested merge's first parent on
a PR, and the preceding commit on a main push. Missing/invalid bases fail;
a base without this file permits initial adoption. The checker refuses empty,
comment/link-only or unchanged Current source notes. Review accuracy and
completeness separately; a text check cannot establish either.

On tags, the publication step selects the exact version section and binds
output to the tag's actual source commit before importing signing material.
On rolling builds it selects Current source. The signed-channel site retains
`releases/<source-sha>.md` and serves `release-notes.md` for its current build;
tagged GitHub Releases use the generated file as their body. These text notes
are editorial metadata, not a replacement for package signature verification.
Existing GitHub release bodies, tags and assets are not rewritten by adding
this archive.

## v0.4.3

GitHub Release published 2026-10-24; tag source `55fbf50a`.
AppStream records version date 2026-10-23. This section summarizes changes
since v0.4.2; later channel changes appear separately below.

### Changes

- Add link-host confirmation, threaded replies and Reply All exclusions for the
  account's reported sending addresses. Resolve standard folders by special-use
  role, including localized/custom Drafts and Sent folders.
- Add snooze, scheduled-send/list/cancel, unread counts and older-inbox paging.
  Scheduling probes relay support before sending; encrypted scheduled messages
  need a self-encrypted draft copy.
- Replace separate layout roots with one width-adaptive Android-aligned shell,
  readable theme/type tokens, system-following Busnes palettes, keyboard access
  and bundled icons.
- Add inbox previews and safe real-body quoting/attachment forwarding without
  replacing a live draft or silently quoting locally decrypted content into
  cleartext mail.
- Add validated vCard contact import, blank-subject confirmation and an optional
  one-time lock offer. Tighten CI token permissions and pin Actions.
- Port the shared core to Qt 5.15 while keeping the desktop/mobile app Qt6/KF6.

### Upgrade impact

- Keep the encrypted profile for automatic cache migrations 010/011; do not
  delete it or replace its database key. Newer rows can fetch older message
  bodies on demand, and unread counts describe the held local cache.
- Use a relay supporting the requested snooze/scheduling/paging APIs. An
  unsupported scheduling endpoint refuses the request rather than sending now.
  Review Reply All recipients when the relay lacks complete account-address data.
- Layout follows window width; existing named themes remain available. Shared
  Qt 5 core support is not a shipped Ubuntu Touch application.
- Calendar RSVP and HPKE v4/v5 enrollment are **later channel changes**, not
  features attributed to this tag. Preserve GnuPG's historical keys.

## v0.4.2

GitHub Release published 2026-10-16; tag source `1a1e849f`.

### Changes

- Improve native OpenPGP reading, exact-byte signature verification, protected
  subjects/attachments and encrypted draft save/reopen with GnuPG custody.
- Add a paired-relay version notice, explicit read/unread actions, folder moves
  and mobile folder management, server search and resumable drafts.
- Encrypt UnifiedPush with RFC 8291 and add matching-number MFA approval,
  pairing-ownership checks and a long-push-silence refresh backstop.
- Improve QR fingerprint checks, snooze-wake ordering, product icons, connection
  status and mobile/pull-to-refresh behavior.
- Carry earlier profile/PIN/wipe fixes described in the 0.4.0 and source-build
  entries in the full history, including Argon2id verifier upgrades and bounded HTML/MIME input.

### Upgrade impact

- Preserve the encrypted cache for migrations 007–009 and keep the OS keyring
  available. A correct legacy PIN unlock upgrades its verifier; older builds
  cannot read the new record, so avoid downgrading afterward.
- Local encrypted mail/drafts require the account's GnuPG keys. Historical keys
  remain for decryption; failed key operations do not authorize plaintext sends.
- Relay-side search, encrypted push and MFA need compatible server endpoints
  and registered device keys. Polling remains available without a distributor.
- Follow the current Busnes-app distribution coordinates and preserve signing
  trust. Standalone bundles from this workflow do not configure an updating
  remote; prefer the documented signed-channel install.

## v0.4.0

AppStream/source milestone dated 2026-08-30, source `ebcd85d7`.
No matching current Git tag or GitHub Release is asserted.

### Changes

- Correct startup theme/taskbar icons and reorganize settings with immediate
  interface-mode changes and persistent keyword ordering.
- Use the verified transport result when pairing rather than persisted endpoint
  announcements.
- Include preceding encryption/wipe, PIN-verifier, pin enforcement, composer
  sanitization and MIME-bound fixes in the full source-build history.

### Upgrade impact

- Preserve the profile, secret store and GnuPG keyring. Unlock a legacy PIN once
  to permit its Argon2id verifier upgrade; avoid downgrading that upgraded record.
- Review a certificate-change banner against the actual relay rather than
  bypassing it. This source milestone does not require a manual cache reset.

## v0.2.0

Repository tag source `0e070948`; AppStream date 2026-08-24 and first observed
successful signed-channel publication. Current GitHub assets for this version
are not asserted.

### Changes

- Distribute the relay-only Qt mail/contacts client through the signed Flatpak
  channel for x86_64 and aarch64.
- Add SQLCipher profile encryption and migration of older plaintext/application-ID
  data, per-device pairing and asynchronous relay operations.
- Support local OpenPGP decrypt/verify/sign/encrypt through GPGME/gpg-agent, with
  transient enrollment material and recipient-key checks.
- Provide HTML composition, attachments/drafts, server folders, system tray and
  pop-out windows with lock/security boundaries.

### Upgrade impact

- Keep the old profile and keyring until automatic migration has succeeded.
  Losing the database encryption key makes the encrypted local cache unreadable;
  an unreadable keyring is not permission to generate a replacement.
- Local PGP needs a functioning GnuPG agent and the account/recipient keys;
  private-key import is a separate authenticated enrollment flow.
- Use the signed remote for updates; do not infer remote configuration or signer
  verification merely from downloading a standalone bundle.

## v0.1-alpha

Historical repository source tag `968aa644`. AppStream separately records an
initial 0.1.0 milestone; no successful signed-channel artifact is inferred here.

### Changes

- Provide the initial relay-only inbox, reader/composer, contacts and pairing
  client with Qt desktop/mobile surfaces.

### Upgrade impact

- This superseded alpha predates the later encryption, custody and migration
  work. Preserve its data before moving to a maintained build and follow the
  subsequent profile/application-ID migration notes; do not downgrade a current
  encrypted profile to this alpha.

## Published source builds

### 8c737580 — checked publication 2026-10-26

Source: [8c7375800d27e2d5df88f253db9ea74106389312](https://github.com/Busnes-app/KyPost-for-Linux/commit/8c7375800d27e2d5df88f253db9ea74106389312) · [Publication evidence](https://github.com/Busnes-app/KyPost-for-Linux/actions/runs/40263129294)

- **Changes:** Link the README to this repository's live OpenSSF Best Practices and Scorecard results.
- **Upgrade impact:** This is a README-only change. The badges report their current program results; no application, profile or key migration is introduced.

### 8fec1b44 — checked publication 2026-10-25

Source: [8fec1b448420c344fb03ea62cf96442b24b13e3e](https://github.com/Busnes-app/KyPost-for-Linux/commit/8fec1b448420c344fb03ea62cf96442b24b13e3e) · [Publication evidence](https://github.com/Busnes-app/KyPost-for-Linux/actions/runs/40182170791)

- **Changes:** Open RFC 9180 HPKE enrollment envelopes v4/v5 through OpenSSL, advertise the supported delivery versions and acknowledge complete-ring deliveries against server metadata. Update native CI for the required OpenSSL API.
- **Upgrade impact:** Source builds require OpenSSL 3.2 or newer. Use a compatible relay for converted accounts and retain GnuPG historical keys. Legacy v2/v3 delivery support remains advertised; this change does not claim downgrade prevention or removal of the older protocol.

### 2b61ad6a — checked publication 2026-10-25

Source: [2b61ad6aa4b44efede3c2b9824150e516d4360e6](https://github.com/Busnes-app/KyPost-for-Linux/commit/2b61ad6aa4b44efede3c2b9824150e516d4360e6) · [Publication evidence](https://github.com/Busnes-app/KyPost-for-Linux/actions/runs/40153717042)

- **Changes:** Derive RFC 8291 push keys through OpenSSL's HKDF implementation instead of the local derivation code.
- **Upgrade impact:** The RFC 8291 wire/key derivation remains compatible; the change does not require re-pairing or replacement of stored device push keys.

### ef21df94 — checked publication 2026-10-24

Source: [ef21df94f74ce10c4f40f96737445219620351ca](https://github.com/Busnes-app/KyPost-for-Linux/commit/ef21df94f74ce10c4f40f96737445219620351ca) · [Publication evidence](https://github.com/Busnes-app/KyPost-for-Linux/actions/runs/40121331662)

- **Changes:** Refresh the pinned Actions used for native, Flatpak and Scorecard workflows.
- **Upgrade impact:** This workflow dependency refresh introduces no application or profile migration.

### 213ac6c6 — checked publication 2026-10-24

Source: [213ac6c6a77eb807897034ac81ebac417e652ada](https://github.com/Busnes-app/KyPost-for-Linux/commit/213ac6c6a77eb807897034ac81ebac417e652ada) · [Publication evidence](https://github.com/Busnes-app/KyPost-for-Linux/actions/runs/40082325994)

- **Changes:** Display calendar invite cards and add Accept/Maybe/Decline replies tied to the loaded card/account and confirmed before sending.
- **Upgrade impact:** RSVP replies are plain, unsigned, unencrypted email to the event ORGANIZER. Review the recipient/details before confirming. A compatible relay calendar-reply endpoint is required; this is not KyCalendar provisioning.

### 55fbf50a — checked publication 2026-10-24

Source: [55fbf50a817fbb940f831c3999c28d406fd622da](https://github.com/Busnes-app/KyPost-for-Linux/commit/55fbf50a817fbb940f831c3999c28d406fd622da) · [Publication evidence](https://github.com/Busnes-app/KyPost-for-Linux/actions/runs/40047625330)

- **Changes:** Add validated vCard contact import, confirmation before sending with a blank subject, and a one-time app-lock offer after pairing. Confirmation overlays refuse actions while locked.
- **Upgrade impact:** Review imported contacts before synchronizing; imported files cannot supply trusted key/identity/photo/group state. The lock offer is optional. Blank-subject messages remain sendable after confirmation. This is the source selected by v0.4.3.

### 8123b4eb — checked publication 2026-10-24

Source: [8123b4eb9c8c3d31f4211131990bbe810bcc9ae1](https://github.com/Busnes-app/KyPost-for-Linux/commit/8123b4eb9c8c3d31f4211131990bbe810bcc9ae1) · [Publication evidence](https://github.com/Busnes-app/KyPost-for-Linux/actions/runs/40042852786)

- **Changes:** Exclude the account's reported sending addresses from Reply All rather than only its primary address.
- **Upgrade impact:** Review recipients when using a relay that does not report all account addresses. This change needs no local profile/key migration.

### 524d836f — checked publication 2026-10-23

Source: [524d836f19b3d7f874105648500ff395c4fb1cb2](https://github.com/Busnes-app/KyPost-for-Linux/commit/524d836f19b3d7f874105648500ff395c4fb1cb2) · [Publication evidence](https://github.com/Busnes-app/KyPost-for-Linux/actions/runs/40036752725)

- **Changes:** Prepare version 0.4.3 in source and AppStream metadata.
- **Upgrade impact:** The final v0.4.3 tag also includes the subsequent account-address and small-item changes in this archive; use its named-version summary. This metadata interval adds no runtime migration.

### 085b5ec3 — checked publication 2026-10-23

Source: [085b5ec34b08ec5b6d5073879e00144e8b5a2b2c](https://github.com/Busnes-app/KyPost-for-Linux/commit/085b5ec34b08ec5b6d5073879e00144e8b5a2b2c) · [Publication evidence](https://github.com/Busnes-app/KyPost-for-Linux/actions/runs/40029871917)

- **Changes:** Use a neutral example relay host in the client plans.
- **Upgrade impact:** This is documentation only; it does not change the relay configured on any installed client.

### 19258283 — checked publication 2026-10-23

Source: [19258283701c853f4539d0ef2c3d668c31d21162](https://github.com/Busnes-app/KyPost-for-Linux/commit/19258283701c853f4539d0ef2c3d668c31d21162) · [Publication evidence](https://github.com/Busnes-app/KyPost-for-Linux/actions/runs/39975391025)

- **Changes:** Pin CI Actions by commit and make repository tokens read-only by default, granting write only to publication.
- **Upgrade impact:** This supply-chain workflow change adds no application, profile or account migration.

### 8c31c855 — checked publication 2026-10-23

Source: [8c31c85531f062187c80cfc2953f0450ca3cacf7](https://github.com/Busnes-app/KyPost-for-Linux/commit/8c31c85531f062187c80cfc2953f0450ca3cacf7) · [Publication evidence](https://github.com/Busnes-app/KyPost-for-Linux/actions/runs/39936333201)

- **Changes:** Show relay-provided inbox previews and quote the real body for Reply/Forward. Carry forwarding attachments without overwriting a live draft; locally decrypted protected content is not silently quoted into a cleartext send.
- **Upgrade impact:** Some protected replies intentionally contain no quote/forwarded attachments; the UI explains why. Keep a draft rather than expecting Forward to replace it. No manual key migration is needed.

### fbe0019c — checked publication 2026-10-21

Source: [fbe0019c7d1a0f5a5311aa5b51b4e0e518a67663](https://github.com/Busnes-app/KyPost-for-Linux/commit/fbe0019c7d1a0f5a5311aa5b51b4e0e518a67663) · [Publication evidence](https://github.com/Busnes-app/KyPost-for-Linux/actions/runs/39620914466)

- **Changes:** Introduce one width-adaptive shell with Android-aligned navigation/cards, Busnes light/dark system-following themes, shared text/color tokens, keyboard access and bundled icons. Fix HiDPI icon feedback, popup lock closers and notification navigation.
- **Upgrade impact:** Layout now adapts to window width rather than choosing separate roots at launch. Existing named theme choices remain available; review display preferences after updating. No profile/key migration is introduced.

### 168c9373 — checked publication 2026-10-21

Source: [168c93739b657605ce7f6fc750d2b7a39d7d6a5f](https://github.com/Busnes-app/KyPost-for-Linux/commit/168c93739b657605ce7f6fc750d2b7a39d7d6a5f) · [Publication evidence](https://github.com/Busnes-app/KyPost-for-Linux/actions/runs/39582291807)

- **Changes:** Build/test the shared core on Qt 5.15 as well as Qt 6, and fix the ambiguous-notification chooser on Plasma Mobile.
- **Upgrade impact:** Desktop and Plasma Mobile still use the Qt6/KF6 app. The shared-core work is not a shipped Ubuntu Touch app; source builders should select the intended Qt version and configure SQLCipher.

### cd662203 — checked publication 2026-10-20

Source: [cd662203257b9cc16c8ba26aa5911b2862f7c374](https://github.com/Busnes-app/KyPost-for-Linux/commit/cd662203257b9cc16c8ba26aa5911b2862f7c374) · [Publication evidence](https://github.com/Busnes-app/KyPost-for-Linux/actions/runs/39480380439)

- **Changes:** Replace a personal relay hostname in examples/comments with neutral documentation.
- **Upgrade impact:** The relay remains the one selected by the user's pairing. No actual deployment endpoint, account or stored setting is changed by this documentation interval.

### a9dcc86c — checked publication 2026-10-20

Source: [a9dcc86c31b4e1e423f342efc694c0f16a0f8ce0](https://github.com/Busnes-app/KyPost-for-Linux/commit/a9dcc86c31b4e1e423f342efc694c0f16a0f8ce0) · [Publication evidence](https://github.com/Busnes-app/KyPost-for-Linux/actions/runs/39466564345)

- **Changes:** Correct organization links in historical handoffs and plans.
- **Upgrade impact:** This documentation correction requires no application or profile migration.

### faf2a51e — checked publication 2026-10-20

Source: [faf2a51e26a0839dc8876f31e27ca0c43f634863](https://github.com/Busnes-app/KyPost-for-Linux/commit/faf2a51e26a0839dc8876f31e27ca0c43f634863) · [Publication evidence](https://github.com/Busnes-app/KyPost-for-Linux/actions/runs/39405306432)

- **Changes:** Clarify standalone setup, actual capabilities, distribution policy and the difference between availability checks and verified installs.
- **Upgrade impact:** Follow the current setup/verification instructions. This interval does not install a server, alter the profile or change the chosen self-hosted Flatpak channel.

### 7fa922ec — checked publication 2026-10-19

Source: [7fa922eca432421718fe2267f80d9fd461b68ee6](https://github.com/Busnes-app/KyPost-for-Linux/commit/7fa922eca432421718fe2267f80d9fd461b68ee6) · [Publication evidence](https://github.com/Busnes-app/KyPost-for-Linux/actions/runs/39239779359)

- **Changes:** Load older inbox pages in 50-message windows, preserve reachable older rows across resync and fetch metadata-only bodies on demand. Cache migration 011 records metadata-only rows.
- **Upgrade impact:** Keep the encrypted profile for migration and allow on-demand relay fetches for older bodies. No mailbox or key replacement is needed; paging depends on the relay's compatible cursor/UID behavior.

### f2702fac — checked publication 2026-10-17

Source: [f2702facfdb62158129e7d7ed10deca47e33a888](https://github.com/Busnes-app/KyPost-for-Linux/commit/f2702facfdb62158129e7d7ed10deca47e33a888) · [Publication evidence](https://github.com/Busnes-app/KyPost-for-Linux/actions/runs/39075030909)

- **Changes:** Display unread counts on folders and keyword tabs, recalculated from cached mail after state changes.
- **Upgrade impact:** Counts describe the held cache, not a separately queried server-wide total. No manual profile or key migration is required.

### aa121f93 — checked publication 2026-10-17

Source: [aa121f93ffd42373a1b9f0bfaddc3449850308a8](https://github.com/Busnes-app/KyPost-for-Linux/commit/aa121f93ffd42373a1b9f0bfaddc3449850308a8) · [Publication evidence](https://github.com/Busnes-app/KyPost-for-Linux/actions/runs/39068251195)

- **Changes:** Add scheduled sends, a scheduled-message list/cancel flow and lock protection. Probe relay support before sending, and include an encrypted draft copy for client-encrypted scheduled mail.
- **Upgrade impact:** An unsupported relay refuses scheduling instead of accidentally sending immediately. Keep the account key available for encrypted sends; no plaintext fallback is introduced.

### 72bb583f — checked publication 2026-10-17

Source: [72bb583f1e564ecee820a4c1b9eaf84b5342d1b6](https://github.com/Busnes-app/KyPost-for-Linux/commit/72bb583f1e564ecee820a4c1b9eaf84b5342d1b6) · [Publication evidence](https://github.com/Busnes-app/KyPost-for-Linux/actions/runs/39058523860)

- **Changes:** Add message snoozing with bounded local-time presets/custom times and an unsnooze move from Snoozed to Inbox.
- **Upgrade impact:** Use a relay supporting snooze actions. Check the selected time/zone before sending; no local key replacement or manual cache migration is needed.

### 86d3641e — checked publication 2026-10-17

Source: [86d3641e7436755447601ce40073c416d83b6fef](https://github.com/Busnes-app/KyPost-for-Linux/commit/86d3641e7436755447601ce40073c416d83b6fef) · [Publication evidence](https://github.com/Busnes-app/KyPost-for-Linux/actions/runs/39053687636)

- **Changes:** Resolve standard folders by the relay's special-use role, with conventional names as fallback; cache migration 010 stores the role.
- **Upgrade impact:** Keep the existing profile for automatic migration. Localized/custom Drafts or Sent folders may now resolve correctly without renaming any server mailbox.

### 75b23012 — checked publication 2026-10-17

Source: [75b230127d73d04c917518f9cc1570b7a0b38554](https://github.com/Busnes-app/KyPost-for-Linux/commit/75b230127d73d04c917518f9cc1570b7a0b38554) · [Publication evidence](https://github.com/Busnes-app/KyPost-for-Linux/actions/runs/39049243066)

- **Changes:** Thread replies with the original message identifiers and exclude the primary self address from Reply All.
- **Upgrade impact:** The relay must accept the threading fields. Review recipients as usual; exclusion of all reported sending aliases is added in the later account-address update in this archive.

### 16f4ce27 — checked publication 2026-10-17

Source: [16f4ce272565c20c3813735c3c3c5d8703c1caae](https://github.com/Busnes-app/KyPost-for-Linux/commit/16f4ce272565c20c3813735c3c3c5d8703c1caae) · [Publication evidence](https://github.com/Busnes-app/KyPost-for-Linux/actions/runs/39045917458)

- **Changes:** Show a link's real host and require confirmation before opening it externally.
- **Upgrade impact:** Review the destination host before accepting. No database, pairing or key migration is introduced by this reader change.

### 1a1e849f — checked publication 2026-10-17

Source: [1a1e849ffce413daf3d106a707550bec4662c512](https://github.com/Busnes-app/KyPost-for-Linux/commit/1a1e849ffce413daf3d106a707550bec4662c512) · [Publication evidence](https://github.com/Busnes-app/KyPost-for-Linux/actions/runs/39020774342)

- **Changes:** Identify the accumulated client changes as version 0.4.2 in CMake/AppStream and issue intake metadata.
- **Upgrade impact:** Use the named-version summary above for the changes since earlier milestones. This version-number interval itself adds no new database migration or key format.

### b50c19de — checked publication 2026-10-16

Source: [b50c19dec397728138aafff09d332c9d4388c921](https://github.com/Busnes-app/KyPost-for-Linux/commit/b50c19dec397728138aafff09d332c9d4388c921) · [Publication evidence](https://github.com/Busnes-app/KyPost-for-Linux/actions/runs/38883038194)

- **Changes:** Refresh the Android-parity inventory to distinguish shipped features from remaining work.
- **Upgrade impact:** This is documentation only; it introduces no application, profile or deployment migration.

### 316e59ad — checked publication 2026-10-14

Source: [316e59ad06580da4c9af0a8ce7f7df53b668116d](https://github.com/Busnes-app/KyPost-for-Linux/commit/316e59ad06580da4c9af0a8ce7f7df53b668116d) · [Publication evidence](https://github.com/Busnes-app/KyPost-for-Linux/actions/runs/38562243250)

- **Changes:** Bind an in-person PGP QR check to the full primary fingerprint actually held by the showing device, and compute the scanned key's fingerprint locally.
- **Upgrade impact:** A device without the matching secret key presents an unverified code. Compare the fingerprint in person; a relay's claimed fingerprint alone does not establish trust. Existing GnuPG keys are retained.

### 5fae4ec7 — checked publication 2026-10-14

Source: [5fae4ec71ab8fc25eb6eaccfeba88f87175f488b](https://github.com/Busnes-app/KyPost-for-Linux/commit/5fae4ec71ab8fc25eb6eaccfeba88f87175f488b) · [Publication evidence](https://github.com/Busnes-app/KyPost-for-Linux/actions/runs/38547942434)

- **Changes:** Keep mail woken from snooze at the top using the relay's wake timestamp, persisted by cache migration 009.
- **Upgrade impact:** Keep the encrypted cache for its automatic migration. Wake ordering depends on relay metadata; no manual key or mailbox migration is needed.

### 62f957ef — checked publication 2026-10-13

Source: [62f957ef95e52decf69d0a24c18aaa7dc7b9689c](https://github.com/Busnes-app/KyPost-for-Linux/commit/62f957ef95e52decf69d0a24c18aaa7dc7b9689c) · [Publication evidence](https://github.com/Busnes-app/KyPost-for-Linux/actions/runs/38335338279)

- **Changes:** Move active project, release-check and channel coordinates to the current Busnes-app organization.
- **Upgrade impact:** Check repository and Flatpak remote URLs against current distribution guidance. Preserve signing trust and the existing profile; do not replace credentials or trust an unrelated old-owner endpoint as a migration shortcut.

### c97e95b5 — checked publication 2026-10-12

Source: [c97e95b5883a8bd4e9b0da99c747f1155b6d874e](https://github.com/Busnes-app/KyPost-for-Linux/commit/c97e95b5883a8bd4e9b0da99c747f1155b6d874e) · [Publication evidence](https://github.com/Busnes-app/KyPost-for-Linux/actions/runs/38253764496)

- **Changes:** Add relay-side search with in-memory results and resumable plain/encrypted drafts. Replacing a reopened draft is tied to the first successful save/send.
- **Upgrade impact:** Search requires a compatible relay endpoint. Preserve the local GnuPG account key for encrypted drafts; failed key/encryption operations do not authorize plaintext fallback or deletion of the original draft.

### b1263ffe — checked publication 2026-10-12

Source: [b1263ffe69f439ddae231a17460c6bd7118a0ac0](https://github.com/Busnes-app/KyPost-for-Linux/commit/b1263ffe69f439ddae231a17460c6bd7118a0ac0) · [Publication evidence](https://github.com/Busnes-app/KyPost-for-Linux/actions/runs/38224099100)

- **Changes:** Fix mobile root loading, add pull-to-refresh and connection status, and recover from long distributor-push silence with a bounded pull/refresh backstop.
- **Upgrade impact:** No manual profile migration is introduced. Polling/backstop refresh may recover missed updates; keep using the selected system distributor rather than an embedded third-party subscriber.

### 2f0be933 — checked publication 2026-10-12

Source: [2f0be9338935d3fcac05496bd31dcf0620f11fbe](https://github.com/Busnes-app/KyPost-for-Linux/commit/2f0be9338935d3fcac05496bd31dcf0620f11fbe) · [Publication evidence](https://github.com/Busnes-app/KyPost-for-Linux/actions/runs/38220712629)

- **Changes:** Add sign-in MFA approval using matching digits from encrypted push or authenticated pull, with expiry, deduplication and app-lock checks.
- **Upgrade impact:** Use a relay supporting this MFA flow and unlock the app before answering. Matching the displayed number is the confirmation; this is not biometric approval and cleartext distributor challenges remain refused.

### 92dcf525 — checked publication 2026-10-12

Source: [92dcf5255bdda1f60d4ffc89e08c6c6f1eae69e5](https://github.com/Busnes-app/KyPost-for-Linux/commit/92dcf5255bdda1f60d4ffc89e08c6c6f1eae69e5) · [Publication evidence](https://github.com/Busnes-app/KyPost-for-Linux/actions/runs/38215892147)

- **Changes:** Add move-to-folder and mobile folder-management flows with account/folder ownership checks.
- **Upgrade impact:** The relay must support the requested folder operation. Keep existing mailboxes; the upgrade does not rename folders or regenerate pairing credentials.

### d419fe70 — checked publication 2026-10-12

Source: [d419fe7092e32627d257800c909320edaa3f6a76](https://github.com/Busnes-app/KyPost-for-Linux/commit/d419fe7092e32627d257800c909320edaa3f6a76) · [Publication evidence](https://github.com/Busnes-app/KyPost-for-Linux/actions/runs/38209776561)

- **Changes:** Recheck which pairing owns an arriving push before delivering it, including after key lookup spans an account replacement.
- **Upgrade impact:** Stale pushes are discarded rather than attributed to the new account. No re-pairing or data migration is needed for the upgrade itself.

### 45d8094c — checked publication 2026-10-12

Source: [45d8094ce82e2ba9ac58f745757c33842b95812a](https://github.com/Busnes-app/KyPost-for-Linux/commit/45d8094ce82e2ba9ac58f745757c33842b95812a) · [Publication evidence](https://github.com/Busnes-app/KyPost-for-Linux/actions/runs/38206704845)

- **Changes:** Record Ubuntu Touch feasibility and the proposed shared-core route.
- **Upgrade impact:** This is research documentation; it does not ship an Ubuntu Touch application or change the desktop/mobile package.

### ba4445b7 — checked publication 2026-10-12

Source: [ba4445b74c0c8621cebf85343b8f77d2f7ce08fd](https://github.com/Busnes-app/KyPost-for-Linux/commit/ba4445b74c0c8621cebf85343b8f77d2f7ce08fd) · [Publication evidence](https://github.com/Busnes-app/KyPost-for-Linux/actions/runs/38200725114)

- **Changes:** Add explicit read/unread actions with local optimistic state and rollback when the relay refuses a change.
- **Upgrade impact:** No manual data/key migration is needed. A refused relay action may restore the previous read state instead of leaving a false local success.

### 45663ac6 — checked publication 2026-10-12

Source: [45663ac608792caae37cf55ddb7148e05769ffb4](https://github.com/Busnes-app/KyPost-for-Linux/commit/45663ac608792caae37cf55ddb7148e05769ffb4) · [Publication evidence](https://github.com/Busnes-app/KyPost-for-Linux/actions/runs/38195756171)

- **Changes:** Encrypt UnifiedPush payloads with RFC 8291 aes128gcm and keep the device's push key material in the secret store.
- **Upgrade impact:** Encrypted distributor delivery needs compatible relay support and registered device keys. Keep the OS keyring available; polling remains the fallback. This does not authorize accepting a cleartext MFA challenge.

### 8f62026f — checked publication 2026-10-11

Source: [8f62026fdf9ce1cedcb6252bd98f45ca306681e1](https://github.com/Busnes-app/KyPost-for-Linux/commit/8f62026fdf9ce1cedcb6252bd98f45ca306681e1) · [Publication evidence](https://github.com/Busnes-app/KyPost-for-Linux/actions/runs/38135415032)

- **Changes:** Add automated OpenSSF Scorecard analysis/result publication.
- **Upgrade impact:** This repository-analysis change introduces no application or profile migration; a Scorecard result is not an independent product audit.

### 544648b1 — checked publication 2026-10-10

Source: [544648b1d7d32469ff0763bc28f641959eb52a6b](https://github.com/Busnes-app/KyPost-for-Linux/commit/544648b1d7d32469ff0763bc28f641959eb52a6b) · [Publication evidence](https://github.com/Busnes-app/KyPost-for-Linux/actions/runs/38047491437)

- **Changes:** Adopt the shared Busnes product icons across launcher and packaged assets.
- **Upgrade impact:** Refresh the installed application/desktop cache to see new icons. This asset interval introduces no profile, pairing or theme-policy migration.

### 05c897a7 — checked publication 2026-09-24

Source: [05c897a790c40eb01ab6f177d99589de71b65df1](https://github.com/Busnes-app/KyPost-for-Linux/commit/05c897a790c40eb01ab6f177d99589de71b65df1) · [Publication evidence](https://github.com/Busnes-app/KyPost-for-Linux/actions/runs/35984709125)

- **Changes:** Improve native OpenPGP handling: transient protected subjects/attachments and inline images, exact-byte signature verification, encrypted drafts and reopening, and confirmed/current recipient-key selection. Add complete-ring import and v3 crypto preparation.
- **Upgrade impact:** Retain GnuPG's historical keys for decryption. Drafts in client custody need the account key and cannot fall back to plaintext after encryption fails. V3 importer/vector support here does not by itself claim complete-ring enrollment was advertised or conversion-ready.

### ebcd85d7 — checked publication 2026-09-15

Source: [ebcd85d7b17fdfbb04ab52321f91a12bfe7d18ab](https://github.com/Busnes-app/KyPost-for-Linux/commit/ebcd85d7b17fdfbb04ab52321f91a12bfe7d18ab) · [Publication evidence](https://github.com/Busnes-app/KyPost-for-Linux/actions/runs/34955982972)

- **Changes:** Release the 0.4.0 source milestone with corrected startup theme/icon behavior, reorganized settings, immediate interface-mode changes and persistent keyword ordering. Pairing uses the verified transport result rather than persisted endpoint announcements.
- **Upgrade impact:** Keep profile and secret-store contents. Review a certificate-change banner against the real relay before reconnecting; do not replace key material to quiet it. This milestone has an AppStream entry, not a currently observed matching Git tag/GitHub Release.

### 6d9840d6 — checked publication 2026-08-30

Source: [6d9840d68462678fd4fecd171f51388352c77e2f](https://github.com/Busnes-app/KyPost-for-Linux/commit/6d9840d68462678fd4fecd171f51388352c77e2f) · [Publication evidence](https://github.com/Busnes-app/KyPost-for-Linux/actions/runs/33306581064)

- **Changes:** Add project funding metadata.
- **Upgrade impact:** This is repository metadata only; application behavior and stored formats are unchanged.

### 4f0473f7 — checked publication 2026-08-28

Source: [4f0473f77c6b9ae2ab5da1df49e85938605de2a0](https://github.com/Busnes-app/KyPost-for-Linux/commit/4f0473f77c6b9ae2ab5da1df49e85938605de2a0) · [Publication evidence](https://github.com/Busnes-app/KyPost-for-Linux/actions/runs/33170232952)

- **Changes:** Sanitize untrusted HTML before seeding the composer and bound MIME traversal so oversized or hostile content cannot silently become editor markup.
- **Upgrade impact:** Expect malformed/oversized messages or seeds to be refused or limited rather than rendered. This interval does not require re-pairing or replacement of stored keys.

### 1250c657 — checked publication 2026-08-27

Source: [1250c65719a1f9463036ccc082d0510b97792e22](https://github.com/Busnes-app/KyPost-for-Linux/commit/1250c65719a1f9463036ccc082d0510b97792e22) · [Publication evidence](https://github.com/Busnes-app/KyPost-for-Linux/actions/runs/33127530064)

- **Changes:** Honor the relay-published TLS pin in the pairing flow and document how re-registration establishes the anchor.
- **Upgrade impact:** A mismatched pin refuses registration; verify the relay rather than bypassing the check. Later source builds in this archive refine the anchor to the verified transport result. No database migration is introduced here.

### 2f31fa83 — checked publication 2026-08-27

Source: [2f31fa83ce671688316a9ae176ff35688c4b3916](https://github.com/Busnes-app/KyPost-for-Linux/commit/2f31fa83ce671688316a9ae176ff35688c4b3916) · [Publication evidence](https://github.com/Busnes-app/KyPost-for-Linux/actions/runs/33093558658)

- **Changes:** Use Argon2id for the app-lock verifier, upgrade legacy PIN verifiers after a correct unlock, and keep memory-allocation failures from counting as wrong guesses. Strengthen database/key erasure, reopen handling, legacy-keyring cleanup and TLS pin checks.
- **Upgrade impact:** Unlock once to allow a legacy verifier upgrade; older builds cannot read the new verifier format, so avoid downgrading afterward. Keep the OS keyring available. Normal upgrades retain mail and keys; explicit wipe actions now erase the encryption key and database together.

### b2965fa3 — checked publication 2026-08-27

Source: [b2965fa320b4ee6ef1150b2b6ae4bf6fd41552b0](https://github.com/Busnes-app/KyPost-for-Linux/commit/b2965fa320b4ee6ef1150b2b6ae4bf6fd41552b0) · [Publication evidence](https://github.com/Busnes-app/KyPost-for-Linux/actions/runs/33033456254)

- **Changes:** Include interrupted encryption-migration copies in wipe paths, correctly quote profile paths containing apostrophes during encryption, and improve pairing-persistence error reporting and UI wording.
- **Upgrade impact:** Preserve profile/keyring state during a normal upgrade. A profile whose path previously prevented encryption can now complete conversion; check the encryption result. Wipe actions remain deliberately destructive and now account for leftover copies.

### d1049b35 — checked publication 2026-08-27

Source: [d1049b357d32e0f42474df12d6ed49a405fd926d](https://github.com/Busnes-app/KyPost-for-Linux/commit/d1049b357d32e0f42474df12d6ed49a405fd926d) · [Publication evidence](https://github.com/Busnes-app/KyPost-for-Linux/actions/runs/33025333747)

- **Changes:** Correct the security policy's disclosure timeline to describe the actual process.
- **Upgrade impact:** No application update procedure or data migration is introduced by this policy-only interval.

### 0cbad895 — checked publication 2026-08-26

Source: [0cbad8953f69b8308cfcd3bf6f6194d8ceab4c1d](https://github.com/Busnes-app/KyPost-for-Linux/commit/0cbad8953f69b8308cfcd3bf6f6194d8ceab4c1d) · [Publication evidence](https://github.com/Busnes-app/KyPost-for-Linux/actions/runs/32994028332)

- **Changes:** Add structured bug/feature intake and a repository security-reporting policy.
- **Upgrade impact:** Use the documented reporting channels. This interval changes project guidance without an application or profile migration.

### fab1ace6 — checked publication 2026-08-26

Source: [fab1ace67599b3f2b4fd523b61263e3470c59177](https://github.com/Busnes-app/KyPost-for-Linux/commit/fab1ace67599b3f2b4fd523b61263e3470c59177) · [Publication evidence](https://github.com/Busnes-app/KyPost-for-Linux/actions/runs/32935620434)

- **Changes:** Show the installed version and newer-release information from the paired relay, with an update notice/toast that survives the lock screen and rejects malformed version claims.
- **Upgrade impact:** The notice does not install updates; continue using the signed Flatpak remote. A relay without release metadata cannot establish that the client is current. No profile-format migration is added here.

### e1a1a9c0 — checked publication 2026-08-25

Source: [e1a1a9c0a2e6544a48eef2ad5b42f1ed0cd5f993](https://github.com/Busnes-app/KyPost-for-Linux/commit/e1a1a9c0a2e6544a48eef2ad5b42f1ed0cd5f993) · [Publication evidence](https://github.com/Busnes-app/KyPost-for-Linux/actions/runs/32881992363)

- **Changes:** Render mail according to the relay's parsed HTML/plain classification rather than guessing from text, and persist that classification with cache migration 007.
- **Upgrade impact:** Keep the encrypted cache for its automatic migration. Some messages previously guessed to be HTML now display as plain text; this is intentional. No key replacement is needed.

### fbc404c5 — checked publication 2026-08-25

Source: [fbc404c5bd2947c010d2d02f4efb1bb7c47ce02c](https://github.com/Busnes-app/KyPost-for-Linux/commit/fbc404c5bd2947c010d2d02f4efb1bb7c47ce02c) · [Publication evidence](https://github.com/Busnes-app/KyPost-for-Linux/actions/runs/32814240284)

- **Changes:** Document the working signed channel and the separate limitation that standalone bundle installs have no configured update origin.
- **Upgrade impact:** Install through the documented remote for ongoing updates. A downloaded bundle alone does not establish an updating, verified remote; no application data migration occurs in this documentation interval.

### 0e070948 — checked publication 2026-08-24

Source: [0e070948f6343bb2a2bf452bc2a51f572ad0ea5b](https://github.com/Busnes-app/KyPost-for-Linux/commit/0e070948f6343bb2a2bf452bc2a51f572ad0ea5b) · [Publication evidence](https://github.com/Busnes-app/KyPost-for-Linux/actions/runs/32763177682)

- **Changes:** First observed signed-channel publication: relay-only desktop/mobile mail, contacts, device pairing, local OpenPGP through GnuPG, and SQLCipher profile encryption are delivered for x86_64 and aarch64.
- **Upgrade impact:** Preserve the profile, OS secret store and GnuPG keyring. Existing plaintext/old-application-ID profiles are migrated on launch; keep recovery copies until the encrypted profile opens successfully. Use the signed remote for updates.
