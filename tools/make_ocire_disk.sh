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
# hd1 first: the recovery disk (the boot disk's menu blocklists into it)
# ----------------------------------------------------------------------
HD1_START=2048
HD1_SECTORS=$((16*1024*1024/512 - HD1_START))
echo "[ocire_disk] hd1: $HD1 (single 0xEE shadow partition, FAT12 volume)"
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

# ---- the FAT12 volume inside the 0xEE shadow partition ----
# 8 KiB clusters keep the cluster count under 4085 (FAT12 range).
P1PULL="$BUILD/.hd1.p1"
dd if="$HD1" of="$P1PULL" bs=512 skip=$HD1_START count=$HD1_SECTORS status=none
mformat -i "$P1PULL" -c 16 -v OCIRE ::

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
python3 - "$P1PULL" <<'PYEOF'
import struct, sys
img = sys.argv[1]
# 128 sectors: header (16 bytes: magic, write_off, boot_seq, reserved) + data
buf = bytearray(b"\x00" * (128 * 512))
buf[0:9] = b"OCIRELOG1"
struct.pack_into("<I", buf, 9, 0)        # write_off (bytes past header sector)
struct.pack_into("<I", buf, 13, 0)       # boot_seq
from_mtools = None
open(img + ".logring", "wb").write(buf)
print("[ocire_disk] log ring staged (128 sectors)")
PYEOF
mcopy -i "$P1PULL" "$P1PULL.logring" ::/log/boot.log
rm -f "$P1PULL.logring"

# ---- the mailbox: filled after the log LBA is known ----

# ---- FAT12 layout facts the kernels/GRUB need (real-structure parse) ----
FAT12_JSON="$BUILD/.hd1.layout.json"
python3 - "$P1PULL" "$FAT12_JSON" <<'PYEOF'
import json, struct, sys
img, out = sys.argv[1], sys.argv[2]
f = open(img, "rb")
bpb = f.read(512)
bps = struct.unpack_from("<H", bpb, 11)[0]
spc = bpb[13]
reserved = struct.unpack_from("<H", bpb, 14)[0]
nfats = bpb[16]
rootent = struct.unpack_from("<H", bpb, 17)[0]
fatsz = struct.unpack_from("<H", bpb, 22)[0]
totsec = struct.unpack_from("<H", bpb, 19)[0]
assert bps == 512 and spc == 16 and nfats == 2 and rootent and fatsz, \
       "unexpected FAT12 geometry (bps=%d spc=%d nfats=%d rootent=%d fatsz=%d)" % (bps, spc, nfats, rootent, fatsz)
root_secs = (rootent * 32 + 511) // 512
fat_start = reserved
root_start = reserved + nfats * fatsz
data_start = root_start + root_secs
total_clusters = (totsec - data_start) // spc
assert total_clusters < 4085, "not FAT12: %d clusters" % total_clusters

# FAT12 entry read
f.seek(fat_start * 512)
fat = f.read(fatsz * 512)
def fat12(n):
    off = n + (n >> 1)
    v = struct.unpack_from("<H", fat, off)[0]
    v = (v >> 4) if (n & 1) else (v & 0xFFF)
    return v

def dir_entries(root_start_sector, root_secs):
    f.seek(root_start_sector * 512)
    data = f.read(root_secs * 512)
    for o in range(0, len(data), 32):
        e = data[o:o+32]
        if e[0] in (0x00, 0xE5):
            continue
        if (e[11] & 0x3F) == 0x0F:
            continue
        yield e

def find_in_root(name):
    # name like b"BOOT       " (11 bytes)
    for e in dir_entries(root_start, root_secs):
        if e[0:11].upper() == name:
            return e
    return None

def cluster_chain(first):
    chain = []
    c = first
    while 2 <= c < 0xFF8:
        chain.append(c)
        c = fat12(c)
    return chain

def file_facts(path_parts):
    e = find_in_root(path_parts[0])
    for part in path_parts[1:]:
        assert e is not None, "dir not found: %s" % (path_parts,)
        # descend
        first = struct.unpack_from("<H", e, 26)[0]
        sec = data_start + (first - 2) * spc
        f.seek(sec * 512)
        sub = f.read(spc * 512)
        nxt = None
        for o in range(0, len(sub), 32):
            se = sub[o:o+32]
            if se[0] in (0x00, 0xE5):
                continue
            if (se[11] & 0x3F) == 0x0F:
                continue
            if se[0:11].upper() == part:
                nxt = se
                break
        e = nxt
    assert e is not None, "not found: %s" % (path_parts,)
    first = struct.unpack_from("<H", e, 26)[0]
    size = struct.unpack_from("<I", e, 28)[0]
    chain = cluster_chain(first)
    return first, size, chain

facts = {}
for label, parts in [("kernel", (b"BOOT       ", b"OCIRE   ELF")),
                     ("log", (b"LOG        ", b"BOOT    LOG"))]:
    first, size, chain = file_facts(parts)
    # absolute LBA inside the partition image
    lba = data_start + (first - 2) * spc
    contiguous = (chain == list(range(chain[0], chain[0] + len(chain))))
    facts[label] = {"first_cluster": first, "size": size, "clusters": len(chain),
                    "lba": lba, "contiguous": contiguous,
                    "sectors": (size + 511) // 512}
    print("[ocire_disk] hd1 %s: cluster %d, %d bytes, %d clusters, contiguous=%s"
          % (label, first, size, len(chain), contiguous))

# agent facts (hash computed by the shell later; we need LBA none)
afirst, asize, achain = file_facts((b"AGENT      ", b"AGENT   ELF"))
acontig = (achain == list(range(achain[0], achain[0] + len(achain))))
assert acontig, "agent.elf is fragmented - blocklist path needs it contiguous (fresh volume: should not happen)"
facts["agent"] = {"first_cluster": afirst, "size": asize, "clusters": len(achain)}

assert facts["kernel"]["contiguous"], "ocire.elf fragmented on the FAT12 volume (blocklist would fail)"
assert facts["log"]["contiguous"], "boot.log fragmented on the FAT12 volume"

facts["geometry"] = {"data_start": data_start, "spc": spc,
                     "total_clusters": total_clusters}
json.dump(facts, open(out, "w"), indent=1)
print("[ocire_disk] hd1 layout facts -> %s" % out)
PYEOF

KERN_LBA=$(python3 -c "import json;d=json.load(open('$FAT12_JSON'));print(d['kernel']['lba'] + $HD1_START)")
KERN_SECTORS=$(python3 -c "import json;d=json.load(open('$FAT12_JSON'));print(d['kernel']['sectors'])")
LOG_LBA=$(python3 -c "import json;d=json.load(open('$FAT12_JSON'));print(d['log']['lba'] + $HD1_START)")
AGENT_SHA=$(sha256_of "$AGENT_ELF")
echo "[ocire_disk] hd1 blocklist: LBA $KERN_LBA + $KERN_SECTORS sectors; log ring at LBA $LOG_LBA"

if [ "$KERN_SECTORS" -gt 1024 ]; then
    echo "[ocire_disk] FATAL: OCIRE ELF ($KERN_SECTORS sectors) exceeds the 1024-sector blocklist window" >&2
    exit 1
fi

# ---- write the mailbox (magic, NONE state, log LBA, agent SHA-256) ----
python3 - "$P1PULL" "$LOG_LBA" "$AGENT_SHA" <<'PYEOF'
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
open(img + ".mb", "wb").write(bytes(mb))
print("[ocire_disk] mailbox staged (state=NONE, log_lba=%d)" % log_lba)
PYEOF
mcopy -i "$P1PULL" "$P1PULL.mb" ::/mailbox.bin
rm -f "$P1PULL.mb"

dd if="$P1PULL" of="$HD1" bs=512 seek=$HD1_START count=$HD1_SECTORS conv=notrunc status=none

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

dd if="$P1PULL" of="$HD1" bs=512 seek=$HD1_START count=$HD1_SECTORS conv=notrunc status=none

rm -f "$P1PULL" "$P0PULL" "$FAT12_JSON" "$BUILD/.core.img" "$BUILD/.BOOTX64.EFI" \
      "$BUILD/.grub-dual.cfg" "$BUILD/.manifest.txt"
echo "[ocire_disk] done:"
ls -la "$HD0" "$HD1"
