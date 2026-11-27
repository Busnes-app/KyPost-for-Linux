#!/usr/bin/env bash
#
# Generate the GPG key that signs KyPost's self-hosted Flatpak repository,
# and (optionally) install it as the GitHub Actions secrets that
# .github/workflows/flatpak.yml expects.
#
# Run this ONCE. See docs/DISTRIBUTION.md for the surrounding setup.
#
#   ./packaging/flatpak/gen-signing-key.sh            # generate + print
#   ./packaging/flatpak/gen-signing-key.sh --upload   # generate + gh secret set
#
# --- Why the key is passphraseless ---
#
# CI has to sign unattended. A passphrase would either have to live in a
# second secret (protecting nothing -- an attacker with one secret has both)
# or block the runner on pinentry. The private key's only protection is
# GitHub's secret store, so treat it accordingly: it lives in exactly two
# places, GitHub Actions secrets and your offline backup.
#
# --- Why the key has a bounded expiry ---
#
# Non-expiring signing keys leave no backstop if a secret backup leaks.
# Keys default to 2 years (`KEY_EXPIRE=2y`) and can be rolled over with zero
# user disruption using OSTree dual-key signing (`FLATPAK_GPG_PRIVATE_KEY_NEXT`
# and `FLATPAK_GPG_KEY_ID_NEXT`) or extended in place; see docs/DISTRIBUTION.md.

set -euo pipefail

UPLOAD=0
[ "${1:-}" = "--upload" ] && UPLOAD=1

REPO_SLUG="${REPO_SLUG:-Busnes-app/KyPost-for-Linux}"
KEY_NAME="KyPost Flatpak Signing Key"
KEY_EMAIL="${KEY_EMAIL:-kypost-flatpak@kysecurity.com}"
KEY_EXPIRE="${KEY_EXPIRE-2y}"

case "$KEY_EXPIRE" in
  0|never|NEVER|"")
    echo "ERROR: KEY_EXPIRE must be a bounded lifetime (e.g. 2y, 18m, 365d), not '$KEY_EXPIRE'." >&2
    exit 1
    ;;
esac
if ! [[ "$KEY_EXPIRE" =~ ^[1-9][0-9]*[dwmy]$ ]]; then
  echo "ERROR: invalid KEY_EXPIRE '$KEY_EXPIRE' (expected <N>[d|w|m|y], e.g. 2y)." >&2
  exit 1
fi

OUT_DIR="${OUT_DIR:-$HOME/kypost-flatpak-signing-key}"
GNUPGHOME_TMP="$(mktemp -d)"
trap 'rm -rf "$GNUPGHOME_TMP"' EXIT
export GNUPGHOME="$GNUPGHOME_TMP"
chmod 700 "$GNUPGHOME"

if [ -e "$OUT_DIR" ]; then
  echo "ERROR: $OUT_DIR already exists." >&2
  echo "A signing key may already have been generated. Refusing to overwrite:" >&2
  echo "losing the old private key means every existing install must re-add the remote." >&2
  exit 1
fi

echo "Generating a 4096-bit RSA signing key (no passphrase, expires in $KEY_EXPIRE)..."
gpg --batch --generate-key <<EOF
%no-protection
Key-Type: RSA
Key-Length: 4096
Key-Usage: sign
Name-Real: $KEY_NAME
Name-Email: $KEY_EMAIL
Expire-Date: $KEY_EXPIRE
%commit
EOF

KEY_ID="$(gpg --list-secret-keys --with-colons "$KEY_EMAIL" | awk -F: '/^fpr:/ {print $10; exit}')"
if [ -z "$KEY_ID" ]; then
  echo "ERROR: could not determine the generated key's fingerprint." >&2
  exit 1
fi
KEY_EXP_EPOCH="$(gpg --list-keys --with-colons --fixed-list-mode "$KEY_ID" | awk -F: '$1=="pub" {print $7; exit}')"
KEY_EXP_DATE="$(date -u -d "@$KEY_EXP_EPOCH" +%Y-%m-%d 2>/dev/null || echo "$KEY_EXP_EPOCH")"

mkdir -p "$OUT_DIR"
chmod 700 "$OUT_DIR"
gpg --armor --export-secret-keys "$KEY_ID" > "$OUT_DIR/private.asc"
gpg --armor --export "$KEY_ID" > "$OUT_DIR/public.asc"
printf '%s\n' "$KEY_ID" > "$OUT_DIR/fingerprint.txt"
printf '%s\n' "$KEY_EXP_DATE" > "$OUT_DIR/expires.txt"
chmod 600 "$OUT_DIR"/*

echo
echo "Key fingerprint: $KEY_ID"
echo "Expires (UTC):   $KEY_EXP_DATE"
echo "Written to:      $OUT_DIR"
echo

if [ "$UPLOAD" -eq 1 ]; then
  command -v gh >/dev/null || { echo "ERROR: gh CLI not found." >&2; exit 1; }
  echo "Setting GitHub Actions secrets on $REPO_SLUG..."
  gh secret set FLATPAK_GPG_PRIVATE_KEY --repo "$REPO_SLUG" < "$OUT_DIR/private.asc"
  printf '%s' "$KEY_ID" | gh secret set FLATPAK_GPG_KEY_ID --repo "$REPO_SLUG"
  echo "Done. Secrets FLATPAK_GPG_PRIVATE_KEY and FLATPAK_GPG_KEY_ID are set."
else
  echo "To install the secrets (initial setup):"
  echo
  echo "  gh secret set FLATPAK_GPG_PRIVATE_KEY --repo $REPO_SLUG < $OUT_DIR/private.asc"
  echo "  printf '%s' '$KEY_ID' | gh secret set FLATPAK_GPG_KEY_ID --repo $REPO_SLUG"
  echo
  echo "Or to stage a dual-key rollover (see docs/DISTRIBUTION.md):"
  echo
  echo "  gh secret set FLATPAK_GPG_PRIVATE_KEY_NEXT --repo $REPO_SLUG < $OUT_DIR/private.asc"
  echo "  printf '%s' '$KEY_ID' | gh secret set FLATPAK_GPG_KEY_ID_NEXT --repo $REPO_SLUG"
fi

cat <<EOF

>>> BACK UP $OUT_DIR/private.asc SOMEWHERE OFFLINE, NOW. <<<

If this key is lost, you cannot sign updates for the existing remote. Every
user would have to remove and re-add it by hand to keep receiving updates.
GitHub Actions secrets are write-only -- you cannot read the key back out.
Start the rollover runbook in docs/DISTRIBUTION.md at least 90 days before $KEY_EXP_DATE.
EOF
