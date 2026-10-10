#!/bin/bash
# Website local build + pre-deploy asset self-check.
#
# fix3 G5 (BUG-0272): all paths are derived from THIS script's location —
# no hardcoded /home/z/... any more, the script runs from any checkout.
# The pre-deploy hash gate: every download asset referenced by
# lib/site.ts must match the actual file in public/downloads/ before the
# build is allowed to succeed, and the exported out/downloads/ is
# re-checked after the build (A18-1/A18-8 lesson: the site once shipped
# SHA claims that no longer matched the deployed bytes).
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"
echo "PWD=$(pwd)"

command -v bun >/dev/null 2>&1 || { echo "ERROR: bun not found in PATH"; exit 1; }

echo "--- pre-deploy asset hash check (lib/site.ts vs public/downloads) ---"
# Extract the release-asset constants straight from lib/site.ts so the
# gate can never drift from what the site will actually display.
ISO_FILE=$(sed -n 's/^export const ISO_FILE = "\(.*\)";$/\1/p' lib/site.ts)
ISO_SHA=$(sed -n '/^export const ISO_SHA256 = *$/{n;s/^  "\(.*\)";$/\1/p}' lib/site.ts)
SRC_FILE=$(sed -n 's/^export const SRC_FILE = "\(.*\)";$/\1/p' lib/site.ts)
SRC_SHA=$(sed -n '/^export const SRC_SHA256 = *$/{n;s/^  "\(.*\)";$/\1/p}' lib/site.ts)
[ -n "$ISO_FILE" ] && [ -n "$ISO_SHA" ] && [ -n "$SRC_FILE" ] && [ -n "$SRC_SHA" ] \
  || { echo "ERROR: could not parse asset constants from lib/site.ts"; exit 1; }

check_asset() { # <dir> <file> <expected sha>
  local f="$1/$2" want="$3" got
  if [ ! -f "$f" ]; then echo "FAIL: $f missing (referenced by lib/site.ts)"; exit 1; fi
  got=$(sha256sum "$f" | awk '{print $1}')
  if [ "$got" != "$want" ]; then
    echo "FAIL: $2 sha256 mismatch: lib/site.ts=$want actual=$got"; exit 1
  fi
  echo "OK: $2 $got"
}
check_asset "public/downloads" "$ISO_FILE" "$ISO_SHA"
check_asset "public/downloads" "$SRC_FILE" "$SRC_SHA"
[ -f public/.nojekyll ] || echo "WARN: public/.nojekyll missing (gh-pages needs it)"

# WP-09-fix5 FIX: start from a clean out/ — a stale out/ kept the OLD
# downloads/ directory alive and gh-pages shipped stale assets while the
# new files landed at out/ root (mixed layouts). Always rebuild fresh.
rm -rf out
echo "--- bun install ---"
bun install 2>&1 | tail -3
echo "--- bun run build ---"
bun run build 2>&1 | tail -15
echo "--- out/ check ---"
ls out/ | head -8
echo "--- downloads check ---"
ls out/downloads/ | head -4
echo "--- post-build re-check (out/downloads vs lib/site.ts) ---"
check_asset "out/downloads" "$ISO_FILE" "$ISO_SHA"
check_asset "out/downloads" "$SRC_FILE" "$SRC_SHA"
echo "BUILD-OK"
