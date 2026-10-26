# Distribution: the self-hosted Flatpak channel

KyPost is distributed from its own signed Flatpak remote, published to this
repository's `gh-pages` branch by `.github/workflows/flatpak.yml`.

> **Status, 2026-08-24: live.** The one-time setup below is done — the signing
> key is configured, `gh-pages` exists, and Pages serves it. v0.2.0 is published
> for x86_64 and aarch64, signed by `14FE17F06FEF936C8C4DEC3AE94FC98D33F667AB`.
> Verified against the live URL, not inferred: the key embedded in the served
> `.flatpakrepo` matches that fingerprint, `flatpak install` from the remote
> works, `ostree show` reports a good signature on the installed commit, and a
> client trusting a different key is refused with "public key not found".
>
> **Known gap: the `.flatpak` bundles attached to a release do not update.**
> `build-bundle` is called without `--repo-url`, so a bundle install creates its
> origin remote with an empty `url=` and no GPG key — verified by installing the
> released x86_64 asset. It installs and runs, but `flatpak update` can never
> reach it and the signature it carries is not checked against the release key.
> Tell users to add the remote; adding `--repo-url` and `--gpg-keys` to the
> bundle step would close it.

KyPost’s chosen application distribution channel is its own signed remote. Flathub
supplies the KDE runtime; KyPost itself is not listed there. This is the project’s
packaging choice, not evidence of a general Flathub policy banning AI-backed applications.

**Availability recheck, 2026-10-20:** release `v0.4.2` and `gh-pages` exist and the
public `.flatpakrepo` responds HTTP 200. The dated signature/install observations above
remain evidence of their original run; this recheck did not reinstall or validate the
current release’s signer on a fresh machine.

Because there is no store, this repo *is* the distribution channel. That's why the
workflow publishes an OSTree repository rather than only a single-file bundle: a
`.flatpak` bundle installs once and then never updates, while a remote gives users a
working `flatpak update`.

---

## For users

```sh
flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
flatpak remote-add --if-not-exists kypost https://busnes-app.github.io/KyPost-for-Linux/kypost.flatpakrepo
flatpak install kypost com.kysecurity.mail
```

Updates are then just `flatpak update`. The Flathub remote is required for the
`org.kde.Platform//6.11` runtime; the app itself is not on Flathub.

The repo is GPG-signed and the public key is embedded in the `.flatpakrepo`, so
`flatpak` refuses anything not signed by the release key.

---

## One-time maintainer setup

Three steps, in this order. The workflow builds and smoke-tests without any of them —
it just skips publishing and logs a warning — so a missing key never breaks CI.

### 1. Generate the signing key

```sh
./packaging/flatpak/gen-signing-key.sh --upload
```

This creates a passphraseless, non-expiring RSA-4096 signing key, writes it to
`~/kypost-flatpak-signing-key/`, and sets the `FLATPAK_GPG_PRIVATE_KEY` and
`FLATPAK_GPG_KEY_ID` repository secrets via `gh`. Omit `--upload` to print the
commands instead of running them.

**Back up `private.asc` offline immediately.** GitHub Actions secrets are write-only —
you cannot read the key back out. Losing it means you cannot sign updates for the
existing remote, and every installed user would have to remove and re-add it by hand.

The key is passphraseless because CI signs unattended; a passphrase stored as a second
secret protects nothing, since an attacker with one secret has both. It never expires
because an expired signing key doesn't just block new releases — it makes
`flatpak update` reject the repo for everyone already installed.

### 2. Run the workflow once

Push to `main`, or trigger it manually:

```sh
gh workflow run flatpak.yml
```

This creates the `gh-pages` branch. It must exist before step 3.

### 3. Enable GitHub Pages

Settings → Pages → Source: *Deploy from a branch* → `gh-pages` / `/ (root)`. Or:

```sh
gh api -X POST repos/Busnes-app/KyPost-for-Linux/pages \
  -f 'source[branch]=gh-pages' -f 'source[path]=/'
```

Then verify:

```sh
curl -fsS https://busnes-app.github.io/KyPost-for-Linux/kypost.flatpakrepo
flatpak remote-add --user --if-not-exists kypost-test \
  https://busnes-app.github.io/KyPost-for-Linux/kypost.flatpakrepo
flatpak install --user kypost-test com.kysecurity.mail
```

---

## How the workflow works

Two jobs. `build` is a matrix over both shipped architectures, each on its own native
runner; `publish` runs once, after both, and is the only thing that touches gh-pages.

| Job | Trigger | Builds | Smoke-tests | Publishes | Release asset |
|---|---|---|---|---|---|
| `build` | every run | both arches | yes | no | no |
| `publish` | `pull_request` | — | — | skipped entirely | no |
| `publish` | `push` to `main` | — | — | yes | no |
| `publish` | `push` tag `v*` | — | — | yes | yes |
| `publish` | nightly / manual | — | — | yes | no |

Every run uploads one `kypost-<arch>.flatpak` bundle per architecture as an artifact.
Tagged runs attach both to the GitHub Release, for people who'd rather not add a remote.

[Release notes](../CHANGELOG.md) cover named versions and published source builds.
Every future publication prepares human notes before signing/publishing: tag runs
select their exact version section; main/nightly builds select Current source.
The site serves `release-notes.md` and retains `releases/<source-sha>.md`; the
tagged GitHub Release uses the generated notes as its body. The notes identify
the source and upgrade impact; package signature verification remains separate.

### Architectures

`x86_64` and `aarch64`. Plasma Mobile is a target, so arm64 is not optional.

Each builds natively — `ubuntu-24.04` and `ubuntu-24.04-arm`, both free for public
repos. No qemu: emulating a QtWebEngine build would take hours and would make the
launch smoke test meaningless, since it would no longer be running the code a user
runs.

Users do not choose an arch. `flatpak install` resolves it from the one repo, which
carries both.

Native `ci.yml` currently runs on x86_64 Ubuntu 26.04, using its stock Qt6/KF6
packages and a separate Qt 5.15 shared-core job. Its former KDE neon dependency
layer is gone. Native arm64 expansion still needs the dependency closure verified;
the runner comment records the earlier failures. This Flatpak workflow already
builds arm64 against `org.kde.Sdk` from Flathub, with `run-tests: true`, so the
whole ctest suite and launch smoke test run on real arm64 hardware.

### How two build jobs become one published repo

Two matrix jobs cannot both force-push `gh-pages` — the workflow-level `concurrency`
group serialises whole runs, not jobs inside one. So the matrix only *builds*, each arch
exporting to its own unsigned OSTree repo that it hands over as an artifact, and the
single `publish` job merges both in with `flatpak build-commit-from` before pushing once.

That merge is also where signing happens, which means **the signing key is never
present in a build job**. It has to be `build-commit-from` rather than
`ostree pull-local`: pull-local copies commits verbatim, so unsigned build output would
stay unsigned, and a client configured with `GPGKey=` verifies the *commit*, not just
the summary — it would refuse to install.

### Why a `gh-pages` branch instead of `actions/deploy-pages`

An OSTree repo must **accumulate**. `flatpak update` computes a delta from the commit
the user already has, so older commits must stay reachable. The artifact-based Pages
deploy replaces the site wholesale each run, which would strand every existing install
on a repo whose history vanished.

So each run clones the currently-published repo, exports the new build *into* it, and
force-pushes the result back as a single **orphan** commit. OSTree keeps its own
history inside the repo (bounded by `--prune-depth=3`), while git history stays one
commit deep so the branch doesn't grow without bound as pruned objects accumulate.

### The empty-directory trap

git cannot store an empty directory, and a fresh OSTree repo has six of them
(`extensions/`, `state/`, `tmp/`, `tmp/cache/`, `refs/mirrors/`, `refs/remotes/`).
Restoring by `git clone` silently drops them, and the *next* publish then dies with:

```
error: Listing refs: opendir(refs/remotes): No such file or directory
```

That failure mode is nasty because the first publish succeeds — it's the first
*update* that breaks. The restore step recreates the directories and runs
`ostree fsck` before building. Verified locally: without the fix the second publish
fails, with it the second publish succeeds and generates a proper v1→v2 static delta.

### Size

GitHub Pages caps a published site at **1 GB**. The runtime is not in this repo (users
get `org.kde.Platform//6.11` from Flathub); it holds only KyPost plus its five
from-source modules (libargon2, qtkeychain, sqlcipher, zxing-cpp, kunifiedpush) — for two
architectures, but without debuginfo (see below). `--prune-depth=3` bounds growth to the three most recent
commits per ref. The publish step logs the site size
on every run — if it approaches the cap, lower `--prune-depth`, or move the repo to
object storage and change `Url=` in the generated `.flatpakrepo`.

### Debuginfo is not published

The merge step drops every `runtime/*.Debug` ref. Debuginfo was the largest thing the
repo carried, and going dual-arch would have meant two copies of it; dropping it is
what keeps two architectures from costing much more than one did.

It is cut at the merge, not with flatpak-builder's `--disable-debuginfo`. Separating
debug symbols out into that ref is *how* flatpak-builder produces stripped binaries, so
turning the mechanism off risks shipping fat ones instead. Filtering afterwards cannot
affect the app ref at all.

Safe for users: the extension is declared `no-autodownload=true` and `autodelete=true`
(verified against the real installed `com.kysecurity.mail` metadata), so no install or
update ever pulls it and its absence cannot break either. Anyone who explicitly
installed `com.kysecurity.mail.Debug` from the remote will simply stop receiving updates
for it. `.Locale` is unaffected — translations still publish.

The first publish after this change also deletes the `.Debug` refs already on gh-pages.
Skipping the new ones only stops the repo growing; the old refs would otherwise keep
their objects alive against `--prune` indefinitely. That deletion is a no-op on every
run after the first. Measured on a synthetic repo: 20M → 272K, fsck clean.

Getting debuginfo back for a build means building the manifest locally
(`packaging/flatpak/build.sh`), which still produces it.

---

## Cutting a release

Three things carry the version and all three must agree, or the build fails before
it publishes anything — `scripts/verify-version.sh` runs first in both workflows.

1. Bump `project(KyPost VERSION ...)` in `CMakeLists.txt`. This is the source of
   truth; it reaches C++ as `KYPOST_VERSION` and ends up in the User-Agent header.
2. Add a matching `<release>` entry, newest first, to
   `packaging/flatpak/com.kysecurity.mail.metainfo.xml`. This decides what Discover
   shows and whether a user is offered the update at all. `appstreamcli validate`
   runs in CI and is blocking.
3. Update `CHANGELOG.md`'s Current source section and add an exact
   `## v<version>` section, each with Changes and Upgrade impact. Include relay,
   schema, keyring and packaging compatibility, migration steps and any known
   advisory IDs. Archive the previous shipped source notes under its known
   commit. Every main update needs fresh source notes, including workflow/docs
   changes; the rolling channel distributes those commits too.
4. Tag and push after review (for example, when preparing version 0.4.4):
   ```sh
   git tag -a v0.4.4 -m "KyPost 0.4.4" && git push origin v0.4.4
   ```
   The tag must be `v<version>`, or `v<version>-<prerelease>` for an rc.

The workflow then publishes both architectures to the remote and attaches
`kypost-x86_64.flatpak`, `kypost-aarch64.flatpak` and the named notes to the
release. It validates notes before importing signing material or changing
gh-pages. Existing release bodies, tags and assets are not rewritten by adding
the retrospective archive.

Check locally before tagging:

```sh
./scripts/verify-version.sh v0.4.4
python3 -B -m unittest discover -s scripts -p test_release_notes.py
python3 scripts/release-notes.py --base origin/main --tag v0.4.4
```

Fetch `origin/main` before that comparison. Release verification needs Python 3;
the desktop runtime does not. CI checks the tested commit against its first parent,
using checkout depth two. It refuses missing/invalid bases and empty, comment/link-only
or unchanged source notes; a parent without the file permits initial adoption.
Named publication output must match the tag's actual commit. The check establishes
presence/freshness and source binding, not prose accuracy or completeness; reviewers
must still verify the summaries against the code.

## Rotating the signing key

Avoid this if at all possible — it is a breaking change. Every user must run
`flatpak remote-delete kypost` and re-add the remote, because `flatpak` will reject a
repo signed by an unknown key. If unavoidable, announce it, then regenerate the key
(delete `~/kypost-flatpak-signing-key/` first, since the script refuses to overwrite)
and re-run the workflow.
