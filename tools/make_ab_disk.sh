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

P1_START=2048                                            # 1 MiB align
P1_SECTORS=$((64 * 1024 * 1024 / 512))                   # 64 MiB
P2_SECTORS=$((128 * 1024 * 1024 / 512))                  # 128 MiB
P3_SECTORS=$((128 * 1024 * 1024 / 512))                  # 128 MiB
P4_SECTORS=$((128 * 1024 * 1024 / 512))                  # 128 MiB
P2_START=$((P1_START + P1_SECTORS))
P3_START=$((P2_START + P2_SECTORS))
P4_START=$((P3_START + P3_SECTORS))
TOTAL_SECTORS=$((P4_START + P4_SECTORS))

echo "[make_ab_disk] image: $OUT (${TOTAL_SECTORS} sectors)"
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
push_part 1 "$P1_START" "$P1_SECTORS"
echo "[make_ab_disk] p1: /boot/grub/grub.cfg installed"

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
