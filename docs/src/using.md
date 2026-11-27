# Everyday tasks

## Mail and contacts

Browse folders and keyword tabs, compose mail, add attachments, and use reply and forward actions. Contacts can be edited offline and their changes queued for sync. Verify the result on the server when reconnecting.

## OpenPGP

Classical keys use your GnuPG keyring and `gpg-agent`; hardware tokens retain their usual behavior. OpenPGP v6 and post-quantum keys use binary entries in the OS secret store. Keep both keyrings when updating or moving the profile. Enrollment shows a code to compare in webmail and only succeeds after the whole keyring is saved and read back. Locally encrypted sends require usable keys for all recipients and do not silently downgrade to plaintext. Preserve retired keys needed for older messages.

## Notifications

A UnifiedPush distributor delivers notifications when configured. Otherwise the app falls back to polling. Lack of push does not by itself mean mail synchronization is broken.

## Local protection

Security settings offer a PIN lock, background-lock grace period and erase threshold. The local database uses SQLCipher. Hostile Location Protection removes persistent state before switching to memory-only behavior; arrange recovery before enabling it.
