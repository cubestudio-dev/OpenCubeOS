#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
# Open Cube OS - 3.0.0-WP07-ocire-loop-perfect
# File: tools/make_esp_disk.sh
# Purpose: build the WP-07 ESP test disk (build/ocire/esp.img).
#
#          A real GPT disk with one EFI System Partition:
#            LBA 0      protective MBR (type 0xEE)
#            LBA 1      GPT header
#            LBA 2..33  GPT entry array (128 entries)
#            p1 (LBA 2048, 32 MiB)  the ESP, FAT32:
#              /EFI/BOOT/BOOTX64.EFI          (a generic loader stub)
#              /EFI/OpenCube/ocire-update.bin (LFN! the update drop
#                                             zone, signed by the
#                                             host tool)
#              /EFI/OpenCube/ocire-update.sig
#
#          Usage: make_esp_disk.sh [--pkg bin sig]   (inject a package)
set -e

OC_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TOOLBIN="$(cd "${OC_TOOLS:-/home/z/opt/extract}/usr/bin" && pwd)"
export PATH="$TOOLBIN:$PATH"

BUILD="$OC_ROOT/build/ocire"
ESP="$BUILD/esp.img"
ESP_START=2048
ESP_SECTORS=$((64*1024*1024/512))
TOTAL_SECTORS=$((ESP_START + ESP_SECTORS + 2048))     # + GPT backup room

echo "[esp] building $ESP (GPT + one ESP, $((ESP_SECTORS/2048)) MiB)"
rm -f "$ESP"
truncate -s $((TOTAL_SECTORS * 512)) "$ESP"

# ---- protective MBR + GPT header + entry array (python, deterministic) ----
python3 - "$ESP" "$ESP_START" "$ESP_SECTORS" "$TOTAL_SECTORS" <<'GPT'
import struct, sys
path, start, sectors, total = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])

# protective MBR
mbr = bytearray(512)
e = 446
mbr[e+0] = 0x00
mbr[e+1: e+4] = b"\x00\x02\x00"
mbr[e+4] = 0xEE
mbr[e+5: e+8] = b"\xff\xff\xff"
mbr[e+8: e+12] = struct.pack("<I", 1)
mbr[e+12: e+16] = struct.pack("<I", max(total - 1, 0xFFFFFFFF))
mbr[510:512] = b"\x55\xAA"

# GPT header at LBA 1
hdr = bytearray(512)
hdr[0:8] = b"EFI PART"
struct.pack_into("<I", hdr, 8, 0x00010000)      # revision
struct.pack_into("<I", hdr, 12, 92)             # header size (no crc fields)
struct.pack_into("<I", hdr, 16, 0)              # crc (patched below)
struct.pack_into("<Q", hdr, 24, 1)              # current LBA
struct.pack_into("<Q", hdr, 32, total - 1)      # backup LBA
struct.pack_into("<Q", hdr, 40, start)          # first usable
struct.pack_into("<Q", hdr, 48, start + sectors - 1)   # last usable
hdr[56:72] = bytes.fromhex("12345678123456781234567812345678")  # disk GUID
struct.pack_into("<Q", hdr, 72, 2)              # entry array LBA
struct.pack_into("<I", hdr, 80, 128)            # 128 entries
struct.pack_into("<I", hdr, 84, 128)            # entry size
# entries CRC patched below

# entry array at LBA 2: entry 0 = the ESP
ents = bytearray(128 * 128)
ent = memoryview(ents)[:128]      # a VIEW: writes land in ents
# EFI System Partition GUID C12A7328-F81F-11D2-BA4B-00A0C93EC93B in
# the on-disk mixed-endian form
ent[0:16] = bytes([0x28,0x73,0x2A,0xC1, 0x1F,0xF8, 0xD2,0x11,
                   0xBA,0x4B, 0x00,0xA0,0xC9,0x3E,0xC9,0x3B])
ent[16:32] = bytes.fromhex("87654321876543218765432187654321")  # unique GUID
struct.pack_into("<Q", ent, 32, start)
struct.pack_into("<Q", ent, 40, start + sectors - 1)
struct.pack_into("<Q", ent, 48, 0)              # attrs
name = "EFI system partition"
for i, ch in enumerate(name):
    struct.pack_into("<H", ent, 56 + i * 2, ord(ch))

# CRC32 (zlib poly 0x04C11DB7 reflected as 0xEDB88320)
import zlib
ents_crc = zlib.crc32(bytes(ents)) & 0xFFFFFFFF
struct.pack_into("<I", hdr, 88, ents_crc)
hdr_crc = zlib.crc32(bytes(hdr)) & 0xFFFFFFFF
struct.pack_into("<I", hdr, 16, hdr_crc)

with open(path, "r+b") as f:
    f.write(bytes(mbr))
    f.seek(512)
    f.write(bytes(hdr))
    f.seek(2 * 512)
    f.write(bytes(ents))
print("[esp] GPT written: ESP at LBA %d (%d sectors)" % (start, sectors))
GPT

# ---- the ESP FAT32 volume ----
PULL="$BUILD/.esp.p1"
dd if=/dev/zero of="$PULL" bs=512 count=$ESP_SECTORS status=none
mformat -i "$PULL" -F -s 1 -v OC_ESP ::
mmd -i "$PULL" ::/EFI ::/EFI/BOOT ::/EFI/OpenCube

# a realistic generic loader stub so `ls` shows mainstream ESP shapes
printf 'EFI boot stub for the WP-07 ESP test disk\r\n' > "$BUILD/.esp.stub"
mcopy -i "$PULL" "$BUILD/.esp.stub" ::/EFI/BOOT/BOOTX64.EFI
rm -f "$BUILD/.esp.stub"

# ---- optional package injection (--pkg bin sig) ----
if [ "$1" = "--pkg" ]; then
    [ -f "$2" ] && [ -f "$3" ] || { echo "[esp] FATAL: package files missing" >&2; exit 1; }
    mcopy -i "$PULL" "$2" ::/EFI/OpenCube/ocire-update.bin
    mcopy -i "$PULL" "$3" ::/EFI/OpenCube/ocire-update.sig
    echo "[esp] package injected: $(basename "$2") ($(stat -c%s "$2") bytes) + sig"
fi

dd if="$PULL" of="$ESP" bs=512 seek=$ESP_START count=$ESP_SECTORS conv=notrunc status=none
rm -f "$PULL"
mdir -i "$ESP@@$ESP_START" ::/EFI/OpenCube 2>/dev/null || true
echo "[esp] done: $ESP"
