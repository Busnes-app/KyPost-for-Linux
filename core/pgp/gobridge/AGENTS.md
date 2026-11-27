# Go cryptography bridge

## Purpose

Build the Linux RFC 9580/RFC 9980 reader and X-Wing HPKE codec as a Go c-archive.

## Ownership

- `bridge.go` owns the stateless C ABI. `../GoPgpBackend.cpp` owns C++ buffer lifetime and Secret Service persistence.
- `testdata/` contains byte-identical public test secrets from KyPost-Server. See `PROVENANCE.md`.

## Local Contracts

- Inputs borrow C memory for the call only; retain no pointers or Go key handles.
- Results allocate C memory. Call `KyPgpFree(pointer, exact_length)` once for each returned buffer; it clears before freeing. C++ immediately moves secret output into `SecureBytes`.
- Bound key/ring input to 512 KiB, aggregate keys to 8 MiB, wire messages to 48 MiB and plaintext to 32 MiB. Bound decompression while reading and return no partial plaintext.
- Clear owned Go byte buffers and private parameters. Go runtime key objects and upstream library allocations cannot be guaranteed erased.
- Use gopenpgp for OpenPGP and ky-primitives/xwing for HPKE; keep their exact versions in go.mod/go.sum. Private keys never enter JSON metadata or Go strings in this bridge.
- CMake compiles the archive and generated header in the build directory. Commit source and pinned Flatpak download metadata only.
- After dependency changes, run `python3 scripts/go-flatpak-sources.py` from the repository root.

## Work Guidance

- Preserve first-signature semantics and primary-fingerprint sender binding.
- Merge immutable revisions at the read boundary, including primary/identity/subkey revocations and bindings. An older certificate must not hide a known revocation.
- Keep v4 GPGME compatibility; read capability does not authorize new v6 sending or key generation.

## Verification

- `go test -count=1 ./...`
- `go test -race -count=1 ./...`
- CTest `GoCryptoTests`, `GoPgpBackendTest` and `DeviceEnrollmentCryptoTest` on Qt 5 and Qt 6.

## Child DOX Index

None.
