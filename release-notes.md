# KyPost source 38c0f317065e

Source: https://github.com/Busnes-app/KyPost-for-Linux/commit/38c0f317065ebb899b0b77f52aca20c6073fa85d

[Full release history](https://github.com/Busnes-app/KyPost-for-Linux/blob/38c0f317065ebb899b0b77f52aca20c6073fa85d/CHANGELOG.md)

### Changes

- Flatpak release signing keys now default to a bounded 2-year expiry (`KEY_EXPIRE=2y` in `packaging/flatpak/gen-signing-key.sh`), the publish workflow checks signing-key expiration and supports zero-downtime OSTree dual-key rollover (`FLATPAK_GPG_*_NEXT` + `--gpg-import`), and `docs/DISTRIBUTION.md` documents the rollover runbook.
- Read OpenPGP v6 and RFC 9980 post-quantum mail and composite signatures through a source-built gopenpgp bridge. Enrollment uses X-Wing HPKE and SAS v2, reads v1/v2 rings, and stores modern keys in the OS Secret Service. QR checks accept complete v4 and v6 fingerprints.
- Keep classical v4 signing and recipient encryption available after enrolling a certificate with PQC subkeys; retain the full certificate for PQC reads. Converted enrollment requires the complete authenticated ring before acknowledgement. Flatpak builds keep the Go module cache writable for cleanup.
- Enrollment state reports now repeat the device's OpenPGP read capabilities, so acknowledging a delivery no longer resets the device to v4-only on the server. ML-DSA-87+Ed448 signatures no longer count as strong, matching Spec A's category-3 allowlist.
- Documentation cleanup: added top-level heading and refreshed links in `README.md`, updated the manual and architecture guides, and removed stale internal planning and rename notes.
- When the KyPost server holds an account's mail until email encryption setup is finished on the web (KyPost-Server #666), the mail section shows "Finish encryption setup" with "Open KyPost on the web" and "Check again" instead of a refresh error. Mail sync pauses rather than retrying, and resumes after a successful check or when the app returns to the foreground. The pairing, keys, cached mail and drafts are kept.

### Upgrade impact

- A device enrolled with the previous build is recorded as v4-only on the server until it enrolls again; the server keeps a user on v4 while any device is v4-only.
- Source builds now require Go 1.27+ and CGO. Preserve both the GnuPG keyring and the OS Secret Service entries; modern keys are not stored in the mail database. No account keys are converted automatically. V6 generation/sending remains a later phase.
- Rebuild or install the updated Flatpak. No data, account, key or configuration migration is required. Older servers never send this answer, so nothing changes against them.
