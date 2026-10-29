# KyPost features

[Product overview](../README.md)

Commands and unlinked file paths below are relative to the repository root.

## Features

- **Inbox, message detail, and an HTML composer** — reply, reply-all, forward, and send, with
  attachments and drafts. A sandboxed `WebEngineView` renders each email body with JavaScript
  and remote images disabled.
- **Server-side folders** — the standard mailboxes and any subfolder. The client can create,
  rename, and delete these folders.
- **Contacts** — synced list and detail views. The client creates and edits contacts offline
  and queues the changes. It also handles group membership and duplicate entries.
- **PGP support** — the client exchanges public keys through QR codes. Scan or show a key with
  the camera. The code carries the fingerprint of the key this device holds, and a scan saves a
  key as verified only if it matches; a device holding no private key shows a code that cannot be
  verified in person. Webmail can seal the account key to a
  temporary P-256 device key as an RFC 9180 HPKE envelope (v4/v5); KyPost refuses the
  retired v2/v3 formats, so enrollment needs a relay that offers v4/v5. KyPost
  imports it into the user's GnuPG keyring and clears the
  transient key bytes. Once a key is enrolled, end-to-end encrypted mail is **decrypted and
  signed on this device** through the user's own `gpg-agent` (GPGME): KyPost never sees the
  OpenPGP passphrase, so hardware tokens and smartcards work unchanged. Sending signs and
  encrypts locally, gives each blind recipient their own ciphertext, keeps the real subject
  inside the ciphertext, and refuses to send at all if any recipient has no usable key — there
  is no silent downgrade to plaintext. Mail this device holds no key for is still explained and
  routed to webmail.
  New signing/encryption requires RSA primary keys and usable RSA subkeys for
  that operation to contain at least 2048 bits. If an older key blocks a new
  message or encrypted draft, arrange a replacement key through your existing
  account/key-management process and verify its fingerprint with the other
  party. Keep the old GnuPG keys: historical mail remains decryptable. Do not
  delete your keyring, reset the profile or send the protected content in
  plaintext to work around the refusal. Incoming signature verification and
  TLS have separate policies; this RSA guard makes no project-wide claim about them.
- **Compose autocomplete** — type a name or an address in Compose, then select a synced
  contact.
- **Push notifications over [UnifiedPush](https://unifiedpush.org/)** — two tiers with
  fallback: the system distributor, then 90-second polling. Mail still arrives when no
  UnifiedPush distributor is installed. Only the push server of your own distributor sees a
  notification.
- **Security** — an optional PIN lock with a lockout, a configurable background-lock grace
  period, and a configurable erase-after-N-failures threshold (including "never", which
  declines only the erase — the rate limit always stays). You can encrypt the pairing
  credential behind that PIN. The local database is **encrypted at rest** with SQLCipher; an
  existing plaintext profile is converted on the next launch. The client pins the relay's TLS
  chain on first use, anchored to the issuer rather than the leaf so a routine certificate
  renewal is not reported as an impersonation. Hostile Location Protection writes nothing to
  disk, and refuses to turn on at all if it cannot first erase what is already there.
- **17 themes plus Follow system** — copied byte for byte from the Android app. The default
  follows your desktop between Busnes Light and Busnes Dark; text follows your system font size.
- **Device pairing** — paste a link, or use a `kypost://` deep link.
- **Localized** — the code wraps every user-facing string for translation (`po/`).
