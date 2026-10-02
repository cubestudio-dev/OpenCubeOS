#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - WP-10u
# File: tools/make_update_pkg.sh
# Purpose: build a WP-10u update package (tar.gz + manifest.json).
#
# Package layout (see kernel/ab_update.h):
#   opencube-<ver>-update/
#     manifest.json          version + files + payload sha256
#     kernel/opencube.elf    kernel ELF (installed to /boot/opencube.elf)
#     boot/grub.cfg          GRUB config (mirrored to the flag partition)
#     etc/opencube.conf      default configuration
#     docs/...               documentation
#
# manifest.json "sha256" = SHA256 over the CONCATENATION of every payload
# file body, in ARCHIVE order, manifest.json itself excluded.  The kernel
# installer hashes the decompressed archive the same way while streaming.
# (The manifest cannot cover its own bytes or the .tar.gz container it
# ships inside, so the payload-level digest is the verifiable choice.)
#
# Usage: bash tools/make_update_pkg.sh <version> <kernel-elf> <out.tar.gz>
set -e

VER="$1"
KERNEL="$2"
OUT="$3"
[ -n "$VER" ] && [ -f "$KERNEL" ] && [ -n "$OUT" ] || {
  echo "usage: $0 <version> <kernel-elf> <out.tar.gz>" >&2
  exit 1
}

OC_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
STAGE="$(mktemp -d)"
PKGDIR="$STAGE/opencube-${VER}-update"
TARBALL="$STAGE/pkg.tar"

mkdir -p "$PKGDIR/kernel" "$PKGDIR/boot" "$PKGDIR/etc" "$PKGDIR/docs"
cp "$KERNEL" "$PKGDIR/kernel/opencube.elf"
cp "$OC_ROOT/grub-ab.cfg" "$PKGDIR/boot/grub.cfg"
cp "$OC_ROOT/etc/opencube.conf" "$PKGDIR/etc/opencube.conf"
cp "$OC_ROOT/docs/CONFIG.md" "$PKGDIR/docs/" 2>/dev/null || true

{
  echo '{'
  echo "  \"version\": \"$VER\","
  echo "  \"time\": \"$(date -u +%F)\","
  echo "  \"files\": ["
  echo "    \"kernel/opencube.elf\","
  echo "    \"boot/grub.cfg\","
  echo "    \"etc/opencube.conf\","
  echo "    \"docs/CONFIG.md\""
  echo "  ],"
  echo "  \"sha256\": \"PENDING\""
  echo '}'
} > "$PKGDIR/manifest.json"

# first tar: used only to compute the payload digest in archive order
tar -C "$STAGE" -cf "$TARBALL" "$(basename "$PKGDIR")"
PAYLOAD_SHA="$(python3 - "$TARBALL" <<'PYEOF'
import hashlib, sys, tarfile
h = hashlib.sha256()
with tarfile.open(sys.argv[1], "r") as t:
    for m in t.getmembers():
        if not m.isfile() or m.name.endswith("/manifest.json"):
            continue
        f = t.extractfile(m)
        if f is None:
            continue
        h.update(f.read())
print(h.hexdigest())
PYEOF
)"

sed -i "s/PENDING/$PAYLOAD_SHA/" "$PKGDIR/manifest.json"

# final tar with the real manifest; the payload members are unchanged so
# the digest above stays valid
rm -f "$TARBALL"
tar -C "$STAGE" -cf "$TARBALL" "$(basename "$PKGDIR")"
gzip -9 -c "$TARBALL" > "$OUT"
FINAL_SHA="$(sha256sum "$OUT" | cut -d' ' -f1)"
FINAL_SIZE="$(stat -c%s "$OUT")"

echo "[make_update_pkg] package: $OUT"
echo "[make_update_pkg] payload sha256 (manifest): $PAYLOAD_SHA"
echo "[make_update_pkg] package sha256 (.tar.gz): $FINAL_SHA"
echo "[make_update_pkg] package size: $FINAL_SIZE"
rm -rf "$STAGE"
