#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - 3.0.0-WP-01-ocire-core
# File: tools/make_ocire_disk.sh
# Purpose: build the WP-01 dual-boot test disks:
#
#   build/ocire/ocire-boot.img (hd0, 128 MiB)
#       MBR p1 FAT32 holding:
#         /boot/opencube.elf     - the OCOS kernel (multiboot2)
#         /boot/ocire.elf        - copy of the OCIRE kernel (evidence of
#                                   "coexist on disk" - the boot source
#                                   used by the menu is hd1 though)
#         /boot/grub/grub.cfg   - the dual menu (ocire/grub-dual.cfg)
#         /EFI/BOOT/BOOTX64.EFI - UEFI GRUB (same menu via -p /boot/grub)
#       BIOS boot: GRUB boot.img in the MBR + core.img in the post-MBR
#       gap (LBA 1..2047; boot.img's default kernel_sector=1 and the
#       diskboot default continuation blocklist both point there, so a
#       plain dd install works - verified by run_ocire_dual_tests.py).
#
#   build/ocire/ocire-hd.img (hd1, 16 MiB) - the OCIRE disk
#       MBR with ONE entry: type 0xEE (GPT protective shadow entry)
#       with NO GPT header behind it (LBA 1 is FAT data, not "EFI PART").
#       The partition content is a plain FAT16 volume holding
#       /boot/ocire.elf.
#       - GRUB: part_msdos yields the 0xEE entry as (hd1,msdos1) and
#         the fat module mounts the FAT16 volume by its boot sector -
#         the menu loads (hd1,msdos1)/boot/ocire.elf with a normal file
#         path.  (GRUB does NOT switch to GPT parsing on its own.)
#       - OCOS: driver_block_part_parse_mbr() sees 0xEE and hands the
#         table to driver_block_part_parse_gpt(), which fails (no GPT
#         header) -> the WHOLE table is refused -> partscan registers
#         NO child device for this disk -> parted prints
#         "no partition table found", hdNpN devices do not exist, no
#         filesystem path can reach the OCIRE partition.  This is the
#         WP-01 partition isolation mechanism; it is measured by
#         tools/run_ocire_dual_tests.py (isolation case).

set -e

OC_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TOOLBIN="$(cd "${OC_TOOLS:-/home/z/opt/extract}/usr/bin" && pwd)"
export PATH="$TOOLBIN:$PATH"
GRUB_DIR="${OC_TOOLS:-/home/z/opt/extract}/usr/lib/grub"
export LD_LIBRARY_PATH="${OC_TOOLS:-/home/z/opt/extract}/usr/lib/x86_64-linux-gnu:${OC_TOOLS:-/home/z/opt/extract}/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

BUILD="$OC_ROOT/build/ocire"
OCOS_ELF="$OC_ROOT/build/opencube.elf"
OCIRE_ELF="$BUILD/ocire.elf"
HD0="$BUILD/ocire-boot.img"
HD1="$BUILD/ocire-hd.img"

[ -f "$OCOS_ELF" ]  || { echo "[ocire_disk] FATAL: $OCOS_ELF missing (make kernel)" >&2; exit 1; }
[ -f "$OCIRE_ELF" ] || { echo "[ocire_disk] FATAL: $OCIRE_ELF missing (make ocire-kernel)" >&2; exit 1; }

# ----------------------------------------------------------------------
# hd0: dual-boot GRUB disk (128 MiB, p1 FAT32 from LBA 2048)
# ----------------------------------------------------------------------
P1_START=2048
P1_SECTORS=$((128*1024*1024/512 - P1_START))
GAP_SECTORS=$((P1_START - 1))            # post-MBR gap for core.img

echo "[ocire_disk] hd0: $HD0 (${P1_SECTORS} sectors FAT32)"
rm -f "$HD0"
truncate -s $(( (P1_START + P1_SECTORS) * 512 )) "$HD0"

python3 - "$HD0" "$P1_START" "$P1_SECTORS" <<'PYEOF'
import struct, sys
path, start, sectors = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
mbr = bytearray(512)
e = 446
mbr[e+0] = 0x80                    # bootable
mbr[e+1: e+4] = b"\x20\x21\x00"    # CHS start (dummy, LBA used)
mbr[e+4] = 0x0C                    # FAT32 (LBA)
mbr[e+5: e+8] = b"\xfe\xff\xff"
mbr[e+8: e+12] = struct.pack("<I", start)
mbr[e+12: e+16] = struct.pack("<I", sectors)
mbr[510:512] = b"\x55\xAA"
with open(path, "r+b") as f:
    f.write(bytes(mbr))
print("[ocire_disk] hd0 MBR written (p1 FAT32 LBA %d, %d sectors)" % (start, sectors))
PYEOF

# ---- p1 FAT32 file system (mtools on a pulled partition file) ----
PULL="$BUILD/.hd0.p1"
dd if="$HD0" of="$PULL" bs=512 skip=$P1_START count=$P1_SECTORS status=none
mformat -i "$PULL" -F -v OCOS_BOOT ::
mmd  -i "$PULL" ::/boot ::/boot/grub ::/EFI ::/EFI/BOOT
mcopy -i "$PULL" "$OCOS_ELF"  ::/boot/opencube.elf
mcopy -i "$PULL" "$OCIRE_ELF" ::/boot/ocire.elf
mcopy -i "$PULL" "$OC_ROOT/ocire/grub-dual.cfg" ::/boot/grub/grub.cfg

# ---- UEFI GRUB (BOOTX64.EFI, modules embedded, config via prefix) ----
grub-mkimage -O x86_64-efi \
  -d "$GRUB_DIR/x86_64-efi" \
  -p '(hd0,msdos1)/boot/grub' \
  -o "$BUILD/.BOOTX64.EFI" \
  fat part_msdos multiboot2 boot normal configfile echo terminal serial test efi_gop
mcopy -i "$PULL" "$BUILD/.BOOTX64.EFI" ::/EFI/BOOT/BOOTX64.EFI

dd if="$PULL" of="$HD0" bs=512 seek=$P1_START count=$P1_SECTORS conv=notrunc status=none
rm -f "$PULL"

# ---- BIOS GRUB: boot.img (MBR) + core.img (post-MBR gap, LBA 1) ----
MODS_BIOS="biosdisk part_msdos fat multiboot2 boot normal configfile echo terminal serial test"
grub-mkimage -O i386-pc \
  -d "$GRUB_DIR/i386-pc" \
  -p '(hd0,msdos1)/boot/grub' \
  -o "$BUILD/.core.img" \
  $MODS_BIOS
CORE_SECTORS=$(( ($(stat -c%s "$BUILD/.core.img") + 511) / 512 ))
if [ "$CORE_SECTORS" -ge "$GAP_SECTORS" ]; then
    echo "[ocire_disk] FATAL: core.img ($CORE_SECTORS sectors) does not fit the post-MBR gap ($GAP_SECTORS)" >&2
    exit 1
fi
# boot.img's default kernel_sector is LBA 1 and diskboot's default
# continuation blocklist starts at LBA 2 - a plain dd install works
# (both are the GRUB source defaults, verified by the dual-boot tests).
dd if="$GRUB_DIR/i386-pc/boot.img" of="$HD0" bs=446 count=1 conv=notrunc status=none
dd if="$BUILD/.core.img" of="$HD0" bs=512 seek=1 conv=notrunc status=none
echo "[ocire_disk] hd0 GRUB installed: boot.img (MBR) + core.img (LBA1, $CORE_SECTORS sectors)"

# ----------------------------------------------------------------------
# hd1: the OCIRE disk (16 MiB, single 0xEE shadow entry)
# ----------------------------------------------------------------------
HD1_START=2048
HD1_SECTORS=$((16*1024*1024/512 - HD1_START))
echo "[ocire_disk] hd1: $HD1 (single partition, type 0xEE shadow)"
rm -f "$HD1"
truncate -s $(( (HD1_START + HD1_SECTORS) * 512 )) "$HD1"

python3 - "$HD1" "$HD1_START" "$HD1_SECTORS" <<'PYEOF'
import struct, sys
path, start, sectors = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
mbr = bytearray(512)
e = 446
mbr[e+0] = 0x00                    # not flagged bootable (GRUB loads it by hand)
mbr[e+1: e+4] = b"\x20\x21\x00"
mbr[e+4] = 0xEE                    # GPT protective shadow entry
mbr[e+5: e+8] = b"\xfe\xff\xff"
mbr[e+8: e+12] = struct.pack("<I", start)
mbr[e+12: e+16] = struct.pack("<I", sectors)
mbr[510:512] = b"\x55\xAA"
with open(path, "r+b") as f:
    f.write(bytes(mbr))
print("[ocire_disk] hd1 MBR written (p1 type 0xEE, LBA %d, %d sectors, no GPT header)" % (start, sectors))
PYEOF

# ---- the 0xEE partition content: the raw OCIRE kernel image ----
# GRUB 2.12's msdos partmap SKIPS type-0xEE entries (verified live:
# "error: disk `hd1,msdos1' not found"), so the menu cannot reach the
# partition through a partition path.  Instead the OCIRE kernel is
# stored as a raw image at the partition start and the menu loads it
# with a whole-disk blocklist: multiboot2 (hd1)2048+128  (see
# ocire/grub-dual.cfg).  Blocklists bypass the partition table, so the
# 0xEE shadow entry stays fully effective for OCOS (whole-table
# rejection) while GRUB still boots the kernel.
dd if="$OCIRE_ELF" of="$HD1" bs=512 seek=$HD1_START conv=notrunc status=none
OCIRE_ELF_SECTORS=$(( ($(stat -c%s "$OCIRE_ELF") + 511) / 512 ))
echo "[ocire_disk] hd1: raw OCIRE kernel at LBA $HD1_START ($OCIRE_ELF_SECTORS sectors)"
if [ "$OCIRE_ELF_SECTORS" -gt 128 ]; then
    echo "[ocire_disk] FATAL: OCIRE ELF exceeds the 128-sector blocklist window" >&2
    exit 1
fi

rm -f "$BUILD/.core.img" "$BUILD/.BOOTX64.EFI"
echo "[ocire_disk] done:"
ls -la "$HD0" "$HD1"
