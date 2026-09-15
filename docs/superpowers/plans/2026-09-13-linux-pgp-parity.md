**Repo:** kypost-Linux
**Worktree:** /home/yoshi/busness.app/kypost-Linux (branch main)

# Linux PGP parity implementation plan

Date: 2026-09-13. Planning complete; implementation has not started.
Myslop task: `kypost-linux-pgp-parity`, claimed by alder.

## Scope and evidence

Implement the Linux reader and compatibility work from Myslop post 594, following
the latest server punch list (post 591, superseding the earlier versions).
Linux baseline: `ebcd85d7b17fdfbb04ab52321f91a12bfe7d18ab`.
Server source inspected: `e7fecb7e7218bda55759d815746863fa6f31e682`, plus its
local `docs/superpowers/plans/2026-09-13-pgp-punch-list.md` and native-client handoff.
Recheck source and board before implementation; these are source observations,
not evidence of deployed server behavior.

Verified differences from the handoff:

- Linux already imports device envelopes and sends attachments inside ciphertext
  through `PgpMimeWriter` and `PgpSendPlanner`. Extend and test the reader; do not
  rebuild enrollment or encrypted attachment sending.
- `MimeBodyReader` returns HTML/plain text only. Its current limits are depth 8,
  64 parts, 64 KiB headers and 8 MiB cumulative walked bytes; hitting a limit can
  silently return a partial result. The decryptor separately caps output at 32 MiB.
- `PgpPayloadClient` drops detached-signature fields. In addition,
  `RelayMailSource` drops the server's existing `pgpSigned` flag, and
  `PgpMessageState` classifies only encrypted messages. Fix the complete read path.
- `PgpResolveClient` already preserves tiers as strings; `PgpPayloadClient` does
  not parse signer-key tiers at all. This is a compatibility/policy test task,
  not a strict-enum decoder replacement.
- GnuPG already stores keys by fingerprint. The importer intentionally accepts
  exactly one primary fingerprint per import. Do not weaken that guard to accept
  an unspecified historical-key bundle or create a second private-key vault.

## Implementation sequence

Use small reviewable changes, each with its relevant runnable checks. Begin with
steps 1 and 2. Step 3 is conditional on the server's draft decision; if its contract
is absent, leave it explicitly pending while preparing independent Linux work.
Steps 4–6 align with server tiers 3, 5 and 6. This does not change the server's
strict tier order or authorize work in the sibling repositories.

### 1. Protected Subject and explicit MIME outcomes

Extend `core/pgp/MimeBodyReader.{h,cpp}` in place. Return the protected Subject
alongside bodies, with an explicit complete/unsupported/malformed/limit result
so future attachments cannot disappear behind a successful partial parse.
Read the inner protected entity's Subject and its legacy `text/rfc822-headers`
representation, using deterministic precedence. Never take a subject from an
arbitrary nested attachment or attached message. Decode folded/encoded header
values with a bounded helper; handle UTF-8 and RFC 2047 examples from actual mail.
Keep inline-PGP plain-text fallback for input that is genuinely not MIME.

Carry this through `MailController`'s transient protected-message state and
`EmailDetail.qml`. Display the inner subject after a successful read, including
reply/forward seeding and pop-out titles. Keep the outer cached subject as fallback;
do not persist protected subjects to EmailDao, search, settings or notifications.
A decrypted subject is protected content, not proof of sender identity.

Use a full identity for every new read/display/save operation: pairing identity,
mailbox, UID, and request generation. Extend the existing one-message memory state,
not a new cache. Clear/invalidate it on lock, account change, folder/message change
and cancellation; late worker results must not repopulate it. UID alone is not
unique across folders. Recheck these conditions when exposing data to each window.

Checks: extend `MimeBodyReaderTest`, `MailControllerTest`, and
`tst_DecryptedBody.qml`: KyPost writer round trip, encoded/folded subject, absent
subject, hostile nested subject, same UID in two folders, lock during read,
account replacement, pop-out switching, and no database write of protected values.

### 2. Encrypted attachment reading, saving and CID display

Extend the same MIME result with attachment metadata (stable part ID, decoded
filename, MIME type, disposition, Content-ID) and bytes held in C++ memory.
Classify named text parts as attachments rather than body; support mixed,
alternative and related nesting. Add RFC 2231 extended/continued filenames,
quoted parameters, base64 and quoted-printable decoding. Treat `message/rfc822`
as a downloadable attachment initially; do not recurse into it to choose the
parent message's subject/body. Mark that deliberate limit with `ponytail:`.

Retain depth 8, 64 parts and 64 KiB per header block. Bound input and aggregate
decoded output by the existing 32 MiB decrypt ceiling, and retain a separate
bounded traversal-work budget sized for permitted nesting. Count all decoded
parts, including ignored ones; check sizes before allocating. Reconcile these
client limits with the relay fixtures rather than blindly copying the browser's
25 MiB limit. Exceeding a bound or malformed encoding yields an explicit failure,
never a UI implying all files arrived. Permit attachment-only and zero-byte files.
Move expanded MIME parsing onto the existing worker, before applying UI state.

Expose metadata through `MailController`; keep bytes out of QVariant/QML models.
Reuse the attachment row in `EmailDetail.qml`, routing protected part IDs to local
bytes, never to relay attachment indexes. Save only after a user chooses a path
through the application's Qt save dialog. Reuse filename sanitization and checked
writes, report cancel/write failure honestly, and preserve Hostile Location
Protection's existing ephemeral-only policy in C++. Do not auto-open files.

Serve CID images from an app-layer, memory-only WebEngine scheme handler installed
on the existing off-the-record mail profile. URLs identify only the current
message generation and part; reject stale, unknown and cross-message requests.
Register/install it in the composition root, with no QtGui/WebEngine dependency
in core. Allow only safe raster image MIME types as inline resources, with bounded
image dimensions; leave SVG/HTML/unknown parts downloadable. Give these local
image requests a narrow exception in `RemoteContentInterceptor`; remote images
remain governed by the current opt-in. A sender-supplied URL must never select
another message's bytes. Revoke resources and clear the rendered page on lock.

Checks: parser fixtures for multiple/empty/binary/odd-named files, RFC 2231,
malformed encoding, duplicate CIDs, all limits and attachment-only mail; native
writer-to-reader round trips. C++/QML tests prove save cancellation, failed writes,
HLP enforcement, account/lock invalidation, CID isolation and blocked remote
beacons. Confirm Flatpak save-dialog behavior on desktop and mobile.

### 3. Draft compatibility — contract-dependent

Owning paths: `RelayMailSource::saveDraft`, `MailController::saveDraftInternal`,
`openWebmailDrafts`, and `Compose.qml`'s token-correlated completion handling.
Before implementing, obtain the server's actual request/response definition for
an encrypted draft, its row marker and reopen behavior. The existing endpoint
name alone is not an encrypted-draft contract.

If plaintext native drafts remain accepted, retain the existing webmail handoff
and test it against that policy; this alone is not encrypted-draft parity. When
an encrypted-save contract exists, encrypt explicit protected drafts to the
current self key with attachments inside the same MIME entity, and reopen through
the protected reader into the composer. Do not downgrade a requested encrypted
save to plaintext. Preserve compose contents on failed save and open webmail only
after confirmed success. If plaintext drafts are rejected, update the handoff to
the agreed encrypted route before claiming compatibility.

Required server answers: exact encrypted draft fields/marker, whether legacy
plaintext saves are accepted, and how webmail opens a native encrypted draft.
No guessed fields, interim UI block, or new endpoint is part of this plan.

### 4. Signed-only reading and delta regression

Parse existing `pgpSigned` from `server_inbox.go` through the Email model, DAO,
transactional schema migration and refresh merge, and expose a verify/read action.
Do not rely on `pgpEncrypted` to discover signed-only messages. Existing cached
rows need a controlled refresh/backfill strategy; a default false column alone
will not discover old signed mail. Preserve cursor protocol during that refresh.

Parse `signaturePayload` and strictly decode bounded `signedPartBase64` in
`PgpPayloadClient`. Distinguish encrypted, detached-signed, absent and malformed
payloads. Verify the exact decoded octets using GPGME's detached verification
operation before MIME rendering; do not normalize line endings or regenerate MIME.
Extend the existing GPGME wrapper and reuse its signature extraction, primary-key
resolution, checked signer imports and `EncryptedMessageReader` sender binding.
Do not credit the display From header or a server `pgpVerified` boolean.

Separate readable signed content from successful decryption in the result model.
A bad/uncheckable signature may leave signed-only content readable with a clear
warning, never a verified badge. A malformed payload is not an empty successful
message. Label encrypted-but-unsigned explicitly through app-layer translation.
Keep cryptographic operations on NetworkExecutor with synchronous busy/coalescing.

Checks: real GPGME detached fixtures for valid, tampered, missing-key,
unknown/conflicting sender binding, signing subkey, CRLF-sensitive data and invalid
base64. Cover the route from inbox flag to reader UI, including plain messages and
attachment-only signed mail. Extend payload, reader, model/repository, controller
and QML tests rather than testing only the low-level verifier.

Run `RelayMailSourceTest` and `MailRepositoryTest` with a large encrypted window:
initial/forced `since=0` persists the returned cursor; subsequent polls send it;
updated rows preserve cached bodies appropriately; removed rows disappear; folder
cursors remain independent; failed writes never advance a cursor; stale account
replies cannot insert or delete. Repeat against the changed server handler once
its cache tier lands. Local stub tests alone cannot prove server cache behavior.

### 5. Historical keys and retirement — contract-dependent

Keep durable key custody in the user's existing GnuPG keyring, naturally indexed
by primary fingerprint. Add only public account-to-fingerprint/current-key metadata
if a demonstrated selection need remains after inspecting bootstrap/enrollment.
Never export/re-seal retired private keys to the relay and never delete them.

The current enrollment path unwraps one secret and checks one expected fingerprint.
Require a concrete server/webmail fixture defining how historical key records and
their expected fingerprints are authenticated inside the envelope. An unchanged
outer envelope does not specify a new multi-key plaintext format.

Validate the complete declared collection in scratch storage before durable
imports; keep each individual import's single-primary-fingerprint check. Enforce
key-count/byte limits and declared fingerprint uniqueness. Merge validated keys
idempotently, handle partial import failure explicitly, and report enrollment
complete only after all required imports succeed. Clear SecureBytes and ephemeral
ECDH state on every exit; no QString private keys. Use the new current key for
future signing, leaving GnuPG able to decrypt to historical keys.

Use the existing authenticated enrollment ceremony. If retirement also requires
mail-device re-pairing, retain `PairingController`'s successful-pairing purge gate;
PGP enrollment alone must not introduce another account purge. Audit async identity
checks around durable imports and completion reporting.

Checks in isolated GNUPGHOMEs: old mail decrypts after re-enrollment and restart;
new mail selects current key; repeats merge; unrelated keys survive; mismatched,
extra and truncated records fail; partial import is retryable; failed re-pairing
does not purge account data. Never test against the user's real keyring.

### 6. Resolver compatibility and completion evidence

Add fixtures with `expired`, `revoked`, missing and unknown future tiers, and
unknown signer-key fields. Keep raw tier strings. Define send eligibility in C++:
known expired/revoked or unknown tiers cannot become an automatic all-clear merely
because key bytes exist. Preserve the established rules for current known tiers
and the `usable` flag. Add localized distinct labels for expired/revoked where
recipient status is displayed; unknown values receive neutral, untrusted wording.
Signer-key metadata changes must not bypass fingerprint/conflict checks.

Update `docs/PARITY.md` and `docs/THREADING.md` when behavior actually lands.
Perform the required DOX pass; change AGENTS.md only where ownership or constraints
really change. Record interop evidence for protected subject, attachments/CID,
signed-only, encrypted draft and historical decrypt. Use KyPost, Proton and GnuPG
fixtures; label live tests separately from generated/local fixtures. No blanket
parity claim while server-dependent rows or live tests remain unverified.

## Verification and delivery

For each code change, run focused existing tests, then the required full build:

```sh
./scripts/build-sqlcipher.sh /tmp/sqlcipher
cmake -B build -S . -DCMAKE_BUILD_TYPE=RelWithDebInfo -DKYPOST_SQLCIPHER_ROOT=/tmp/sqlcipher
cmake --build build
ctest --test-dir build --output-on-failure
```

Reuse a verified SQLCipher prefix if already available. Review CI before commits;
exercise its sanitizer configuration for the parser/GPGME changes. Run
`scripts/verify-pgp-against-relay.sh` for MIME/send regressions. Add relevant
account/plaintext/sender-binding guards to `tests/guards.tsv` and run
`scripts/verify-guards.sh` under its clean-tree requirements whenever listed guards
change. A skipped test or unapplied neutralization is not verification.

Planning validation: inspected the named Linux paths and server payload/inbox
handlers; no implementation or build/test run performed in this planning task.
The first implementation change should be step 1, followed by the attachment
reader. Server draft and historical-envelope contracts remain external dependencies,
not blockers to those changes. Mirror this entire plan to Myslop and retain the
claim; do not mark the parity task done merely because planning is complete.
