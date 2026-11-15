#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - 3.0.0-WP02-ocire_recovery
# File: tools/make_ocire_disk.sh
# Purpose: build the WP-02 dual-boot test disks:
#
#   build/ocire/ocire-boot.img (hd0, 128 MiB) - the GRUB dual-boot
#       disk.  MBR p1 FAT32 (from LBA 2048) holding the OCOS system
#       volume (the REPAIR TARGET):
#         /boot/opencube.elf     - the OCOS kernel (multiboot2)
#         /boot/ocire.elf        - copy of the OCIRE kernel
#         /boot/grub/grub.cfg    - the generated 3-entry menu
#         /boot/grub/fonts/unicode.pf2 (gfxterm font)
#         /EFI/BOOT/BOOTX64.EFI  - UEFI GRUB (same menu)
#       BIOS boot: GRUB boot.img in the MBR + core.img in the
#       post-MBR gap (LBA 1..2047, same plain-dd install WP-01
#       verified).
#
#   build/ocire/ocire-hd.img (hd1, 16 MiB) - the OCIRE recovery disk.
#       MBR with ONE type-0xEE shadow entry and NO GPT header (the
#       WP-01 partition isolation), holding a FAT12 volume with:
#         /boot/ocire.elf    - the boot kernel; GRUB loads it with a
#                              whole-disk blocklist (see grub-dual.cfg.in)
#         /golden/...       - golden OCOS system image + MANIFEST.TXT
#                              (SHA-256 per file; repair source; 5 files:
#                              OPENCUBE.ELF, OCIRE.ELF, GRUB.CFG,
#                              UNICODE.PF2, BOOTX64.EFI)
#         /agent/agent.elf  - the ring-3 log agent (kexec injection)
#         /log/boot.log     - 64 KiB fixed-size ring (sector 0 =
#                              header, data after; the OCOS agent
#                              appends through the kernel channel)
#         /mailbox.bin      - 128-byte boot-signal mailbox (see
#                              ocire/ocire_disk.h)
#
#       The script computes the OCIRE.ELF blocklist LBA and the log
#       ring LBA from the REAL FAT12 structures (python parses the
#       BPB + directory), verifies the kernel file is CONTIGUOUS, and
#       initializes the mailbox (magic, NONE state, log LBA, agent
#       SHA-256) so the kernels only ever patch fields in place.

set -e

OC_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TOOLBIN="$(cd "${OC_TOOLS:-/home/z/opt/extract}/usr/bin" && pwd)"
export PATH="$TOOLBIN:$PATH"
GRUB_DIR="${OC_TOOLS:-/home/z/opt/extract}/usr/lib/grub"
GRUB_SHARE="${OC_TOOLS:-/home/z/opt/extract}/usr/share/grub"
export LD_LIBRARY_PATH="${OC_TOOLS:-/home/z/opt/extract}/usr/lib/x86_64-linux-gnu:${OC_TOOLS:-/home/z/opt/extract}/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

BUILD="$OC_ROOT/build/ocire"
OCOS_ELF="$OC_ROOT/build/opencube.elf"
OCIRE_ELF="$BUILD/ocire.elf"
AGENT_ELF="$BUILD/agent.elf"
HD0="$BUILD/ocire-boot.img"
HD1="$BUILD/ocire-hd.img"

[ -f "$OCOS_ELF" ]  || { echo "[ocire_disk] FATAL: $OCOS_ELF missing (make kernel)" >&2; exit 1; }
[ -f "$OCIRE_ELF" ] || { echo "[ocire_disk] FATAL: $OCIRE_ELF missing (make ocire-kernel)" >&2; exit 1; }
[ -f "$AGENT_ELF" ] || { echo "[ocire_disk] FATAL: $AGENT_ELF missing (make ocire-agent)" >&2; exit 1; }
[ -f "$GRUB_SHARE/unicode.pf2" ] || { echo "[ocire_disk] FATAL: $GRUB_SHARE/unicode.pf2 missing" >&2; exit 1; }

# ----------------------------------------------------------------------
# ----------------------------------------------------------------------
# hd1: the OCIRE recovery disk - WP-07 DUAL-INDEPENDENT-PARTITION edition
#
#   LBA 0        OCBL BIOS image (stage1_mbr + core): the disk boots
#                itself straight into the loader; the loader's A/B
#                scan picks the slot from the flags sector
#   LBA 1024     the OCIREAB1 A/B flags sector (active/try/attempts)
#   p1  (LBA 2048, 64 MiB)  OCIRE-A  - a FULL recovery environment
#   p2  (LBA 133120, 64 MiB) OCIRE-B - a FULL recovery environment
#
#   Both slots are REAL physical partitions carrying the complete WP-02
#   file set (boot kernel, golden image, log agent, log ring, mailbox);
#   slot B starts as a byte-identical copy of slot A.  The 0xEE/FAT12
#   single-shadow layout of WP-02..WP-06 is retired on this disk (the
#   OCIRE reader still mounts old media for compatibility).
# ----------------------------------------------------------------------
HD1_START=2048
HD1_SLOT_SECTORS=$((64*1024*1024/512))          # 64 MiB per slot
HD1_SECTORS=$((2*HD1_SLOT_SECTORS + HD1_START)) # p1 + p2
echo "[ocire_disk] hd1: $HD1 (dual independent OCIRE-A/B partitions, FAT32 slots + OCBL in the MBR gap)"
rm -f "$HD1"
truncate -s $((HD1_SECTORS * 512)) "$HD1"

# ---- 1. the OCBL BIOS image at LBA 0 (the disk is self-booting) -----
#        [ stage1_mbr 512B ][ core ] - the stage1 keeps its boot facts
#        in the +0x1A0 patch block (ocbl-install contract): core LBA
#        (1, right behind stage1) + sector count.  The MBR partition
#        table is written AFTER the image so it SURVIVES the stage1
#        bytes (the same preserve-446..509 discipline ocbl-install
#        uses on real targets).
OCBL_MBR="$BUILD/.hd1.ocbl-mbr.img"
if [ ! -f "$OC_ROOT/build/ocbl/stage1_mbr.bin" ] || \
   [ ! -f "$OC_ROOT/build/ocbl/core.bin" ]; then
    echo "[ocire_disk] FATAL: build/ocbl/stage1_mbr.bin or core.bin missing (make ocbl-bios)" >&2
    exit 1
fi
python3 "$OC_ROOT/ocbl/build_image.py" "$OCBL_MBR" \
    "$OC_ROOT/build/ocbl/stage1_mbr.bin" "$OC_ROOT/build/ocbl/core.bin"
dd if="$OCBL_MBR" of="$HD1" bs=512 conv=notrunc status=none
OCBL_SECTORS=$(( ($(stat -c%s "$OCBL_MBR") + 511) / 512 ))
if [ "$OCBL_SECTORS" -gt 1024 ]; then
    echo "[ocire_disk] FATAL: OCBL image ($OCBL_SECTORS sectors) would overrun the flags sector at LBA 1024" >&2
    exit 1
fi

# ---- 2. MBR: OCBL stage1 + the two FAT32 partitions + the boot patch -
python3 - "$HD1" "$HD1_START" "$HD1_SLOT_SECTORS" "$OCBL_SECTORS" <<'MBREOF'
import struct, sys
path, start, slot, ocbl_sectors = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
with open(path, "r+b") as f:
    mbr = bytearray(f.read(512))
    for i in range(2):
        off = 446 + i * 16
        mbr[off + 0] = 0x00
        mbr[off + 1: off + 4] = b"\x20\x21\x00"
        mbr[off + 4] = 0x0C                    # FAT32 (LBA)
        mbr[off + 5: off + 8] = b"\xfe\xff\xff"
        mbr[off + 8: off + 12] = struct.pack("<I", start + i * slot)
        mbr[off + 12: off + 16] = struct.pack("<I", slot)
    mbr[510:512] = b"\x55\xAA"
    # the +0x1A0 stage1 boot patch block: 'OCBL' + core LBA (lo/hi)
    # + core sector count (the core sits right behind stage1 at LBA 1)
    struct.pack_into("<I", mbr, 0x1A0, 0x4C42434F)          # 'OCBL'
    struct.pack_into("<I", mbr, 0x1A4, 1)                   # core LBA lo
    struct.pack_into("<I", mbr, 0x1A8, 0)                   # core LBA hi
    struct.pack_into("<H", mbr, 0x1AC, ocbl_sectors - 1)    # core sectors
    f.seek(0)
    f.write(bytes(mbr))
print("[ocire_disk] hd1 MBR: OCBL stage1 + p1=OCIRE-A LBA %d, p2=OCIRE-B LBA %d "
      "(%d sectors each); core at LBA 1, %d sectors"
      % (start, start + slot, slot, ocbl_sectors - 1))
MBREOF
echo "[ocire_disk] hd1 OCBL image: $OCBL_SECTORS sectors at LBA 0 (flags live at LBA 1024)"

# ---- 3. slot A: the FAT32 volume with the full recovery file set ----
# 1 sector/cluster keeps the cluster count >= 65525 (real FAT32).
P1PULL="$BUILD/.hd1.slotA"
dd if=/dev/zero of="$P1PULL" bs=512 count=$HD1_SLOT_SECTORS status=none
mformat -i "$P1PULL" -F -s 1 -v OCIRE_A ::

mmd  -i "$P1PULL" ::/boot ::/golden ::/agent ::/log
# The boot kernel goes in FIRST (fresh volume -> contiguous clusters,
# which the GRUB blocklist requires; verified below).
mcopy -i "$P1PULL" "$OCIRE_ELF" ::/boot/ocire.elf
mcopy -i "$P1PULL" "$OCOS_ELF"  ::/golden/opencube.elf
mcopy -i "$P1PULL" "$OCIRE_ELF" ::/golden/ocire.elf
mcopy -i "$P1PULL" "$AGENT_ELF" ::/agent/agent.elf

# ---- golden manifest (SHA-256 + size per OCOS system file) ----
GOLDEN_CFG="$BUILD/.golden.grub.cfg"      # filled after hd0 exists; placeholder order kept
sha256_of() { sha256sum "$1" | cut -d' ' -f1; }

# ---- the log ring: 64 KiB, sector 0 = header ----
python3 - "$P1PULL" <<'LOGEOF'
import struct, sys
img = sys.argv[1]
buf = bytearray(b"\x00" * (128 * 512))
buf[0:9] = b"OCIRELOG1"
struct.pack_into("<I", buf, 9, 0)        # write_off (bytes past header sector)
struct.pack_into("<I", buf, 13, 0)       # boot_seq
open(img + ".logring", "wb").write(buf)
print("[ocire_disk] log ring staged (128 sectors)")
LOGEOF
mcopy -i "$P1PULL" "$P1PULL.logring" ::/log/boot.log
rm -f "$P1PULL.logring"

# ---- FAT32 layout facts the kernels/GRUB need (real-structure parse) ----
FAT32_JSON="$BUILD/.hd1.layout.json"
python3 - "$P1PULL" "$FAT32_JSON" <<'FACTEOF'
import json, struct, sys
img, out = sys.argv[1], sys.argv[2]
f = open(img, "rb")
bpb = f.read(512)
bps = struct.unpack_from("<H", bpb, 11)[0]
spc = bpb[13]
reserved = struct.unpack_from("<H", bpb, 14)[0]
nfats = bpb[16]
fatsz = struct.unpack_from("<I", bpb, 36)[0]
rootclus = struct.unpack_from("<I", bpb, 44)[0]
totsec = struct.unpack_from("<I", bpb, 32)[0]
assert bps == 512 and spc == 1 and nfats == 2 and fatsz and rootclus >= 2, \
       "unexpected FAT32 geometry (bps=%d spc=%d nfats=%d fatsz=%d rootclus=%d)" \
       % (bps, spc, nfats, fatsz, rootclus)
data_start = reserved + nfats * fatsz
total_clusters = (totsec - data_start) // spc
assert total_clusters >= 65525, "not FAT32: %d clusters" % total_clusters

fat_start = reserved
f.seek(fat_start * 512)
fat = f.read(fatsz * 512)
def fat32(n):
    return struct.unpack_from("<I", fat, n * 4)[0] & 0x0FFFFFFF

def cluster_chain(first):
    chain = []
    c = first
    while 2 <= c < 0x0FFFFFF8:
        chain.append(c)
        c = fat32(c)
    return chain

def read_cluster(c):
    f.seek((data_start + (c - 2) * spc) * 512)
    return f.read(spc * 512)

def dir_of(cluster):
    ents = []
    for c in cluster_chain(cluster):
        data = read_cluster(c)
        for o in range(0, len(data), 32):
            e = data[o:o+32]
            if e[0] in (0x00, 0xE5):
                continue
            if (e[11] & 0x3F) == 0x0F:
                continue
            ents.append(e)
    return ents

def find_in_dir(cluster, name):
    for e in dir_of(cluster):
        if e[0:11].upper() == name:
            return e
    return None

def file_facts(root_cluster, parts):
    e = find_in_dir(root_cluster, parts[0])
    for part in parts[1:]:
        assert e is not None, "dir not found: %s" % (parts,)
        first = struct.unpack_from("<H", e, 26)[0] | \
                (struct.unpack_from("<H", e, 20)[0] << 16)
        e = find_in_dir(first, part)
    assert e is not None, "not found: %s" % (parts,)
    first = struct.unpack_from("<H", e, 26)[0] | \
            (struct.unpack_from("<H", e, 20)[0] << 16)
    size = struct.unpack_from("<I", e, 28)[0]
    chain = cluster_chain(first)
    return first, size, chain

facts = {}
for label, parts in [("kernel", (b"BOOT       ", b"OCIRE   ELF")),
                     ("log", (b"LOG        ", b"BOOT    LOG"))]:
    first, size, chain = file_facts(rootclus, parts)
    lba = data_start + (first - 2) * spc
    contiguous = (chain == list(range(chain[0], chain[0] + len(chain))))
    facts[label] = {"first_cluster": first, "size": size, "clusters": len(chain),
                    "lba": lba, "contiguous": contiguous,
                    "sectors": (size + 511) // 512}
    print("[ocire_disk] hd1-A %s: cluster %d, %d bytes, %d clusters, contiguous=%s"
          % (label, first, size, len(chain), contiguous))

afirst, asize, achain = file_facts(rootclus, (b"AGENT      ", b"AGENT   ELF"))
acontig = (achain == list(range(achain[0], achain[0] + len(achain))))
assert acontig, "agent.elf fragmented (fresh volume: should not happen)"
facts["agent"] = {"first_cluster": afirst, "size": asize, "clusters": len(achain)}

assert facts["kernel"]["contiguous"], "ocire.elf fragmented on the FAT32 slot (blocklist would fail)"
assert facts["log"]["contiguous"], "boot.log fragmented on the FAT32 slot"

facts["geometry"] = {"data_start": data_start, "spc": spc,
                     "total_clusters": total_clusters, "root_cluster": rootclus}
json.dump(facts, open(out, "w"), indent=1)
print("[ocire_disk] hd1-A layout facts -> %s" % out)
FACTEOF

KERN_LBA=$(python3 -c "import json;d=json.load(open('$FAT32_JSON'));print(d['kernel']['lba'] + $HD1_START)")
KERN_SECTORS=$(python3 -c "import json;d=json.load(open('$FAT32_JSON'));print(d['kernel']['sectors'])")
LOG_LBA=$(python3 -c "import json;d=json.load(open('$FAT32_JSON'));print(d['log']['lba'] + $HD1_START)")
AGENT_SHA=$(sha256_of "$AGENT_ELF")
echo "[ocire_disk] hd1-A blocklist: LBA $KERN_LBA + $KERN_SECTORS sectors; log ring at LBA $LOG_LBA"

if [ "$KERN_SECTORS" -gt 1024 ]; then
    echo "[ocire_disk] FATAL: OCIRE ELF ($KERN_SECTORS sectors) exceeds the 1024-sector blocklist window" >&2
    exit 1
fi

# ---- write the mailbox (magic, NONE state, log LBA, agent SHA-256) ----
python3 - "$P1PULL" "$LOG_LBA" "$AGENT_SHA" <<'MBEOF'
import binascii, struct, sys
img, log_lba, agent_sha = sys.argv[1], int(sys.argv[2]), sys.argv[3]
mb = bytearray(128)
mb[0:8] = b"OCIREMB2"
struct.pack_into("<I", mb, 8, 0)        # state = NONE
struct.pack_into("<I", mb, 12, 0)       # boot_count
struct.pack_into("<I", mb, 16, 0)       # fail_count
struct.pack_into("<I", mb, 20, log_lba) # log ring LBA (absolute)
struct.pack_into("<I", mb, 24, 128)     # log sectors
mb[28:60] = binascii.unhexlify(agent_sha)
# #52: ver=1 + ones-complement checksum (words 0..63 skipping the
# cksum pair at words 32/33) - mirrors ocire_mb_checksum().
struct.pack_into("<I", mb, 60, 1)       # ver = 1
s = 0
for i in range(64):
    if i in (32, 33):
        continue
    s += struct.unpack_from("<H", mb, i * 2)[0]
    if s > 0xFFFF:
        s = (s & 0xFFFF) + 1
struct.pack_into("<I", mb, 64, (~s) & 0xFFFF)
open(img + ".mb", "wb").write(bytes(mb))
print("[ocire_disk] mailbox staged (state=NONE, log_lba=%d, ver=1)" % log_lba)
MBEOF
mcopy -i "$P1PULL" "$P1PULL.mb" ::/mailbox.bin
rm -f "$P1PULL.mb"

# ---- 4. (slots are written AFTER the golden stage below adds the
#         manifest + menu to the pulled slot-A image; both slots then
#         receive the identical full content) -------------------------

# ---- 5. the A/B flags sector at LBA 1024 (active=A, no try) ---------
python3 - "$HD1" <<'ABEOF'
import struct, sys
path = sys.argv[1]
sec = bytearray(512)
sec[0:8] = b"OCIREAB1"
sec[8] = 1      # active_slot = A
sec[9] = 0      # try_slot = none
sec[10] = 0     # attempts
sec[11] = 0     # flags
struct.pack_into("<H", sec, 12, 0)     # boot_count
struct.pack_into("<H", sec, 14, 0)     # rsvd
# ones-complement sum of the 20 payload bytes as LE u16 words
s = 0
for i in range(10):
    s += struct.unpack_from("<H", sec, i * 2)[0]
struct.pack_into("<I", sec, 16, (~s) & 0xFFFF)
with open(path, "r+b") as f:
    f.seek(1024 * 512)
    f.write(bytes(sec))
print("[ocire_disk] hd1 A/B flags written (active=A, try=none) at LBA 1024")
ABEOF


# ----------------------------------------------------------------------
# hd0: dual-boot GRUB disk (128 MiB, p1 FAT32 from LBA 2048)
# ----------------------------------------------------------------------
P1_START=2048
P1_SECTORS=$((128*1024*1024/512 - P1_START))
GAP_SECTORS=$((P1_START - 1))

echo "[ocire_disk] hd0: $HD0 (${P1_SECTORS} sectors FAT32)"
rm -f "$HD0"
truncate -s $(( (P1_START + P1_SECTORS) * 512 )) "$HD0"

python3 - "$HD0" "$P1_START" "$P1_SECTORS" <<'PYEOF'
import struct, sys
path, start, sectors = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
mbr = bytearray(512)
e = 446
mbr[e+0] = 0x80                    # bootable
mbr[e+1: e+4] = b"\x20\x21\x00"
mbr[e+4] = 0x0C                    # FAT32 (LBA)
mbr[e+5: e+8] = b"\xfe\xff\xff"
mbr[e+8: e+12] = struct.pack("<I", start)
mbr[e+12: e+16] = struct.pack("<I", sectors)
mbr[510:512] = b"\x55\xAA"
with open(path, "r+b") as f:
    f.write(bytes(mbr))
print("[ocire_disk] hd0 MBR written (p1 FAT32 LBA %d, %d sectors)" % (start, sectors))
PYEOF

# ---- p1 FAT32 (the OCOS system volume, repair target) ----
P0PULL="$BUILD/.hd0.p1"
dd if="$HD0" of="$P0PULL" bs=512 skip=$P1_START count=$P1_SECTORS status=none
mformat -i "$P0PULL" -F -v OCOS_SYS ::
mmd  -i "$P0PULL" ::/boot ::/boot/grub ::/boot/grub/fonts ::/EFI ::/EFI/BOOT
mcopy -i "$P0PULL" "$OCOS_ELF"  ::/boot/opencube.elf
mcopy -i "$P0PULL" "$OCIRE_ELF" ::/boot/ocire.elf
mcopy -i "$P0PULL" "$GRUB_SHARE/unicode.pf2" ::/boot/grub/fonts/unicode.pf2

# ---- the menu (template + blocklist) ----
sed -e "s/@KERN_LBA@/$KERN_LBA/g" -e "s/@KERN_SECTORS@/$KERN_SECTORS/g" \
    "$OC_ROOT/ocire/grub-dual.cfg.in" > "$BUILD/.grub-dual.cfg"
mcopy -i "$P0PULL" "$BUILD/.grub-dual.cfg" ::/boot/grub/grub.cfg

# ---- UEFI GRUB (BOOTX64.EFI, gfxterm video chain embedded) ----
grub-mkimage -O x86_64-efi \
  -d "$GRUB_DIR/x86_64-efi" \
  -p '(hd0,msdos1)/boot/grub' \
  -o "$BUILD/.BOOTX64.EFI" \
  fat part_msdos multiboot2 boot normal configfile echo terminal serial test \
  efi_gop fixvideo video video_fb gfxterm font bitmap bufio all_video \
  video_bochs video_cirrus video_colors
mcopy -i "$P0PULL" "$BUILD/.BOOTX64.EFI" ::/EFI/BOOT/BOOTX64.EFI

dd if="$P0PULL" of="$HD0" bs=512 seek=$P1_START count=$P1_SECTORS conv=notrunc status=none

# ---- BIOS GRUB: boot.img (MBR) + core.img (post-MBR gap, LBA 1) ----
MODS_BIOS="biosdisk part_msdos fat multiboot2 boot normal configfile echo terminal serial test vbe vga video video_fb gfxterm font bitmap bufio all_video video_bochs video_cirrus video_colors"
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
dd if="$GRUB_DIR/i386-pc/boot.img" of="$HD0" bs=446 count=1 conv=notrunc status=none
dd if="$BUILD/.core.img" of="$HD0" bs=512 seek=1 conv=notrunc status=none
echo "[ocire_disk] hd0 GRUB installed: boot.img (MBR) + core.img (LBA1, $CORE_SECTORS sectors)"

# ----------------------------------------------------------------------
# golden image on hd1: OPENCUBE.ELF + GRUB.CFG (the exact menu file) +
# BOOTX64.EFI + MANIFEST.TXT
# ----------------------------------------------------------------------
mcopy -i "$P1PULL" "$BUILD/.grub-dual.cfg"      ::/golden/grub.cfg
mcopy -i "$P1PULL" "$BUILD/.BOOTX64.EFI"        ::/golden/bootx64.efi
mcopy -i "$P1PULL" "$GRUB_SHARE/unicode.pf2"    ::/golden/unicode.pf2

{
  echo "$(sha256_of "$OCOS_ELF") $(stat -c%s "$OCOS_ELF") BOOT/OPENCUBE.ELF"
  echo "$(sha256_of "$OCIRE_ELF") $(stat -c%s "$OCIRE_ELF") BOOT/OCIRE.ELF"
  echo "$(sha256_of "$BUILD/.grub-dual.cfg") $(stat -c%s "$BUILD/.grub-dual.cfg") BOOT/GRUB/GRUB.CFG"
  echo "$(sha256_of "$GRUB_SHARE/unicode.pf2") $(stat -c%s "$GRUB_SHARE/unicode.pf2") BOOT/GRUB/FONTS/UNICODE.PF2"
  echo "$(sha256_of "$BUILD/.BOOTX64.EFI") $(stat -c%s "$BUILD/.BOOTX64.EFI") EFI/BOOT/BOOTX64.EFI"
} > "$BUILD/.manifest.txt"
mcopy -i "$P1PULL" "$BUILD/.manifest.txt" ::/golden/manifest.txt

# WP-07: BOTH independent partitions receive the identical slot image
# (slot A into p1, the byte-identical copy into p2).
dd if="$P1PULL" of="$HD1" bs=512 seek=$HD1_START count=$HD1_SLOT_SECTORS conv=notrunc status=none
dd if="$P1PULL" of="$HD1" bs=512 seek=$((HD1_START + HD1_SLOT_SECTORS)) count=$HD1_SLOT_SECTORS conv=notrunc status=none
echo "[ocire_disk] hd1 slots A+B written (identical full recovery environments)"

rm -f "$P1PULL" "$P0PULL" "$FAT32_JSON" "$BUILD/.core.img" "$BUILD/.BOOTX64.EFI" \
      "$BUILD/.grub-dual.cfg" "$BUILD/.manifest.txt" "$OCBL_MBR"
echo "[ocire_disk] done:"
ls -la "$HD0" "$HD1"
