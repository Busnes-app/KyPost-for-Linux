# Troubleshooting

- **Direct IMAP settings cannot be found:** configure the KyPost relay server; the client deliberately has no direct IMAP/SMTP path.
- **Pairing fails:** request a fresh link, verify the HTTPS origin and investigate certificate warnings.
- **"Finish encryption setup" instead of mail:** the server holds your mail until email encryption setup is finished. Choose **Open KyPost on the web**, finish setup there, then return to the app or choose **Check again**. This device stays paired and nothing local is deleted.
- **Encrypted send refused:** check every recipient's usable key and trust state; do not remove encryption just to dismiss an error.
- **Old encrypted mail cannot open:** check that the required retired key is still in GnuPG or, for modern keys, in the OS secret store. Unlock the relevant keyring and retry.
- **No notifications:** inspect UnifiedPush or wait for the polling fallback, then check actual synchronization.
- **Keyring waits on startup:** unlock the session keyring and inspect Secret Service availability.

When reporting a problem, include the product version, platform, failed task and a redacted error. Exclude pairing links, device secrets, recovery codes, custodian shares and private content.
