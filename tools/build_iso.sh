#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - WP-01
# File: tools/build_iso.sh
# Purpose: Build a hybrid BIOS+UEFI bootable ISO using grub-mkimage + xorriso
#          directly, bypassing grub-mkrescue (which has trouble finding the
#          extracted GRUB modules and unicode.pf2 in some environments).
#
# Output: build/opencube.iso
#
# This script reads the toolchain location from the OC_TOOLS environment
# variable (set by your env.sh or equivalent). If OC_TOOLS is not set, it
# falls back to system-installed GRUB.

set -e

OC_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# Toolchain location: read from env, fall back to system paths.
if [ -n "$OC_TOOLS" ]; then
    GRUB_DIR="$OC_TOOLS/usr/lib/grub"
    GRUB_SHARE="$OC_TOOLS/usr/share/grub"
else
    GRUB_DIR="/usr/lib/grub"
    GRUB_SHARE="/usr/share/grub"
fi
# Fallback: if the font isn't in GRUB_SHARE, check alternate locations.
if [ ! -f "$GRUB_SHARE/unicode.pf2" ]; then
    if [ -f "/tmp/grub_share/unicode.pf2" ]; then
        GRUB_SHARE="/tmp/grub_share"
    elif [ -f "$OC_ROOT/iso/boot/grub/fonts/unicode.pf2" ]; then
        GRUB_SHARE="$OC_ROOT/iso/boot/grub/fonts"
    fi
fi

BUILD="$OC_ROOT/build"
ISO_STAGE="$BUILD/iso-stage"

echo "[build_iso] staging in $ISO_STAGE"
rm -rf "$ISO_STAGE"
mkdir -p "$ISO_STAGE/boot/grub"
mkdir -p "$ISO_STAGE/boot/grub/i386-pc"
mkdir -p "$ISO_STAGE/boot/grub/x86_64-efi"
mkdir -p "$ISO_STAGE/boot/grub/fonts"
mkdir -p "$ISO_STAGE/EFI/BOOT"

# --- 1. Copy kernel ELF ---
cp "$BUILD/opencube.elf" "$ISO_STAGE/boot/opencube.elf"

# --- 2. Copy grub.cfg ---
cp "$OC_ROOT/grub.cfg" "$ISO_STAGE/boot/grub/grub.cfg"

# --- 3. Copy unicode.pf2 font ---
cp "$GRUB_SHARE/unicode.pf2" "$ISO_STAGE/boot/grub/fonts/unicode.pf2"

# --- 4. Copy all GRUB modules to the ISO so insmod works at runtime ---
cp "$GRUB_DIR/i386-pc/"*.mod "$ISO_STAGE/boot/grub/i386-pc/" 2>/dev/null || true
cp "$GRUB_DIR/i386-pc/"*.lst "$ISO_STAGE/boot/grub/i386-pc/" 2>/dev/null || true
cp "$GRUB_DIR/x86_64-efi/"*.mod "$ISO_STAGE/boot/grub/x86_64-efi/" 2>/dev/null || true
cp "$GRUB_DIR/x86_64-efi/"*.lst "$ISO_STAGE/boot/grub/x86_64-efi/" 2>/dev/null || true

# --- 5. Build BIOS El Torito boot image (i386-pc-eltorito) ---
GRUB_MODULES_BIOS="biosdisk iso9660 fat ext2 normal echo ls cat help configfile \
              test linux multiboot2 multiboot boot serial terminal gfxterm \
              all_video vbe vga video video_fb video_bochs video_cirrus \
              video_colors font bufio part_msdos part_gpt reboot halt"
echo "[build_iso] building BIOS El Torito image"
grub-mkimage -O i386-pc-eltorito \
  -d "$GRUB_DIR/i386-pc" \
  -p /boot/grub \
  -o "$ISO_STAGE/boot/grub/i386-pc/eltorito.img" \
  $GRUB_MODULES_BIOS

# --- 6. Build EFI bootloader (BOOTX64.EFI) ---
GRUB_MODULES_EFI="iso9660 fat ext2 normal echo ls cat help configfile \
              test linux multiboot2 multiboot boot serial terminal gfxterm \
              all_video efi_gop efi_uga fixvideo video video_fb video_bochs \
              video_cirrus video_colors font bufio part_msdos part_gpt \
              reboot halt"
echo "[build_iso] building EFI bootloader"
grub-mkimage -O x86_64-efi \
  -d "$GRUB_DIR/x86_64-efi" \
  -p /boot/grub \
  -o "$ISO_STAGE/EFI/BOOT/BOOTX64.EFI" \
  $GRUB_MODULES_EFI

# --- 7. Build a FAT image for the EFI System Partition (ESP) ---
echo "[build_iso] building ESP FAT image"
ESP_IMG="$ISO_STAGE/boot/efi.img"
dd if=/dev/zero of="$ESP_IMG" bs=1M count=1 status=none
mformat -i "$ESP_IMG" -v EFI ::
mmd -i "$ESP_IMG" ::/EFI
mmd -i "$ESP_IMG" ::/EFI/BOOT
mcopy -i "$ESP_IMG" "$ISO_STAGE/EFI/BOOT/BOOTX64.EFI" ::/EFI/BOOT/BOOTX64.EFI
echo "[build_iso] ESP FAT image contents:"
mdir -i "$ESP_IMG" ::/ 2>&1
echo "[build_iso] /EFI/BOOT contents:"
mdir -i "$ESP_IMG" ::/EFI/BOOT 2>&1

# --- 8. Build the ISO with xorriso, with both BIOS and UEFI boot ---
echo "[build_iso] building ISO"
mkdir -p "$BUILD"
xorriso -as mkisofs \
  -graft-points \
  --protective-msdos-label \
  -o "$BUILD/opencube.iso" \
  -b boot/grub/i386-pc/eltorito.img \
  -no-emul-boot \
  -boot-load-size 4 \
  -boot-info-table \
  --grub2-boot-info \
  -eltorito-alt-boot \
  -e boot/efi.img \
  -no-emul-boot \
  -isohybrid-gpt-hfsplus \
  -r "$ISO_STAGE" 2>&1 | tail -5

echo "[build_iso] ISO: $BUILD/opencube.iso"
ls -la "$BUILD/opencube.iso"

echo "[build_iso] El Torito info:"
xorriso -indev "$BUILD/opencube.iso" -report_el_torito plain 2>&1 | tail -10
