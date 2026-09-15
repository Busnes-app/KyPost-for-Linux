**Repo:** kypost-Linux
**PR:** #61 — https://github.com/Busness-app/KyPost-for-Linux/pull/61
**Worktree:** /home/yoshi/busness.app/kypost-Linux (branch feat/linux-pgp-parity)

Owner: Usagi / GPT-6 / alder. Date: 2026-09-15.
This supersedes the start note in Myslop posts 768/769. Branch published; PR61 is ready for review.
Crypto code tested at 08421d6; GnuPG-only import code tested at e669d7d.
Earlier parity implementation remains on this branch.
Server contract pinned to merged PR199, dc2a70eb3a5288be64dd5e08482844a80dfb8969.

## Previous crypto increment

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

## Previous crypto verification

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

## Custody decision and next implementation

User decision, 2026-09-15: **GnuPG-only**. This supersedes the blocked question
in Myslop posts 775/776. Keep durable private-key custody in the user's GnuPG
keyring; do not add an original-JSON archive or a second encrypted key store.
Existing keys remain untouched by unpair/wipe. The Linux acceptance contract
therefore measures imported key material and inventory, not original JSON bytes.

Unpublished revocation certificates cannot be imported without applying the
revocation. The Linux importer will explicitly refuse bundles containing them
before writing to the real keyring. It will neither discard them while claiming
complete enrollment nor apply them. The server-side sealed ring remains their
home; activation needs the server to accommodate this Linux acceptance policy.
This is a supported-payload limit, not authorization for a second custody system.

## GnuPG-only import increment

Implemented importPrivateKeyring as an explicit preparation API; production v2
controller wiring and its boolean acknowledgement remain unchanged. It requires
the caller's expected active fingerprint, material generation and complete
fingerprint inventory from an authenticated server snapshot. It does not derive
that authority from the incoming ring. The server generation delivery/acknowledgement
contract is still needed before wiring this API into enrollment.

- Bounded ASCII schema decoding holds private strings only in owned SecureBytes;
  no QJson private-key strings, archive or new dependency. Reject unknown/duplicate
  fields, invalid/non-safe generations, missing/duplicate members, and more than
  128 KiB JSON, 16 primary members or 256 fingerprints. Fingerprints normalize case.
- Inspect armored packet framing before GnuPG: no compressed/literal/encrypted
  packets or partial/indeterminate lengths. GnuPG remains the packet-body/crypto
  authority. Its 2.4.4 import reader expands compressed key packets; this machine's
  2.4.9 independently refuses them. The local compressed-key test therefore does
  not prove the admission guard load-bearing by mutation; recorded in guards.tsv.
- Validate every member in a disposable GnuPG home, including exact primary and
  complete subkey inventory and present, unprotected private material. GPGME
  KEYINFO uses the scratch agent socket resolved by gpgconf; an Assuan home_dir
  alone does not select that agent. Missing/stubbed/protected material is rejected.
- Explicit UnsupportedRevocationCertificate before destination writes. No
  unpublished revocations applied or silently discarded. Existing user keys remain.
- Import only after all validation, check every GPGME import status and re-read
  secret inventory. Cancellation/failure can leave partial imports; never delete
  or report success. Retry merges idempotently. Active selection/ack belongs to
  the future controller and is allowed only after full success.
- Shared legacy importer now checks each GPGME status result, scratch directory
  permissions, and secret_imported when deciding whether material changed. Its
  single-primary guard remains in place.
- Vendored tests/fixtures/pgp-keyring-v1.json is byte-identical public test material
  from server PR199 dc2a70eb3a5288be64dd5e08482844a80dfb8969.

Verification for the import increment:
- Fresh RelWithDebInfo build and all 102 ctest targets passed using SQLCipher at
  /home/yoshi/.cache/kypost-v3/sqlcipher.
- ASan/UBSan: OpenPgpKeyringTest, OpenPgpKeyImporterTest and DeviceEnrollmentCryptoTest
  passed. Ring tests exercise fresh/repeated import, GnuPG agent process restart
  and disk reload, historical and hidden-recipient decrypt, explicit current
  signing, malformed/oversize input, snapshot binding, dummy/public/protected
  material, unsupported certificates, cancellation and retry. A filesystem
  obstruction during the second real import reports failure; restoring the path
  proves the first key survives and retry completes the ring. No power-loss claim.
- First diagnostic runs found/fixed scratch Assuan selection and QtTest SIGPIPE
  during KILLAGENT cleanup; these were not passing validation. Cleanup now closes
  GPGME contexts before stopping only the scratch agent via gpgconf.
- All 88 manifest guards proved load-bearing, including seven new mutations
  for ring size, certificates, snapshot bindings, actual inventory, protected
  private material and final cancellation. Sources restored, rebuilt and git
  clean after the run. The redundant compressed-packet guard is explicitly not
  counted as mutation-proven on GnuPG 2.4.9.
- Logs: /tmp/kypost-ring-full-build.log, /tmp/kypost-ring-full-tests.log,
  /tmp/kypost-ring-asan-tests.log, /tmp/kypost-ring-guards.log.
  SQLCipher tests actually ran; the existing immediate-Secret-Service timing
  case is the only skip in the full suite.
- PR61 CI and autonomous security review are pending. Live enrollment remains owed.

## Remaining integration and delivery

The enrollment-key capability publication contract is available in server PR201
(head 1209911f8e7e2b8db692a13765d4ef0c28b34299, Myslop post 780; open at that
handoff, merge/deployment not checked here). Request field envelopeVersions and
owner-list field enrollmentEnvelopeVersions are distinct; omitted/null publication
resets support to [2]. Do not advertise [2,3] yet. Capability publication alone
provides neither v3 delivery nor generation-bound acknowledgement.

Next server coordination: accommodate Linux's GnuPG-only certificate acceptance
policy and supply the actual complete-ring delivery/current-generation/acknowledgement
contract. Do not invent fields or reuse the legacy boolean enrollment-state ack.
Then wire worker cancellation/account identity, persist public generation/active
selection only after complete import, and test actual enrollment/re-enrollment.
No conversion activation from vector/import success.

Live Flatpak, Proton/Thunderbird and paired-relay drills from the original parity
task remain outstanding. Keep the owner assigned; this task is not complete.
No sibling code was changed. PR61 is open and ready for review; CI and the autonomous
security reviewer are being watched. Merge remains a human decision.

Implementation references:
- [GPGME Assuan socket selection](https://www.gnupg.org/documentation/manuals/gpgme/Using-the-Assuan-protocol.html).
- [GnuPG 2.4.4 import reader](https://github.com/gpg/gnupg/blob/gnupg-2.4.4/g10/import.c),
  read_block's PKT_COMPRESSED branch is the reason to bound framing before GnuPG.
