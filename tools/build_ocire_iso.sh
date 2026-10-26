#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - 3.0.0-WP-01-ocire-core
# File: tools/build_ocire_iso.sh
# Purpose: Build the standalone bootable OCIRE ISO
#          (build/ocire/ocire.iso), hybrid BIOS + UEFI.
#
#          Same approach as tools/build_iso.sh (grub-mkimage + xorriso,
#          module lists filtered against the installed GRUB, and an
#          ESP FAT image for the UEFI El Torito entry - OVMF can only
#          boot an EFI payload from a FAT filesystem), packaged for
#          the OCIRE single-menu image.

set -e

OC_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

if [ -n "$OC_TOOLS" ]; then
    GRUB_DIR="$OC_TOOLS/usr/lib/grub"
else
    GRUB_DIR="/usr/lib/grub"
fi

BUILD="$OC_ROOT/build/ocire"
STAGE="$BUILD/iso-stage"
ELF="$BUILD/ocire.elf"
ISO="$BUILD/ocire.iso"

[ -f "$ELF" ] || { echo "[ocire_iso] FATAL: $ELF missing (run: make ocire-kernel)" >&2; exit 1; }

echo "[ocire_iso] staging in $STAGE"
rm -rf "$STAGE"
mkdir -p "$STAGE/boot/grub"
cp "$ELF" "$STAGE/boot/ocire.elf"
cp "$OC_ROOT/ocire/grub-ocire.cfg" "$STAGE/boot/grub/grub.cfg"

grub_mods() {
    local dir="$1"; shift
    local m out=""
    for m in "$@"; do
        if [ -f "$dir/$m.mod" ]; then out="$out $m"; else
            echo "[ocire_iso] note: $m.mod not shipped by this GRUB - skipped" >&2
        fi
    done
    echo "${out# }"
}

grub_mods_required() {
    local dir="$1" what="$2"; shift 2
    local m
    for m in "$@"; do
        if [ ! -f "$dir/$m.mod" ]; then
            echo "[ocire_iso] FATAL: essential $what module $m.mod is missing from $dir" >&2
            exit 1
        fi
    done
}

# --- 1. BIOS El Torito boot image (i386-pc) ---
grub_mods_required "$GRUB_DIR/i386-pc" BIOS \
    biosdisk iso9660 multiboot2 normal
MODS_BIOS="$(grub_mods "$GRUB_DIR/i386-pc" \
    biosdisk iso9660 normal echo configfile boot multiboot2 serial terminal test)"
echo "[ocire_iso] building BIOS El Torito image"
grub-mkimage -O i386-pc-eltorito \
  -d "$GRUB_DIR/i386-pc" \
  -p /boot/grub \
  -o "$STAGE/boot/grub/i386-pc-eltorito.img" \
  $MODS_BIOS

# --- 2. EFI bootloader (BOOTX64.EFI) ---
grub_mods_required "$GRUB_DIR/x86_64-efi" EFI \
    iso9660 multiboot2 normal efi_gop
MODS_EFI="$(grub_mods "$GRUB_DIR/x86_64-efi" \
    iso9660 normal echo configfile boot multiboot2 serial terminal test \
    efi_gop fixvideo)"
echo "[ocire_iso] building EFI bootloader"
mkdir -p "$STAGE/EFI/BOOT"
grub-mkimage -O x86_64-efi \
  -d "$GRUB_DIR/x86_64-efi" \
  -p /boot/grub \
  -o "$STAGE/EFI/BOOT/BOOTX64.EFI" \
  $MODS_EFI

# --- 3. ESP FAT image (OVMF boots EFI payloads from FAT only) ---
echo "[ocire_iso] building ESP FAT image"
ESP_IMG="$STAGE/boot/efi.img"
dd if=/dev/zero of="$ESP_IMG" bs=1M count=1 status=none
mformat -i "$ESP_IMG" -v EFI ::
mmd -i "$ESP_IMG" ::/EFI
mmd -i "$ESP_IMG" ::/EFI/BOOT
mcopy -i "$ESP_IMG" "$STAGE/EFI/BOOT/BOOTX64.EFI" ::/EFI/BOOT/BOOTX64.EFI

# --- 4. Assemble the hybrid ISO ---
echo "[ocire_iso] assembling ISO"
xorriso -as mkisofs \
  -graft-points \
  --protective-msdos-label \
  -o "$ISO" \
  -b boot/grub/i386-pc-eltorito.img \
  -no-emul-boot \
  -boot-load-size 4 \
  -boot-info-table \
  --grub2-boot-info \
  -eltorito-alt-boot \
  -e boot/efi.img \
  -no-emul-boot \
  -isohybrid-gpt-hfsplus \
  -r "$STAGE" 2>&1 | tail -3

echo "[ocire_iso] done: $ISO ($(stat -c%s "$ISO") bytes)"
echo "[ocire_iso] El Torito info:"
xorriso -indev "$ISO" -report_el_torito plain 2>&1 | tail -8
