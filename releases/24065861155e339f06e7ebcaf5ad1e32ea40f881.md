# KyPost source 24065861155e

Source: https://github.com/Busnes-app/KyPost-for-Linux/commit/24065861155e339f06e7ebcaf5ad1e32ea40f881

[Full release history](https://github.com/Busnes-app/KyPost-for-Linux/blob/24065861155e339f06e7ebcaf5ad1e32ea40f881/CHANGELOG.md)

### Changes

- Older mail and uncached search hits can now be decrypted or have their
  signatures checked locally. Opening them checks the existing relay payload
  endpoint without prompting for a key or importing signer keys; use Decrypt
  or Verify signature to run the existing local reader.
- Readable fallback text remains available when the check fails. A missing
  payload or older relay never counts as proof of plain mail. Late checks after
  navigation, lock, query or pairing changes are discarded; protected content
  and signature results remain transient. Clearing/replacing a search revokes
  its uncached protected reader content, while a deliberate draft handoff keeps
  its own lifetime. Pop-out readers can restart a canceled classification.

### Upgrade impact

- No database, keyring or configuration migration. Local reading needs working
  GnuPG and the existing relay PGP-payload endpoint. Older relays keep the
  readable fallback/webmail route. Classification and explicit reading each
  download the bounded payload; nothing new is stored on disk.
