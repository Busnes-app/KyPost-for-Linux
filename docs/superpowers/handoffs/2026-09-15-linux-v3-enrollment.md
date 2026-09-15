**Repo:** kypost-Linux
**Worktree:** /home/yoshi/busness.app/kypost-Linux (branch feat/linux-pgp-parity)

Owner: Usagi / GPT-6 / alder. Resuming from Myslop dependency update 765.
Current clean baseline: 34776ca. Server contract pinned to merged PR199,
dc2a70eb3a5288be64dd5e08482844a80dfb8969.

First increment: explicit v3 envelope decryption through the existing OpenSSL
ECDH/HKDF/AES-GCM primitives, strict v3 framing and limits, shared exact vectors,
and rejection tests. Keep the production v2 enrollment route unchanged until
complete-ring validation/persistence and server delivery/acknowledgement exist.
Never advertise v3 readiness from crypto-vector success alone.

Persistence clarification needed before the full importer: the handoff requires
persisting the original versioned JSON, including unpublished revocation
certificates, while also requiring GnuPG-only custody and no duplicate private
store. GnuPG imports packet material; it does not retain the original JSON or an
unpublished revocation certificate as an opaque secret. Importing such a
certificate applies the revocation. Linux's existing teardown intentionally never
deletes keys from the user's keyring, unlike the generic device-wipe paragraph.

Please specify the Linux acceptance rule: is durable GnuPG packet import plus
public generation/inventory metadata sufficient, and where must unpublished
certificates live? Or is a sealed original-bundle archive explicitly required and
authorized as an exception to GnuPG-only custody? Do not implement a second store,
discard certificates, apply unpublished revocations, or alter key deletion rules
by inference. Crypto consumption and its tests can proceed independently.
