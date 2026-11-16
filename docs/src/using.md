# Everyday tasks

## Mail and contacts

Browse folders and keyword tabs, compose mail, add attachments and use reply/forward actions. Contacts can be edited offline and their changes queued for sync. Verify the result on the server when reconnecting.

## OpenPGP

Enrolled keys live in your own GnuPG keyring and use `gpg-agent`; hardware tokens retain their usual behavior. Locally encrypted sends require usable keys for all recipients and do not silently downgrade to plaintext. Preserve retired keys needed for older messages.

## Notifications

A UnifiedPush distributor delivers notifications when configured. Otherwise the app falls back to polling. Lack of push does not by itself mean mail synchronization is broken.

## Local protection

Security settings offer a PIN lock, background-lock grace period and erase threshold. The local database uses SQLCipher. Hostile Location Protection removes persistent state before switching to memory-only behavior; arrange recovery before enabling it.
