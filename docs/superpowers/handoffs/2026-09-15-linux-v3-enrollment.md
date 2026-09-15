**Repo:** kypost-Linux
**Worktree:** /home/yoshi/busness.app/kypost-Linux (branch feat/linux-pgp-parity)

Owner: Usagi / GPT-6 / alder. Date: 2026-09-15.
This supersedes the start note in Myslop posts 768/769. No PR or push yet.
Code tested at 08421d6; earlier parity implementation remains on this branch.
Server contract pinned to merged PR199, dc2a70eb3a5288be64dd5e08482844a80dfb8969.

## Completed increment

DeviceEnrollmentCrypto::openKeyringEnvelope explicitly consumes v3 framing using
the existing OpenSSL P-256 ECDH, HKDF-SHA256 and AES-256-GCM implementation.
V3 binds its distinct domain in both HKDF and AAD, exact nonempty UTF-8 device ID,
and uppercase 40/64-character active fingerprint. Canonical padded base64 and the
128 KiB serialized-envelope limit are enforced before decryption. Invalid points,
wrong lengths, unsupported version/algorithm, wrong bindings/domains and bad tags
return no plaintext. Shared secrets, AES keys and plaintext remain SecureBytes.

The result is authenticated RAW keyring JSON, not a validated or imported ring.
The production controller stays on the v2-only openEnvelope entry point, and
legacy v2 vectors still pass. No capability advertisement, HTTP shape, import,
acknowledgement, active-key pointer, persistence or teardown behavior changed.
No new dependency or private-key vault was introduced.

Vendored tests/fixtures/device-envelope-v3.json is byte-identical PUBLIC TEST DATA
from the server merge above; SHA256:
0ce8d5ac20ec94bcc35facff6e76fcf78ec0d66f666aabc2dcef1965408e9c3a.
Fixed scalars/IVs exist only in tests. Production still generates ephemeral keys.

## Verification

- Fresh RelWithDebInfo build and all 101 ctest targets passed.
- DeviceEnrollmentCryptoTest passed under ASan/UBSan.
- All 81 manifest guards proved load-bearing; sources restored and rebuilt cleanly.
  Four new mutations prove v3 size admission, explicit version checks, canonical
  base64 and GCM authentication failure handling.
- Shared v2/v3 vectors match ECDH shared secret, HKDF AES key, exact AAD, exact
  decrypted plaintext and reproduced ciphertext. Negative tests cover wrong
  device/fingerprint/domain/tag, malformed fields/points, missing base64 padding,
  unsupported versions, key clearing, and UTF-8/envelope-size boundaries.
- SQLCipher 4.14.0 rebuilt with the pinned script into the persistent prefix
  /home/yoshi/.cache/kypost-v3/sqlcipher. Both build and build-asan are configured
  against it. The initial attempted full build/sanitizer run failed because the
  old /tmp/sqlcipher installation was absent; it is not validation evidence.
  The fresh runs above supersede it. At-rest tests ran; one pre-existing Secret
  Service timing case skipped because this provider answers immediately.
- Logs: /tmp/kypost-v3-full-build.log, /tmp/kypost-v3-full-tests.log,
  /tmp/kypost-v3-asan-tests.log, /tmp/kypost-v3-guards.log,
  /tmp/kypost-v3-sqlcipher.log. No live enrollment or interoperability claim.
- AGENTS.md and docs/PARITY.md describe the preparation/activation boundary.
  Independent security review and PR CI remain owed before delivery.

## Blocked decision and next implementation

The user has been asked which Linux custody contract to follow; no answer has
arrived. Server owner redwood acknowledged this as a separate design decision in
Myslop post 772. Server capability negotiation does not authorize a custody change.

The server handoff requires persisting the original versioned JSON, including
unpublished revocation certificates, while also requiring GnuPG-only custody and
no duplicate private store. GnuPG imports packet material; it does not retain the
original JSON or an unpublished revocation certificate as an opaque secret.
Importing such a certificate applies the revocation. Linux's existing teardown
intentionally never deletes keys from the user's keyring, unlike the generic
device-wipe paragraph.

Required decision:
1. Keep GnuPG-only custody and revise the Linux persistence acceptance requirement,
   explicitly specifying treatment of unpublished revocation certificates; or
2. Explicitly authorize an encrypted original-bundle archive alongside GnuPG and
   define its protection, durability and teardown contract.

Do not implement a second store, discard certificates, apply unpublished
revocations, or alter key deletion rules by inference.

After that decision, implement bounded complete-ring validation without putting
private JSON strings into QString; validate all primary/subkey members and the
exact inventory in disposable state; persist according to the agreed contract;
check every import and preserve existing keys on partial failure; report success
and switch active selection only after all required durability succeeds. Test
fresh/re-enrollment, partial failures, retry, process restart, old-mail decrypt
and active signing with disposable stores. Keep the legacy single-primary guard.

Production integration also awaits the actual capability/upload/acknowledgement
and generation contract; never invent fields. No readiness advertisement or
conversion activation from vector success. Live Flatpak, Proton/Thunderbird and
paired-relay drills from the original parity task remain outstanding. Keep the
owner assigned; this task is not complete. No sibling code was changed.
