#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - WP-01
# File: tools/build_iso.sh
# Purpose: Build a hybrid BIOS+UEFI bootable ISO using grub-mkimage + xorriso
# 3.0.0-WP04 G13: --modification-date + --set_all_file_dates pin every
# ISO9660/Rock-Ridge timestamp (epoch) so two clean builds of the same
# tree are byte-identical (the G13 determinism gate).  NOTE: the TIME
# form must be YYYYMMDDhhmmsscc - a bare '0' is rejected by xorriso
# ('Cannot decode timestring') and silently leaves dates unpinned.
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
# 2.1.0 (P-01 follow-up): filter the module lists against the modules
# the installed GRUB actually ships.  GRUB retires old drivers over
# time (2.12 removed efi_uga - UGA graphics is dead, efi_gop has been
# the standard EFI driver for a decade+); a hardcoded list made
# grub-mkimage fail on CI runners whose GRUB is newer than the list.
# Boot behaviour is unchanged on every toolchain that still ships the
# retired modules; a missing ESSENTIAL module still fails loudly.
grub_mods() {
    # grub_mods <dir> <mod> [mod...] - echo the shipped subset.
    local dir="$1"; shift
    local m out=""
    for m in "$@"; do
        if [ -f "$dir/$m.mod" ]; then
            out="$out $m"
        else
            echo "[build_iso] note: $m.mod not shipped by this GRUB - skipped" >&2
        fi
    done
    echo "${out# }"
}

grub_mods_required() {
    # grub_mods_required <dir> <what> <mod>... - hard-fail if missing.
    local dir="$1" what="$2"; shift 2
    local m
    for m in "$@"; do
        if [ ! -f "$dir/$m.mod" ]; then
            echo "[build_iso] FATAL: essential $what module $m.mod is missing from $dir" >&2
            exit 1
        fi
    done
}

grub_mods_required "$GRUB_DIR/i386-pc" BIOS \
    biosdisk iso9660 multiboot2 normal
GRUB_MODULES_BIOS="$(grub_mods "$GRUB_DIR/i386-pc" \
    biosdisk iso9660 fat ext2 normal echo ls cat help configfile search search_fs_file \
    test linux multiboot2 multiboot boot serial terminal gfxterm \
    all_video vbe vga video video_fb video_bochs video_cirrus \
    video_colors font bufio part_msdos part_gpt reboot halt)"
echo "[build_iso] building BIOS El Torito image"
grub-mkimage -O i386-pc-eltorito \
  -d "$GRUB_DIR/i386-pc" \
  -p /boot/grub \
  -o "$ISO_STAGE/boot/grub/i386-pc/eltorito.img" \
  $GRUB_MODULES_BIOS

# --- 6. Build EFI bootloader (BOOTX64.EFI) ---
grub_mods_required "$GRUB_DIR/x86_64-efi" EFI \
    iso9660 multiboot2 normal efi_gop
GRUB_MODULES_EFI="$(grub_mods "$GRUB_DIR/x86_64-efi" \
    iso9660 fat ext2 normal echo ls cat help configfile search search_fs_file \
    test linux multiboot2 multiboot boot serial terminal gfxterm \
    all_video efi_gop efi_uga fixvideo video video_fb video_bochs \
    video_cirrus video_colors font bufio part_msdos part_gpt \
    reboot halt)"
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
# G13 determinism: fixed volume serial (mformat derives it from the
# wall clock otherwise) + all directory date/time fields scrubbed to
# zero below, so clean builds minutes apart are byte-identical.
mformat -i "$ESP_IMG" -N 1 -v EFI ::
mmd -i "$ESP_IMG" ::/EFI
mmd -i "$ESP_IMG" ::/EFI/BOOT
mcopy -i "$ESP_IMG" "$ISO_STAGE/EFI/BOOT/BOOTX64.EFI" ::/EFI/BOOT/BOOTX64.EFI
python3 "$OC_ROOT/tools/fat_zero_dates.py" "$ESP_IMG"
echo "[build_iso] ESP FAT image contents:"
mdir -i "$ESP_IMG" ::/ 2>&1
echo "[build_iso] /EFI/BOOT contents:"
mdir -i "$ESP_IMG" ::/EFI/BOOT 2>&1

# --- 8. Build the ISO with xorriso, with both BIOS and UEFI boot ---
# G13 determinism (final piece): the staged directories were mkdir'ed
# seconds ago, so their ISO9660 directory-record dates would carry the
# wall clock (--set_all_file_dates pins files, not dirs).  Normalize
# the whole stage tree to the epoch before packing.
find "$ISO_STAGE" -exec touch -d @0 {} +
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
  --modification-date=1970010100000000 \
  --set_all_file_dates 1970010100000000 \
  --gpt_disk_guid DEADBEEF-0000-0000-0000-00000000C0DE \
  -eltorito-alt-boot \
  -e boot/efi.img \
  -no-emul-boot \
  -isohybrid-gpt-hfsplus \
  -r "$ISO_STAGE" 2>&1 | tail -5

# G13 determinimsm (last mile): xorriso hardcodes the Rock Ridge TF
# ctime stamp to the assembly moment (-alter_date has no ctime type);
# zero every TF ctime in the finished image so clean builds minutes
# apart stay byte-identical.
python3 "$OC_ROOT/tools/iso_zero_ctime.py" "$BUILD/opencube.iso"
echo "[build_iso] ISO: $BUILD/opencube.iso"
ls -la "$BUILD/opencube.iso"

echo "[build_iso] El Torito info:"
xorriso -indev "$BUILD/opencube.iso" -report_el_torito plain 2>&1 | tail -10
