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
# K-10 root fix (WP-severe-fix-and-closure): the UEFI track used to
#          stage the OCBL PE (whose entry RVA is the POST-EBS raw
#          entry, the handoff ABI) directly as BOOTX64.EFI - OVMF's
#          StartImage then arrived at ocbl_raw_entry with the EFI
#          ABI (RCX=image handle, RDX=system table), the handoff
#          magic check failed, and the whole UEFI track died on the
#          single '!' marker.  The chain is the main ISO's now:
#          BOOTX64.EFI = the OCRMECL PE (it performs the firmware
#          calls, finds /OCBL.EFI in the ISO, LoadImage-from-buffer,
#          EBS, and jumps to OCBL's raw entry with the handoff ABI);
#          the El Torito BIOS image = the OCRMECL composite (it
#          embeds ocbl_bios.bin).
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
if [ ! -f "$BUILD/ocrmecl/ocrmecl_bios.bin" ] || \
   [ ! -f "$BUILD/ocrmecl/ocrmecl_x64.efi" ]; then
    echo "[ocire_iso] FATAL: OCRMECL images missing (make ocrmecl)" >&2
    exit 1
fi

echo "[ocire_iso] staging in $STAGE"
rm -rf "$STAGE"
mkdir -p "$STAGE/boot"

# --- 1. the recovery kernel payload (OCIRE class marker file) ---
cp "$BUILD/ocire/ocire.elf" "$STAGE/boot/ocire.elf"

# --- 2. the OCRMECL -> OCBL dual-firmware chain (the main ISO's layout) ---
cp "$BUILD/ocrmecl/ocrmecl_bios.bin" "$STAGE/OCRMECL.BIN"
cp "$BUILD/ocbl/ocbl_x64.efi" "$STAGE/OCBL.EFI"
cp "$BUILD/ocrmecl/ocrmecl_x64.efi" "$STAGE/BOOTX64.EFI"

# --- 3. pack: BIOS El Torito (the OCRMECL composite) + UEFI El Torito
#        (1 MiB ESP), G13 seals ---
python3 "$OC_ROOT/ocbl/build_test_iso.py" "$BUILD/ocire/ocire.iso" \
        "$STAGE" OCRMECL.BIN BOOTX64.EFI

echo "[ocire_iso] ISO: $BUILD/ocire/ocire.iso"
ls -la "$BUILD/ocire/ocire.iso"
