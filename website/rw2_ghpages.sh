#!/bin/bash
# Deploy out/ as the gh-pages branch (root commit, force push).
#
# fix3 G5 (BUG-0272): paths parameterized with script-relative defaults
# (OC_PUBLIC_DIR / OC_GHPAGES_OUT still override), the commit message is
# templated from the public repo's git describe instead of hardcoding a
# release-era asset list, and a PRE-DEPLOY hash gate verifies the
# exported out/downloads assets against lib/site.ts before anything is
# pushed (A18-1/A18-8: a stale-claims deployment must fail here, not on
# the live site).
#
# Layout assumption: this script lives at <public-repo>/oc-os/website/
# so SCRIPT_DIR/../.. is the public repo working copy (= oc-os/).
# Override both via environment when running from elsewhere.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PUBLIC_DIR="${OC_PUBLIC_DIR:-$(cd "$SCRIPT_DIR/../.." && pwd)}"
OUT="${OC_GHPAGES_OUT:-$PUBLIC_DIR/website/out}"
ORIGIN_URL=$(git -C "$PUBLIC_DIR" config --get remote.origin.url)
echo "ORIGIN_URL=$(echo "$ORIGIN_URL" | sed 's|x-access-token:[^@]*|x-access-token:REDACTED|')"
echo "PUBLIC_DIR=$PUBLIC_DIR"
echo "OUT=$OUT"
[ -d "$OUT" ] || { echo "ERROR: $OUT missing - run website/build-local.sh first"; exit 1; }

echo "--- pre-deploy asset hash check (out/downloads vs lib/site.ts) ---"
ISO_FILE=$(sed -n 's/^export const ISO_FILE = "\(.*\)";$/\1/p' "$SCRIPT_DIR/lib/site.ts")
ISO_SHA=$(sed -n '/^export const ISO_SHA256 = *$/{n;s/^  "\(.*\)";$/\1/p}' "$SCRIPT_DIR/lib/site.ts")
SRC_FILE=$(sed -n 's/^export const SRC_FILE = "\(.*\)";$/\1/p' "$SCRIPT_DIR/lib/site.ts")
SRC_SHA=$(sed -n '/^export const SRC_SHA256 = *$/{n;s/^  "\(.*\)";$/\1/p}' "$SCRIPT_DIR/lib/site.ts")
[ -n "$ISO_FILE" ] && [ -n "$ISO_SHA" ] && [ -n "$SRC_FILE" ] && [ -n "$SRC_SHA" ] \
  || { echo "ERROR: could not parse asset constants from lib/site.ts"; exit 1; }
check_asset() { # <file> <expected sha>
  local f="downloads/$1" want="$2" got
  if [ ! -f "$f" ]; then echo "FAIL: $f missing from the export"; exit 1; fi
  got=$(sha256sum "$f" | awk '{print $1}')
  if [ "$got" != "$want" ]; then
    echo "FAIL: $1 sha256 mismatch: lib/site.ts=$want actual=$got"; exit 1
  fi
  echo "OK: $1 $got"
}

cd "$OUT"
echo "PWD=$(pwd)"
check_asset "$ISO_FILE" "$ISO_SHA"
check_asset "$SRC_FILE" "$SRC_SHA"

# build-local.sh deletes out/ (including any previous .git), so this fresh
# init has no identity — set it explicitly or the commit is authored as
# the sandbox default (Z User <z@container>), which fails repo author checks.
git init -q -b gh-pages .
git config user.name "cubestudio-dev"
git config user.email "cubestudio@qq.com"
git add -A
# Templated commit message: derived from the public repo state at deploy
# time; override with OC_COMMIT_MSG="..." for a custom note. No era-locked
# asset names / stats baked in (the old message froze a fix2b asset list
# that every later redeploy would silently misattribute).
BUILD_ID=$(git -C "$PUBLIC_DIR" describe --tags --always 2>/dev/null || echo "build-$(date -u +%Y%m%d-%H%M%SZ)")
COMMIT_MSG="${OC_COMMIT_MSG:-website build: $BUILD_ID static export (out/downloads hash-verified against website/lib/site.ts before push)}"
git commit -q -m "$COMMIT_MSG"
git log --oneline -1
echo "--- push gh-pages (force) ---"
git push -f "$ORIGIN_URL" gh-pages 2>&1
echo "--- post-deploy hint: spot-check the live site hashes, e.g."
echo "    curl -fsSL <site>/downloads/$ISO_FILE | sha256sum   # expect $ISO_SHA"
echo "    curl -fsSL <site>/downloads/$SRC_FILE | sha256sum   # expect $SRC_SHA"
