#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - WP-10u
# File: tools/make_ab_disk.sh
# Purpose: build the A/B update disk image (build/abdisk.img).
#
# Layout (MBR, matches kernel/ab_update.h):
#   p1 64 MiB  FAT32  boot/flags  - /boot/grub/grub.cfg + boot flag files
#   p2 128 MiB FAT32  slot A      - /boot/opencube.elf, /etc, ...
#   p3 128 MiB FAT32  slot B      - same structure as slot A
#   p4 128 MiB FAT32  data        - user data, downloaded packages
#
# Every partition is dd-extracted to a scratch file, formatted/seeded
# with mtools there and written back (no loop devices, no root).
#
# Usage:
#   bash tools/make_ab_disk.sh [kernel-elf]
#     kernel-elf: optional; seed slot A's /boot/opencube.elf with it
#                 (so the disk boots on its own without an ISO).
set -e

OC_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${OC_ROOT}/build/abdisk.img"
GRUB_CFG="${OC_ROOT}/grub-ab.cfg"
TOOLBIN="${OC_TOOLS:-/home/z/opt/extract}/usr/bin"
export PATH="$TOOLBIN:$PATH"
# grub-mkimage links against toolchain libs (libdevmapper etc.) - make the
# script self-sufficient even when tools/env.sh was not sourced.
export LD_LIBRARY_PATH="${OC_TOOLS:-/home/z/opt/extract}/usr/lib/x86_64-linux-gnu:${OC_TOOLS:-/home/z/opt/extract}/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

P1_START=2048                                            # 1 MiB align
P1_SECTORS=$((64 * 1024 * 1024 / 512))                   # 64 MiB
P2_SECTORS=$((128 * 1024 * 1024 / 512))                  # 128 MiB
P3_SECTORS=$((128 * 1024 * 1024 / 512))                  # 128 MiB
P4_SECTORS=$((128 * 1024 * 1024 / 512))                  # 128 MiB
P2_START=$((P1_START + P1_SECTORS))
P3_START=$((P2_START + P2_SECTORS))
P4_START=$((P3_START + P3_SECTORS))
TOTAL_SECTORS=$((P4_START + P4_SECTORS))

echo "[m[make_ab_disk] image: $OUT (${TOTAL_SECTORS} sectors)"
rm -f "$OUT"
truncate -s $((TOTAL_SECTORS * 512)) "$OUT"

# ---- 1. MBR partition table (python, deterministic) -----------------
python3 - "$OUT" "$P1_SECTORS" "$P2_SECTORS" "$P3_SECTORS" "$P4_SECTORS" <<'PYEOF'
import struct, sys
path = sys.argv[1]
p1, p2, p3, p4 = (int(x) for x in sys.argv[2:6])
types = [0x0B, 0x0B, 0x0B, 0x0B]   # FAT32 (LBA)
starts = [2048]
sizes = [p1, p2, p3, p4]
for i in range(1, 4):
    starts.append(starts[i-1] + sizes[i-1])
mbr = bytearray(512)
for i in range(4):
    off = 446 + i * 16
    mbr[off + 0] = 0x00                    # not bootable (GRUB manages boot)
    mbr[off + 1] = 0xFE; mbr[off + 2] = 0xFF; mbr[off + 3] = 0xFF
    mbr[off + 4] = types[i]
    mbr[off + 5] = 0xFE; mbr[off + 6] = 0xFF; mbr[off + 7] = 0xFF
    mbr[off + 8:off + 12] = struct.pack("<I", starts[i])
    mbr[off + 12:off + 16] = struct.pack("<I", sizes[i])
mbr[510:512] = b"\x55\xAA"
with open(path, "r+b") as f:
    f.seek(0)
    f.write(bytes(mbr))
    print("[make_ab_disk] MBR written: starts=%s" % starts)
PYEOF

# ---- helpers ----------------------------------------------------------
pull_part() {   # pull_part <part_no> <start_sector> <sectors>
  dd if="$OUT" of="$OUT.p$1" bs=512 skip="$2" count="$3" status=none
}
push_part() {   # push_part <part_no> <start_sector> <sectors>
  dd if="$OUT.p$1" of="$OUT" bs=512 seek="$2" count="$3" conv=notrunc status=none
  rm -f "$OUT.p$1"
}

# ---- 2. FAT32 format every partition ----------------------------------
fmt_part() {    # fmt_part <part_no> <start_sector> <sectors> <label>
  pull_part "$1" "$2" "$3"
  mformat -i "$OUT.p$1" -F -v "$4" ::
  push_part "$1" "$2" "$3"
  echo "[make_ab_disk] formatted p$1 ($4, $3 sectors)"
}
fmt_part 1 "$P1_START" "$P1_SECTORS" OCS_BOOT
fmt_part 2 "$P2_START" "$P2_SECTORS" OCS_A
fmt_part 3 "$P3_START" "$P3_SECTORS" OCS_B
fmt_part 4 "$P4_START" "$P4_SECTORS" OCS_DATA

# ---- 3. Seed the flag partition with the disk grub.cfg ----------------
pull_part 1 "$P1_START" "$P1_SECTORS"
mmd -i "$OUT.p1" ::/boot
mmd -i "$OUT.p1" ::/boot/grub
mcopy -i "$OUT.p1" "$GRUB_CFG" ::/boot/grub/grub.cfg

# BUG-0305: pre-create the GRUB environment block (1024 bytes, canonical
# "# GRUB Environment Block\n" header + ab_attempts=0 + '#' padding).
# GRUB's save_env can only write pre-existing files, and the kernel resets
# the counter in place by rewriting the same 1024 bytes.
python3 -c '
hdr = b"# GRUB Environment Block\n"
blk = hdr + b"ab_attempts=0\n"
blk += b"#" * (1024 - len(blk))
assert len(blk) == 1024
open("/tmp/ab_grubenv", "wb").write(blk)
'
mcopy -i "$OUT.p1" /tmp/ab_grubenv ::/boot/grub/grubenv
rm -f /tmp/ab_grubenv

# BUG-0305: the disk must boot on its own GRUB (the selection logic with
# the attempt counter lives in /boot/grub/grub.cfg on partition 1). Embed
# a core.img in the post-MBR gap and install the boot code, so the image
# is self-bootable in QEMU/real hardware without an external ISO.
GRUBDIR="${OC_TOOLS:-/home/z/opt/extract}/usr/lib/grub/i386-pc"
if [ -d "$GRUBDIR" ]; then
  mmd -i "$OUT.p1" ::/boot/grub/i386-pc || true
  mcopy -s -i "$OUT.p1" "$GRUBDIR"/*.mod ::/boot/grub/i386-pc/ || true
  mcopy -i "$OUT.p1" "$GRUBDIR/partmap.lst" ::/boot/grub/i386-pc/ || true
  mcopy -i "$OUT.p1" "$GRUBDIR/command.lst" ::/boot/grub/i386-pc/ || true
  mcopy -i "$OUT.p1" "$GRUBDIR/fs.lst" ::/boot/grub/i386-pc/ || true
  mcopy -i "$OUT.p1" "$GRUBDIR/terminal.lst" ::/boot/grub/i386-pc/ || true
  # early embedded config: bring serial up BEFORE normal.mod loads, so
  # disk-boot diagnostics are visible on the serial console (same shape as
  # the ISO's grub setup).
  printf 'serial --unit=0 --speed=115200\nterminal_output serial console\nnormal\n' > "$OUT.early.cfg"
  grub-mkimage -O i386-pc -d "$GRUBDIR" \
    -p '(hd0,msdos1)/boot/grub' \
    -c "$OUT.early.cfg" \
    -o "$OUT.core.img" \
    biosdisk part_msdos fat multiboot2 loadenv echo test ls serial terminal normal configfile
  rm -f "$OUT.early.cfg"
  python3 - "$OUT" "$OUT.core.img" "$GRUBDIR/boot.img" <<'PYEOF2'
import struct, sys
img, corep, bootimg = sys.argv[1], sys.argv[2], sys.argv[3]
# Read the STOCK boot.img from the grub module dir - NOT the current MBR
# (a zero-filled placeholder: reusing it would install no boot code and
# SeaBIOS would hang right after "Booting from Hard Disk").
boot = bytearray(open(bootimg, "rb").read(512))
core = open(corep, "rb").read()
if len(core) % 512:
    core += b"\x00" * (512 - len(core) % 512)
assert len(core) // 512 + 1 <= 2047, "core.img must fit the post-MBR gap"
boot[92:100] = struct.pack("<Q", 1)
with open(img, "r+b") as f:
    f.seek(0)
    f.write(bytes(boot[:446]))
    f.seek(512)
    f.write(core)
print("[make_ab_disk] GRUB core.img embedded: %d bytes at LBA 1" % len(core))
PYEOF2
  rm -f "$OUT.core.img"
  echo "[make_ab_disk] GRUB i386-pc stages installed (self-booting disk)"
else
  echo "[make_ab_disk] WARNING: $GRUBDIR not found - disk NOT self-bootable"
fi

push_part 1 "$P1_START" "$P1_SECTORS"
echo "[make_ab_disk] p1: /boot/grub/grub.cfg + grubenv + i386-pc installed"

# ---- 4. Optionally seed slot A with the current kernel ----------------
KERNEL="${1:-}"
if [ -n "$KERNEL" ] && [ -f "$KERNEL" ]; then
  pull_part 2 "$P2_START" "$P2_SECTORS"
  mmd -i "$OUT.p2" ::/boot
  mcopy -i "$OUT.p2" "$KERNEL" ::/boot/opencube.elf
  push_part 2 "$P2_START" "$P2_SECTORS"
  echo "[make_ab_disk] p2 (slot A): /boot/opencube.elf seeded from $KERNEL"
fi

# ---- 5. Verify --------------------------------------------------------
echo "[make_ab_disk] verification:"
pull_part 1 "$P1_START" "$P1_SECTORS"
echo "--- p1 (boot/flags) ---"
mdir -i "$OUT.p1" ::/ 2>&1 | tail -4
mdir -i "$OUT.p1" ::/boot/grub 2>&1 | tail -4
rm -f "$OUT.p1"
pull_part 2 "$P2_START" "$P2_SECTORS"
echo "--- p2 (slot A) ---"
mdir -i "$OUT.p2" ::/ 2>&1 | tail -4
mdir -i "$OUT.p2" ::/boot 2>&1 | tail -4
rm -f "$OUT.p2"
pull_part 3 "$P3_START" "$P3_SECTORS"
echo "--- p3 (slot B) ---"
mdir -i "$OUT.p3" ::/ 2>&1 | tail -4
rm -f "$OUT.p3"
pull_part 4 "$P4_START" "$P4_SECTORS"
echo "--- p4 (data) ---"
mdir -i "$OUT.p4" ::/ 2>&1 | tail -4
rm -f "$OUT.p4"
ls -la "$OUT"
echo "[make_ab_disk] done: $OUT"
