# Verifying releases

[Product overview](../README.md) · [Distribution](DISTRIBUTION.md) · [Security](../SECURITY.md) · [Assurance case](ASSURANCE-CASE.md)

KyPost for Linux ships as a Flatpak in two forms, both signed with the same GPG key:

- the **remote** at `https://busnes-app.github.io/KyPost-for-Linux/`, which `flatpak update`
  follows; and
- the **bundles** `kypost-x86_64.flatpak` and `kypost-aarch64.flatpak` attached to each
  GitHub release.

Every command on this page was run on 2026-11-03 against the live remote and the `v0.6.0`
release, with Flatpak 1.18.4, in an empty directory with an empty Flatpak installation
(`FLATPAK_USER_DIR` set to a new directory). The outputs quoted are what they printed.
The commands use `--user`; drop it to use the system installation.

## The signing key

`.github/workflows/flatpak.yml` signs each OSTree commit and the repository summary in its
`publish` job (`flatpak build-commit-from --gpg-sign`, `flatpak build-update-repo
--gpg-sign`), and signs release bundles there with `flatpak build-bundle --gpg-sign`. The
`build` job, which also runs for pull requests, exports unsigned repositories and never
receives the key. The key:

```
pub   rsa4096 2026-08-24 [SC]
      14FE 17F0 6FEF 936C 8C4D  EC3A E94F C98D 33F6 67AB
uid                      KyPost Flatpak Signing Key <kypost-flatpak@kysecurity.com>
```

It has no expiry date. The same fingerprint is in [DISTRIBUTION.md](DISTRIBUTION.md),
recorded when the key was made. `kypost.flatpakrepo` carries the public key, and
`flatpak remote-add` trusts whatever key it finds there, so check it first:

```bash
curl -sSfO https://busnes-app.github.io/KyPost-for-Linux/kypost.flatpakrepo
sed -n 's/^GPGKey=//p' kypost.flatpakrepo | base64 -d | gpg --show-keys --with-fingerprint
```

The second command prints the key block above. The fingerprint must be the one above. This
page and the `.flatpakrepo` are served from the same GitHub account, so read the
fingerprint from a copy of this repository you trust, not only from the website.

## The remote

```bash
flatpak --user remote-add --if-not-exists kypost https://busnes-app.github.io/KyPost-for-Linux/kypost.flatpakrepo
flatpak --user remote-info kypost com.kysecurity.mail
```

`remote-info` succeeds only if the summary signature checks out against the key. It
printed, in part:

```
            ID: com.kysecurity.mail
           Ref: app/com.kysecurity.mail/x86_64/master
       Runtime: org.kde.Platform/x86_64/6.11
        Commit: 096669968df8ea83054d278675acb75c3b94c7e09b79257d21f1e9bbc9a19bcf
       Subject: Export com.kysecurity.mail
          Date: 2026-11-03 12:53:04 +0000
```

It prints no version. Flatpak checks each commit's signature again on `install` and
`update`. Adding the same repository with any other key (here with `--gpg-import` of a
throwaway key) fails. `remote-add` exits 1:

```
error: Signature made Tue 03 Nov 2026 07:58:00 AM EST using RSA key ID E94FC98D33F667AB
Can't check signature: public key not found
```

and `remote-info` on that remote exits 1:

```
error: Unable to load summary from remote kypost-wrong: Signature made Tue 03 Nov 2026 07:58:00 AM EST using RSA key ID E94FC98D33F667AB
Can't check signature: public key not found
```

`remote-add` still writes the remote to the configuration when it fails, so remove it
with `flatpak --user remote-delete` if you were testing.

**The remote follows `main`, not releases.** `flatpak.yml` publishes on every push to
`main`, nightly, on manual runs and on `v*` tags. On 2026-11-03 the remote served `main`
commit `a922968`, two commits after the `v0.6.0` tag (`1b7db4e`). The commit subject names
no source revision. To see which source commit the current remote build came from:

```bash
gh api repos/Busnes-app/KyPost-for-Linux/commits/gh-pages --jq .commit.message
```

It printed `Publish a922968ae080b7a6857945e3bf7240d0fcf75934`. That only names the latest
build: `gh-pages` holds a single commit, which each publish replaces.

## A release bundle

```bash
v=0.6.0
gh release download v$v -R Busnes-app/KyPost-for-Linux -p kypost-x86_64.flatpak
gh release view v$v -R Busnes-app/KyPost-for-Linux --json assets --jq '.assets[]|"\(.digest)  \(.name)"'
sha256sum kypost-x86_64.flatpak
sed -n 's/^GPGKey=//p' kypost.flatpakrepo | base64 -d > kypost.gpg
flatpak --user install --bundle --gpg-file=kypost.gpg kypost-x86_64.flatpak
flatpak --user info com.kysecurity.mail
```

For `v0.6.0`, `sha256sum` printed
`6193009a0a89dae544e59859c306e695c5e251f00e288dc55cdb97b3fbec5823`, the same as GitHub's
`digest` for `kypost-x86_64.flatpak` (`kypost-aarch64.flatpak`:
`7c8e7e7dc4fd0f046b5dcdbf511e6343221f916588604c6af63c55029af91980`, also matching). The
install exited 0 and `flatpak info` reported `Version: 0.6.0` and commit
`9d6a5f1e3ff1ef878aa8d238c6c55fd8d34efb756e82bc3eb1f91a59c59a6222`. The aarch64 bundle
also installed with the KyPost key and was refused with another key. These installs, and
the ones below, were run with `--no-deps`, so the runtime was not fetched. The app was
never started.

In an installation that does not already trust the KyPost key, another key in
`--gpg-file` stops the install with exit 1:

```
Error: Failed to install bundle com.kysecurity.mail: GPG signatures found, but none are in trusted keyring
```

In an installation that already has the `kypost` remote, the bundle installs with any
`--gpg-file`, because its signature checks against the key that remote added.

**Pass `--gpg-file`.** The bundle is signed but does not carry the key or the repository
URL (`build-bundle` runs without `--gpg-keys` or `--repo-url`). Without `--gpg-file`, the
install also exits 0, and the signature is not checked against the release key. The
installed app's origin remote has an empty `url=`, so it never updates
([DISTRIBUTION.md](DISTRIBUTION.md), "Known gap").

## What this project cannot offer

- **A link from a package to its source that lasts.** No build-provenance attestation is
  made. The Actions log that shows what a run built is kept for 90 days (the repository's
  retention setting), and `gh-pages` names only the latest build.
- **A remote that serves only releases.** Anyone following the remote runs `main`.
- **A rebuild to the same bytes.** The Flatpak build has not been checked for
  reproducibility, and CI does not check it.
