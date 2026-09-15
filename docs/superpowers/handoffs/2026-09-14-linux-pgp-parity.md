**Repo:** kypost-Linux
**Worktree:** /home/yoshi/busness.app/kypost-Linux (branch feat/linux-pgp-parity)

# PGP parity implementation checkpoint

Owner: Usagi / GPT-6 / alder. Date: 2026-09-14.
Task remains claimed: `kypost-linux-pgp-parity`. No PR or remote push yet.
This supersedes implementation checkpoint Myslop post 647.
The user clarified that the failed-security-audit message belonged to another window.
The original plan is `docs/superpowers/plans/2026-09-13-linux-pgp-parity.md`.

## Completed

- Earlier `e1ec5c6`: protected Subject extraction and transient reader headings,
  reply/forward subjects and pop-out titles. Account/mailbox/UID and generation
  checks prevent stale reads after navigation, account replacement or lock.
- `1291464`: protected attachment extraction, private atomic saves and CID images.
  MIME supports binary/empty files, named text parts, RFC 2231 filenames, nested
  multipart bodies and downloadable attached messages. Files stay in C++ memory;
  QML receives metadata and a message token. Exports and image lookups recheck the
  active token/account/lock. HLP refuses permanent exports and retains the existing
  restricted temporary-open path. Raster CID images use an off-the-record,
  uncached WebEngine handler; remote images remain opt-in. Fixed the MIME writer's
  missing delimiter line break for a zero-byte attachment.
- MIME bounds are explicit failures, never partial success: 32 MiB input, 64 MiB
  cumulative traversal, depth 8, 64 entities and 64 KiB headers. Raster images
  require positive dimensions and at most 16 million pixels. SVG/HTML are never
  served as inline images.
- `789ef1d`: encrypted draft save and the existing webmail handoff. Fetch fresh,
  type-checked bootstrap custody before any draft POST. Client-custody drafts are
  signed/self-encrypted to the current fingerprint; To/Cc/Bcc, subject, body and
  files go inside. Only `to` and `pgpDraft` go outside. Unknown/failed custody and
  key failures never fall back to plaintext. Enforce the serialized 25 MiB draft
  request limit, coalesce saves, reject stale-account completion and do not open
  webmail while locked. Server-custody/plain accounts retain their ordinary save.
- `44ee3ed`: signed-only reading. Persist `pgpSigned` through migration 008, expose
  Verify signature, strictly decode bounded `signedPartBase64`, and verify the
  exact bytes with GPGME before MIME parsing. Reuse primary-key resolution and
  sender binding. Tampered, unknown-key and conflicting signatures cannot earn a
  verified label; signed content remains readable with its warning. A signed-only
  response cannot impersonate decryption of an encrypted inbox row. Unsigned
  encrypted mail explicitly says so. The versioned mail-cursor namespace forces
  one full window per folder to backfill existing rows, then resumes deltas.
- `46e0af7`: current-key selection and discovery consent. New sends use the current
  bootstrap fingerprint for signing and the self copy, retaining old GnuPG keys
  for decryption. Only usable `verified` and `wkd` recipient keys permit automatic
  use. Keyserver-confirmation, changed, expired, revoked, absent and future tiers
  refuse automatic sending. Decoder strings remain tolerant of unknown tiers.
- `d60bf71`: native encrypted draft reopening on desktop and mobile. After reading
  an encrypted row in Drafts, Edit draft restores protected To/Cc/Bcc, subject and
  body. Mailbox syntax is preserved by the MIME parser; the composer extracts bare
  mailbox addresses without splitting quoted display-name commas. Binary and empty
  files stay in an account-bound C++ session across reader navigation. The token is
  checked even when all attachments are removed. Lock, release, session replacement
  and an observed pairing mismatch invalidate it. A restored draft always uses local
  encrypted sending, including through the legacy send entry point with toggles off;
  a save cannot downgrade to server-custody plaintext. HTML is parsed in an inert
  template and cleaned with the editor allowlist before entering the live page.
  Desktop composition changes recreate the editor and each editor owns its seed.
  Limits are explicit: one restored draft at a time; Save Draft creates a copy using
  existing APPEND semantics; inline images remain attached files and the editor keeps
  basic formatting. Detaching an already-restored editor is disabled; opening one
  directly in a desktop window works. Source drafts are retained after save/send.
- Updated AGENTS.md, docs/PARITY.md and docs/THREADING.md. No new dependencies,
  app-owned key vault, key deletion, account purge path or Flatpak permissions.

## Verification

- Full RelWithDebInfo build with `KYPOST_SQLCIPHER_ROOT=/tmp/sqlcipher` passed;
  all 101 ctest targets passed. SQLCipher tests ran. One pre-existing Secret
  Service timing case skipped because this machine answers immediately.
- ASan/UBSan: MimeBodyReaderTest and MailDecryptionTest passed again for draft
  reopening. EncryptedMessageReaderTest and PgpSendPlannerTest passed at the prior
  checkpoint and were unchanged in this step.
- Latest full suite: 101/101 targets passed. After the final desktop lifecycle
  adjustment, QmlTests, QmlPropertyLint and ProtectedImageHandlerTest passed again.
  The draft integration test decrypts, reopens, saves and sends real GnuPG payloads,
  checks recipient restoration and binary/empty files, rejects account/lock reuse
  and plaintext downgrade, and checks the database contains no protected subject.
  A real WebEngine test runs the production seed sanitizer on hostile markup and
  observes no script execution and zero tracker requests. Four new controller
  mutations and one editor-sanitization mutation each turn those tests red.
- Latest `scripts/verify-guards.sh`: all 77 guards proved load-bearing; the script
  restored and rebuilt the source successfully. `git diff --check` passed.
- Real GnuPG fixtures prove encrypted draft round trips with confidential fields
  absent outside ciphertext; detached valid/tampered/CRLF-sensitive/missing-key/
  conflicting-key/signing-subkey cases; attachment-only signed mail; and current
  key selection while old mail still decrypts after restarting gpg-agent.
- A real WebEngine test renders a CID image with scripts disabled while a remote
  tracking endpoint receives zero requests. Revocation prevents cached reuse.
- The native 500-row encrypted-window regression exercises initial since=0,
  cursor acquisition, updated and removed rows, signature flags, folder cursor
  isolation and forced resync. Existing stale-account and failed-write tests pass.
- `scripts/verify-pgp-against-relay.sh` passed four source-level probes against
  merged draft code `5f45075` (server PR #185): delivery shape, whole send request,
  protected subject and draft decoder/shape. Fixed the script's stale Go module
  imports. This is shape/decoder evidence, not live delivery or IMAP APPEND.
- Targeted Go mailcache/API regressions (`PGP|Encrypted|Cursor|Snapshot`) passed
  against warm-cache commit `128a40e` from server PR #186. Both isolated server
  test worktrees were removed afterward; no sibling source changes were left.
- Logs: /tmp/kypost-final-tests.log, /tmp/kypost-asan-tests.log,
  /tmp/kypost-final-guards.log, /tmp/kypost-relay-interop.log,
  /tmp/kypost-warm-regression.log. Latest step: /tmp/kypost-draft-tests.log,
  /tmp/kypost-draft-ui-final.log, /tmp/kypost-draft-asan.log and
  /tmp/kypost-draft-guards.log. Disposable keyrings only; no user-keyring tests.

## Remaining and next work

1. Historical-key enrollment still needs the authenticated envelope-plaintext
   contract and a server/webmail fixture. The unchanged outer ECDH envelope does
   not define how multiple private keys and their expected fingerprints are
   represented. Require the declared record layout, current-key selection,
   fingerprint bindings and collection bounds before implementing imports. Never
   guess concatenated armor, weaken the one-primary-fingerprint importer guard,
   delete retired keys or add a second custody store. Single-key enrollment and
   retaining already-imported old keys continue to work.
2. Live verification remains: encrypted draft save/reopen in webmail; Proton and
   Thunderbird signed/encrypted messages with files and CID images; Flatpak
   desktop/mobile save dialogs, including paths outside Downloads. The current
   checks prove local fixtures and source contracts, not deployed-server behavior.

Latest server posts checked: 643, 649, 663 and 689. Tier 3 work is in PR #188;
its audit belongs to the server task, not this Linux task. The sibling checkout
was at `944fa30` when last read. No historical-key envelope bundle contract was
found. Keep the Linux task claimed; the remaining contract and live verification
items prevent a full parity claim. No sibling code was changed.
