# Building KyPost

[Product overview](../README.md)

Commands and unlinked file paths below are relative to the repository root.

## Building

The app needs Qt6 and KF6. Ubuntu Touch will get a separate Qt 5.15 app that shares `core/`, so
`core/` also builds on Qt 5.15 (see [Core only, on Qt 5.15](#core-only-on-qt-515)). Use one
out-of-tree build directory:

```sh
cmake -B build -S . -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build
```

Dependencies, with Arch package names: `qt6-base`, `qt6-declarative`, `qt6-svg`, `qt6-webengine`,
`qt6-multimedia`, `kirigami` (KF6), `knotifications` (KF6), `kstatusnotifieritem` (KF6),
`kdbusaddons` (KF6), `ki18n` (KF6), `qtkeychain-qt6`, `kunifiedpush`, `zxing-cpp`, `openssl`,
`argon2`, `gpgme`. For the Ubuntu and KDE neon archive equivalents, see
[`.github/workflows/ci.yml`](../.github/workflows/ci.yml).

`openssl` supplies AES-256-GCM and `argon2` the memory-hard key derivation behind the
credential seal — Qt exposes neither. See `core/security/CredentialCipher.h`. `gpgme` is the C
API, not the `gpgmepp` C++ wrapper, and it is what delegates OpenPGP custody to the user's
`gpg-agent`.

### Encryption at rest needs SQLCipher

The command above builds a working client whose database is **not** encrypted — configure
reports `SQLCipher: not configured (KYPOST_SQLCIPHER_ROOT unset) -- encrypted databases
unavailable`. SQLCipher has to carry the SONAME `libsqlite3.so.0` and be built with
`SQLITE_ENABLE_COLUMN_METADATA`, which distro packages generally do not do, so this repo builds
it — the same way CI and the Flatpak manifest do:

```sh
./scripts/build-sqlcipher.sh "$HOME/.local/opt/kypost-sqlcipher"
cmake -B build -S . -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DKYPOST_SQLCIPHER_ROOT="$HOME/.local/opt/kypost-sqlcipher"
cmake --build build
ctest --test-dir build --output-on-failure
```

The first build recipe is for evaluation without database encryption. Encryption tests may
skip there: a green `ctest` result with skips is not encryption coverage. Use the SQLCipher
build above for an encrypted profile and verify that configure reports SQLCipher enabled.
`AGENTS.md` §6h explains the SONAME/metadata requirements. Fake-relay tests and demo
screenshots do not qualify live mail, push or physical-device PGP interoperability.

### Core only, on Qt 5.15

`-DKYPOST_CORE_ONLY=ON` builds only `core/` and its tests, with no KF6 and no app. This is the
build the CI `core-qt5` job runs. It needs `qt5-base` (Ubuntu: `qtbase5-dev`, `libqt5sql5-sqlite`)
and the same SQLCipher as above. `-DQT_DIR` selects Qt 5 when Qt 6 is also installed. Configure
prints the version it used, for example `kypostcore: Qt 5.15.13`:

```sh
./scripts/build-sqlcipher.sh "$HOME/.local/opt/kypost-sqlcipher"
cmake -B build-qt5 -S . -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DKYPOST_CORE_ONLY=ON \
  -DQT_DIR=/usr/lib/cmake/Qt5 -DKYPOST_SQLCIPHER_ROOT="$HOME/.local/opt/kypost-sqlcipher"
cmake --build build-qt5
ctest --test-dir build-qt5 --output-on-failure
```

Without `KYPOST_CORE_ONLY` the build is always Qt 6, whatever `QT_DIR` says.

### Flatpak

```sh
flatpak-builder --user --force-clean --install-deps-from=flathub \
  build-flatpak packaging/flatpak/com.kysecurity.mail.yaml
flatpak-builder --run build-flatpak packaging/flatpak/com.kysecurity.mail.yaml kypost
```

This manifest is the packaging target for Linux Desktop and for Plasma Mobile. The Click and
Ubuntu Touch directory (`packaging/click/`) is a deliberate empty placeholder. It stays empty
until the Ubuntu Touch app exists.

CI builds this manifest for every pull request. Publishing a signed OSTree repository
to `gh-pages` on `main` is conditional on the configured signing/publishing secrets;
without them the build/smoke checks run and publication is skipped. See [the publishing workflow](../.github/workflows/flatpak.yml). The `flatpak remote-add`
command [above](../README.md#installing) uses that published repository.
