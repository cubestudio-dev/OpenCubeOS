#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - tools/build_ocire_iso.sh
# Purpose: build the standalone OCIRE recovery ISO.  3.0.0-WP05 B5-7:
#          the GRUB chain is REMOVED - the DUAL-FIRMWARE OCBL boot
#          loader boots this ISO under SeaBIOS AND OVMF, and the OCBL
#          six-class scanner picks the /boot/ocire.elf payload out of
#          it (the OCIRE class: a volume carrying /boot/ocire.elf).
#          The G13 determinism seals travel with the OCBL packer.
#
# Output: build/ocire/ocire.iso
set -e

OC_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$OC_ROOT/build"
OCIRE_DIR="$OC_ROOT/ocire"
STAGE="$BUILD/ocire-iso-stage"

if [ ! -f "$BUILD/ocire/ocire.elf" ]; then
    echo "[ocire_iso] FATAL: $BUILD/ocire/ocire.elf missing (make ocire-kernel)" >&2
    exit 1
fi
if [ ! -f "$BUILD/ocbl/ocbl_bios.bin" ] || \
   [ ! -f "$BUILD/ocbl/ocbl_x64.efi" ]; then
    echo "[ocire_iso] FATAL: OCBL images missing (make ocbl-bios ocbl-efi)" >&2
    exit 1
fi

echo "[ocire_iso] staging in $STAGE"
rm -rf "$STAGE"
mkdir -p "$STAGE/boot"

# --- 1. the recovery kernel payload (OCIRE class marker file) ---
cp "$BUILD/ocire/ocire.elf" "$STAGE/boot/ocire.elf"

# --- 2. the OCBL dual-firmware pair ---
cp "$BUILD/ocbl/ocbl_bios.bin" "$STAGE/OCBL.BIN"
cp "$BUILD/ocbl/ocbl_x64.efi" "$STAGE/BOOTX64.EFI"

# --- 3. pack: BIOS El Torito + UEFI El Torito (1 MiB ESP), G13 seals ---
python3 "$OC_ROOT/ocbl/build_test_iso.py" "$BUILD/ocire/ocire.iso" \
        "$STAGE" OCBL.BIN BOOTX64.EFI

echo "[ocire_iso] ISO: $BUILD/ocire/ocire.iso"
ls -la "$BUILD/ocire/ocire.iso"
