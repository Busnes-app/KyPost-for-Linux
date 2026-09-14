**Repo:** kypost-Linux
**Worktree:** /home/yoshi/busness.app/kypost-Linux (branch feat/linux-pgp-parity)

# PGP parity: first implementation checkpoint

Owner: Usagi / GPT-6 / alder. Date: 2026-09-14.
Task remains claimed: `kypost-linux-pgp-parity`. No PR or remote push yet.

## Completed

- `b1b9bf6`: committed the implementation plan at
  `docs/superpowers/plans/2026-09-13-linux-pgp-parity.md`.
- `e1ec5c6`: implemented protected Subject reading. MIME parsing returns the
  decrypted root Subject or the leading protected legacy header, with RFC 2047
  decoding and control-character cleanup. Nested attachment subjects cannot
  replace the parent subject. Reader headings, reply/forward subjects and pop-out
  titles use the transient value; the database keeps its outer subject.
- Moved MIME parsing onto NetworkExecutor and introduced explicit malformed/limit
  failures with no partial content. Existing depth/part/header/walk bounds remain.
- Scoped read/display state to pairing identity, mailbox and UID. Readers can now
  select cached messages with duplicate UIDs by passing the folder explicitly.
- Fixed a related lifetime defect: forgetting a result now invalidates pending
  work even when no plaintext has arrived yet. A lock/unlock or navigation during
  a pending read cannot restore the old result. main seeds and updates the C++
  app-lock gate; locked invocations are refused.
- `fdf8a2d`: repaired guard-manifest targets. Two pre-existing entries were stale:
  ComposeSeeding named a QML case as a CMake target, and the settings persistence
  guard matched two lines. Also corrected the same target mistake in the new QML
  entry. The settings source change is a distinguishing comment only.
- Updated AGENTS.md, docs/PARITY.md and docs/THREADING.md for the implemented behavior.

## Verification

- RelWithDebInfo configure and full build passed with SQLCipher 4.14.0, rebuilt
  using `scripts/build-sqlcipher.sh /tmp/sqlcipher`. The old cached prefix had
  disappeared; verified the final test executable loads
  `/tmp/sqlcipher/lib/libsqlite3.so.0`.
- All 100 ctest targets passed. One pre-existing Secret Service timing case skipped
  because this machine answers immediately; the SQLCipher cases ran.
- MimeBodyReaderTest and MailDecryptionTest passed under ASan/UBSan in build-asan.
- `scripts/verify-guards.sh`: all 59 guards proven load-bearing on the final run,
  including invalidated reads, mailbox display isolation and MIME byte bounds.
  Its first run reported the three manifest issues described above; they were
  corrected rather than counted as passes. The script restored and rebuilt the
  tree successfully. `git diff --check` passed.
- No live Proton test, deployed-relay test, Flatpak UI exercise or full native
  parity claim. GPG tests used disposable keyrings, not the user's keyring.

## Remaining and next work

Continue with step 2 of the plan: attachment extraction, bounded RFC 2231 parsing,
attachment-only messages, explicit save handling and memory-only CID rendering.
The current parser still returns only body/subject and uses the pre-existing
8 MiB cumulative walk budget. It must not be presented as attachment parity.
Signed-only detection/verification, delta regression evidence, historical keys
and resolver compatibility work also remain.

The draft dependency has changed since planning. Myslop server posts 608/614 and
PR #185 establish a concrete contract; the later post reverses the earlier promise
that native plaintext drafts remain accepted for client-custody accounts:

- `POST /api/mail/draft` accepts `pgpDraft`, a complete self-encrypted PGP/MIME
  message, plus a valid `to` string. Protect To/Cc/Bcc/Subject inside the encrypted
  entity. Keep cleartext Cc/Bcc/body/attachment fields out of that request.
- Client-custody accounts without `pgpDraft` receive 409 with
  `clientSideNeeded: true`. Other custody modes retain plaintext drafts.
- Source checked: `backend/internal/api/server_mail_send.go`:
  `decodeMailRequest` and `serveDraftSave`, plus `encrypted_draft_test.go`.
- Linux draft saving and webmail handoff have NOT yet been updated and will receive
  that 409 when talking to the changed server. Bootstrap/custody lookup must fail
  closed before sending draft contents; do not first post plaintext to learn that
  encryption was needed. Preserve the composer on failure.
- Recheck PR/deployment state and source before implementation. Historical-key
  envelope plaintext shape is still a separate dependency; keep the importer's
  single-primary-fingerprint guard and durable GnuPG custody.

Implementation is unfinished, but there is no blocker to starting the attachment
reader. Keep the task claimed until resumed or explicitly handed off.
