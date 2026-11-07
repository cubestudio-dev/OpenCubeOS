#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - build toolchain installer (no-root sandbox).
#
# Downloads the Debian .deb packages needed to build + boot-test Open
# Cube OS and extracts them into $OC_OPT (default /home/z/opt/extract).
# Run with the extracted bin dirs prepended to PATH (see the hint at
# the end). gcc itself comes from the system (Debian gcc 14.2.0 is the
# same compiler the release builds have always used).
#
# Packages installed:
#   nasm 2.16.03, qemu-system-x86 10.0.13 (+ data + libs), xorriso 1.5.6,
#   grub-pc-bin + grub-efi-amd64-bin + grub-common (BIOS+EFI modules),
#   mtools (grub-mkrescue/mformat), ovmf (UEFI firmware), seabios.
#
# Usage: bash tools/install-toolchain.sh
set -e

OC_OPT="${OC_OPT:-/home/z/opt/extract}"
DL="$OC_OPT/debs"
mkdir -p "$DL"

echo "[toolchain] target dir: $OC_OPT"
echo "[toolchain] resolving package list (batch 1/3: build tools)"
PKGS1="nasm xorriso mtools"
echo "[toolchain] resolving package list (batch 2/3: qemu)"
PKGS2="qemu-system-x86 qemu-system-data"
echo "[toolchain] resolving package list (batch 3/3: grub + firmware)"
PKGS3="grub-pc-bin grub-efi-amd64-bin grub-common ovmf seabios"

download_batch() {
    local tag="$1"; shift
    local pkgs="$1"; shift
    local want
    want=$(apt-cache depends --recurse --no-recommends --no-suggests \
           --no-conflicts --no-breaks --no-replaces --no-enhances \
           $pkgs 2>/dev/null | grep '^\w' | sort -u | tr '\n' ' ')
    echo "[toolchain] $tag: $(echo $want | wc -w) packages (incl. deps)"
    # shellcheck disable=SC2086
    (cd "$DL" && apt-get download $want 2>&1 | tail -2)
}

download_batch "batch1" "$PKGS1"
download_batch "batch2" "$PKGS2"
download_batch "batch3" "$PKGS3"

echo "[toolchain] extracting $(ls "$DL"/*.deb 2>/dev/null | wc -l) debs"
for d in "$DL"/*.deb; do
    dpkg-deb -x "$d" "$OC_OPT"
done

echo "[toolchain] done. Export for builds:"
echo "  export OC_TOOLS=$OC_OPT"
echo "  export PATH=\"$OC_OPT/usr/bin:\$PATH\""
echo "  export LD_LIBRARY_PATH=\"$OC_OPT/usr/lib/x86_64-linux-gnu:$OC_OPT/lib/x86_64-linux-gnu\""
