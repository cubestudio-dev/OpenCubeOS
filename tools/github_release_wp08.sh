#!/usr/bin/env bash
# Open Cube OS — WP-08 GitHub Release helper
#
# Usage:
#   GITHUB_TOKEN=ghp_xxxxxxxxxxxxxxxxxxx bash tools/github_release_wp08.sh
#
# Prerequisites:
#   - Your GitHub PAT (Personal Access Token) with repo + workflow scopes
#   - Local commit `da51f46` (WP-08 rename) already created
#   - Local tag `WP-08` already created
#   - ISO at /home/z/my-project/public/downloads/opencube-wp08.iso
#
# What this script does:
#   1. Push commit da51f46 to origin/main
#   2. Push tag WP-08 to origin
#   3. Create GitHub Release (tag=WP-08, name=WP-08, only ISO asset)
#   4. Print the Release URL

set -e
OC_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$OC_ROOT"

# ---- 0. Validate PAT ----
if [ -z "$GITHUB_TOKEN" ]; then
  echo "ERROR: GITHUB_TOKEN env var not set."
  echo "       Get a PAT from https://github.com/settings/tokens (repo scope)."
  echo "       Then: GITHUB_TOKEN=ghp_xxx bash tools/github_release_wp08.sh"
  exit 1
fi

REPO='cubestudio-dev/OpenCubeOS'
ISO_LOCAL='/home/z/my-project/public/downloads/opencube-wp08.iso'
TAG='WP-08'

# ---- 1. Push commit + tag to origin ----
echo "=== 1. Push commit + tag to origin ==="
git push "https://x-access-token:${GITHUB_TOKEN}@github.com/${REPO}.git" main
git push "https://x-access-token:${GITHUB_TOKEN}@github.com/${REPO}.git" "$TAG"

# ---- 2. Create GitHub Release via REST API ----
echo ""
echo "=== 2. Create GitHub Release (tag=WP-08, name=WP-08) ==="
RELEASE_BODY='WP-08 — Complete release

## What is in WP-08
- Boot + interrupt + memory + scheduler + shell + FS + net + disk
- Complete syscall set (37 syscalls)
- Dynamic linking (ld.so, 5 relocation types, dlopen/dlsym/dlclose, ldd)
- User-space shell (ush) + 20 built-in tools + Tab completion + jobs + signals
- 7 L1 extension interfaces (items 51-57), total 57 L1 surface
- 47 audit bugs all fixed (P0=2, P1=8, P2=29, P3=8)
- Source: 25954 lines / With docs: 31534 lines
- Tests: 17/17 + 18/21 PASS
- Apache 2.0 license

## Quick start
```
qemu-system-x86_64 -m 256M -cdrom opencube-wp08.iso -boot d -no-reboot \\
  -display none -serial stdio -monitor none -vga std -snapshot

oc> run hello
oc> run ush         # user-space shell
ush> uname         # → Open Cube OS WP-08 x86_64
```

## Asset
- opencube-wp08.iso (10.29 MB, BIOS + UEFI dual boot)
  SHA256: 76b1c8517566acf4fd0340414506ec4beadd374c9b2685b8e014a2fc9ddcbf58

## Release policy
- Latest version (WP-08): ISO only
- Older versions (WP-08a, WP-08b): ISO + SRC zip
'

PAYLOAD=$(jq -n \
  --arg tag "$TAG" \
  --arg name "$TAG" \
  --arg body "$RELEASE_BODY" \
  '{tag_name: $tag, name: $name, body: $body, draft: false, prerelease: false}')

RESP=$(curl -s -X POST \
  -H "Authorization: Bearer $GITHUB_TOKEN" \
  -H "Accept: application/vnd.github+json" \
  -H "X-GitHub-Api-Version: 2022-11-28" \
  -d "$PAYLOAD" \
  "https://api.github.com/repos/${REPO}/releases")

RELEASE_ID=$(echo "$RESP" | jq -r '.id')
UPLOAD_URL_TEMPLATE=$(echo "$RESP" | jq -r '.upload_url')
RELEASE_HTML_URL=$(echo "$RESP" | jq -r '.html_url')

if [ "$RELEASE_ID" = "null" ] || [ -z "$RELEASE_ID" ]; then
  echo "ERROR: Failed to create release. Response:"
  echo "$RESP" | jq .
  exit 1
fi
echo "  Release ID: $RELEASE_ID"
echo "  Release URL: $RELEASE_HTML_URL"

# ---- 3. Upload ISO asset ----
echo ""
echo "=== 3. Upload ISO asset ==="
UPLOAD_URL="${UPLOAD_URL_TEMPLATE%\{?name,label\}}"
ASSET_RESP=$(curl -s -X POST \
  -H "Authorization: Bearer $GITHUB_TOKEN" \
  -H "Accept: application/vnd.github+json" \
  -H "Content-Type: application/octet-stream" \
  --data-binary "@$ISO_LOCAL" \
  "${UPLOAD_URL}?name=opencube-wp08.iso")

ASSET_NAME=$(echo "$ASSET_RESP" | jq -r '.name')
ASSET_SIZE=$(echo "$ASSET_RESP" | jq -r '.size')
ASSET_DL=$(echo "$ASSET_RESP" | jq -r '.browser_download_url')

if [ -z "$ASSET_NAME" ] || [ "$ASSET_NAME" = "null" ]; then
  echo "ERROR: Failed to upload asset. Response:"
  echo "$ASSET_RESP" | jq .
  exit 1
fi
echo "  Asset: $ASSET_NAME ($ASSET_SIZE bytes)"
echo "  Download URL: $ASSET_DL"

# ---- 4. Final summary ----
echo ""
echo "=== 4. Final summary ==="
echo "Tag:          $TAG"
echo "Release URL:  $RELEASE_HTML_URL"
echo "Asset:        $ASSET_NAME ($ASSET_SIZE bytes)"
echo ""
echo "Done. Open the Release page to verify:"
echo "  $RELEASE_HTML_URL"
