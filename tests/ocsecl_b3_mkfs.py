#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - tests/ocsecl_b3_mkfs.py (3.0.0-WP06b-ocsecl)
# Batch-3 acceptance: the format + mount command group.
#   mkfs.fat32 / mkfs.exfat / mkfs.ext4 with the BUG-0239 confirm
#   gate (refusal without `confirm`, mounted-device refusal), mount
#   with filesystem auto-detection (ext4 / fat32 / exFAT signatures),
#   umount, the partition-type sync (MBR byte + GPT GUID) and
#   HOST-SIDE structural verification of the freshly written volumes
#   (ext4 superblock/GDT/root-directory honesty, FAT32 BPB, MBR type
#   bytes) read back from the raw disk image.
# (The ext4 WRITE path itself gets its command surface with the batch-4
# file commands; this battery pins the on-disk formats + mount chain.)
import os
import struct
import sys
import time
import socket
import pexpect

BUILD = "/home/z/oc-public/build"
DISK = f"{BUILD}/ocse-b3-disk64.raw"
MON = f"{BUILD}/ocse-b3-mon.sock"
if os.path.exists(DISK):
    os.unlink(DISK)
with open(DISK, "wb") as f:
    f.write(b"\0" * (64 * 1024 * 1024))          # blank, zeroed
if os.path.exists(MON):
    os.unlink(MON)

QEMU = "qemu-system-x86_64"
CMD = (f"{QEMU} -L /home/z/opt/extract/usr/share/qemu "
       f"-L /home/z/opt/extract/usr/share/seabios -m 256M "
       f"-drive file={DISK},format=raw,if=ide,index=0,media=disk "
       f"-cdrom {BUILD}/ocse.iso -boot d "
       f"-no-reboot -display none -serial stdio "
       f"-monitor unix:{MON},server,nowait -vga std")


class Mon:
    def __init__(self, path, wait=20):
        for _ in range(wait * 2):
            try:
                self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                self.s.connect(path)
                self.s.settimeout(0.2)
                try:
                    while self.s.recv(4096):
                        pass
                except socket.timeout:
                    pass
                return
            except OSError:
                time.sleep(0.5)
        raise RuntimeError("monitor socket never came up")

    def cmd(self, c, settle=0.35):
        self.s.sendall((c + "\n").encode())
        time.sleep(settle)

    def type_line(self, text, settle=0.35):
        for ch in text:
            self.cmd(f"sendkey {self.keyname(ch)}", 0.02)
        self.cmd("sendkey ret", settle)

    @staticmethod
    def keyname(ch):
        special = {" ": "spc", ".": "dot", "-": "minus", "=": "equal",
                   "/": "slash", ";": "semicolon"}
        if ch in special:
            return special[ch]
        return ch


child = pexpect.spawn(CMD, timeout=180, encoding="utf-8", codec_errors="replace")
child.logfile = sys.stdout
mon = None
fails = []


def check(name, pattern, timeout=25):
    try:
        child.expect(pattern, timeout=timeout)
        print(f"\n[b3 OK] {name}")
    except Exception as e:
        print(f"\n[b3 FAIL] {name}: {e}")
        fails.append(name)


try:
    child.expect("environment up: GUI mode", timeout=120)
    mon = Mon(MON)
    mon.cmd("sendkey ctrl-alt-f2", 1.0)
    check("cli prompt", r"ocsecl>")

    # A: two partitions (p1 for ext4, p2 for fat/exfat)
    mon.type_line("mkpart hda 1 33")
    check("mkpart p1", "partition created: hdap1")
    mon.type_line("mkpart hda 33 64")
    check("mkpart p2", "partition created: hdap2")

    # B: the BUG-0239 gate - no confirm, nothing written
    mon.type_line("mkfs.ext4")
    check("mkfs no args usage", r"usage: mkfs\.ext4 <device> confirm")
    mon.type_line("mkfs.ext4 hdap1")
    check("mkfs refused w/o confirm", "Nothing was written")

    # C: real ext4 format + signature listing
    mon.type_line("mkfs.ext4 hdap1 confirm", settle=1.2)
    check("mkfs.ext4 ok", r"mkfs\.ext4: 'hdap1' formatted")
    mon.type_line("lspart hda")
    check("p1 classified ext", r"#1 ext .* type=0x83")

    # D: mount auto-detect (ext4) + umount
    mon.type_line("mount hdap1 /tgt")
    check("mount ext4", r"mounted hdap1 \(ext4\) at /tgt")

    # E: formatting a MOUNTED device is refused
    mon.type_line("mkfs.ext4 hdap1 confirm")
    check("mkfs mounted refused", "is mounted - umount it first")

    # F: umount + unknown-fs refusal + bad umount
    mon.type_line("umount /tgt")
    check("umount ext4", "unmounted /tgt")
    mon.type_line("mount hda /x")
    check("mount unknown refused", "does not carry a mountable filesystem")
    mon.type_line("umount /x")
    check("umount bad path", "umount: /x failed")

    # G: FAT32 then exFAT then FAT32 again on p2 (type byte 0x0C wins)
    mon.type_line("mkfs.fat32 hdap2 confirm", settle=1.0)
    check("mkfs.fat32 ok", r"mkfs\.fat32: 'hdap2' formatted")
    mon.type_line("mount hdap2 /fat")
    check("mount fat32", r"mounted hdap2 \(fat32\) at /fat")
    mon.type_line("umount /fat")
    check("umount fat32", "unmounted /fat")
    mon.type_line("mkfs.exfat hdap2 confirm", settle=1.0)
    check("mkfs.exfat ok", r"mkfs\.exfat: 'hdap2' formatted")
    mon.type_line("mount hdap2 /ex")
    check("mount exfat", r"mounted hdap2 \(exfat\) at /ex")
    mon.type_line("umount /ex")
    check("umount exfat", "unmounted /ex")
    mon.type_line("mkfs.fat32 hdap2 confirm", settle=1.0)
    check("p2 fat32 again", r"mkfs\.fat32: 'hdap2' formatted")

    # H: ext4 remount (volume persists, type sync did not break it)
    mon.type_line("mount hdap1 /tgt")
    check("ext4 remount", r"mounted hdap1 \(ext4\) at /tgt")
    mon.type_line("umount /tgt")

    print("\n[b3 summary] fails:", fails if fails else "none")
except Exception as e:
    print(f"\n[b3 CRASH] {e}")
    fails.append("crash")
finally:
    if mon:
        mon.s.close()
    child.terminate(force=True)
    time.sleep(1.0)          # let qemu flush the disk image

if fails:
    sys.exit(1)

# ==================================================================
# Host-side structural verification (reads the raw disk image back)
# ==================================================================
SEC = 512
P1_LBA = 2048                       # 1 MiB
P2_LBA = 33 * 2048                  # 33 MiB
raw = open(DISK, "rb").read()
hfails = []


def hcheck(name, ok, detail=""):
    print(f"[b3 HOST {'OK' if ok else 'FAIL'}] {name} {detail}")
    if not ok:
        hfails.append(name)


# -- MBR: partition types after the mkfs syncs --
mbr = raw[0:512]
t1 = mbr[446 + 4]
t2 = mbr[446 + 16 + 4]
hcheck("mbr p1 type 0x83", t1 == 0x83, hex(t1))
hcheck("mbr p2 type 0x0C", t2 == 0x0C, hex(t2))
hcheck("mbr signature", mbr[510:512] == b"\x55\xaa")

# -- ext4 volume (p1): 65536 sectors = 8192 blocks of 4 KiB --
p1 = raw[P1_LBA * SEC:]
sb = p1[1024:2048]
magic, = struct.unpack_from("<H", sb, 56)
hcheck("ext4 magic 0xEF53", magic == 0xEF53, hex(magic))
state, = struct.unpack_from("<H", sb, 58)
hcheck("ext4 state clean", state == 1)
blocks_count, = struct.unpack_from("<I", sb, 4)
free_blocks, = struct.unpack_from("<I", sb, 12)
free_inodes, = struct.unpack_from("<I", sb, 16)
bpg, = struct.unpack_from("<I", sb, 32)
ipg, = struct.unpack_from("<I", sb, 40)
log_bsz, = struct.unpack_from("<I", sb, 24)
bsz = 1024 << log_bsz
hcheck("ext4 blocks_count 8192", blocks_count == 8192, str(blocks_count))
hcheck("ext4 block size 4096", bsz == 4096, str(bsz))
hcheck("ext4 free blocks 8058", free_blocks == 8192 - 134, str(free_blocks))
hcheck("ext4 free inodes 2037", free_inodes == 2048 - 11, str(free_inodes))
hcheck("ext4 bpg 32768", bpg == 32768)
hcheck("ext4 ipg 2048", ipg == 2048)
# group descriptor 0: bitmap/ibitmap/itable at 2/3/4, honest counts
gdt = p1[bsz:bsz + 32]
bmp, ibmp, itbl = struct.unpack_from("<III", gdt, 0)
gfree, gino, gdirs = struct.unpack_from("<HHH", gdt, 12)
hcheck("ext4 gd0 layout", (bmp, ibmp, itbl) == (2, 3, 4), f"{bmp}/{ibmp}/{itbl}")
hcheck("ext4 gd0 free blocks", gfree == 8192 - 134, str(gfree))
hcheck("ext4 gd0 free inodes", gino == 2048 - 11, str(gino))
hcheck("ext4 gd0 used dirs", gdirs == 1, str(gdirs))
# block bitmap: bits 0..133 used (meta + root + lpf), no bit beyond
bm = p1[2 * bsz:3 * bsz]
used_ok = all(bm[i >> 3] & (1 << (i & 7)) for i in range(134))
tail_ok = not any(bm[i >> 3] & (1 << (i & 7)) for i in range(134, 8192))
hcheck("ext4 bitmap meta used", used_ok)
hcheck("ext4 bitmap tail free", tail_ok)
# root directory block at 132: . .. lost+found
rb = p1[132 * bsz:133 * bsz]
ino, rec, nl, ft = struct.unpack_from("<IHBB", rb, 0)
hcheck("root dirent '.'", (ino, rec, nl, ft) == (2, 12, 1, 2))
ino, rec, nl, ft = struct.unpack_from("<IHBB", rb, 12)
hcheck("root dirent '..'", (ino, rec, nl, ft) == (2, 12, 2, 2))
ino, rec, nl, ft = struct.unpack_from("<IHBB", rb, 24)
hcheck("root dirent lost+found", (ino, rec, nl) == (11, 4096 - 24, 10)
       and rb[32:42] == b"lost+found")
# root inode (itable block 4, offset (2-1)*256): size 4096, 1 extent -> 132
# (slot N-1 holds inode N - fixed with the mkfs off-by-one, b4 battery)
itb = p1[4 * bsz:5 * bsz]
rino = itb[1 * 256:2 * 256]
mode, = struct.unpack_from("<H", rino, 0)
size, = struct.unpack_from("<I", rino, 4)
links, = struct.unpack_from("<H", rino, 26)
flags, = struct.unpack_from("<I", rino, 32)
eh_magic, eh_ent, eh_max, eh_depth = struct.unpack_from("<HHHH", rino, 40)
ee_blk, ee_len, ee_hi = struct.unpack_from("<IHH", rino, 52)
ee_lo, = struct.unpack_from("<I", rino, 60)
hcheck("root inode mode", mode == 0x41ED, hex(mode))
hcheck("root inode size", size == 4096)
hcheck("root inode links 3", links == 3, str(links))
hcheck("root inode extents flag", flags == 0x80000, hex(flags))
hcheck("root extent -> block 132",
       (eh_magic, eh_ent, eh_max, eh_depth) == (0xF30A, 1, 4, 0) and
       ee_blk == 0 and ee_len == 1 and
       ((ee_hi << 32) | ee_lo) == 132,
       f"{eh_magic:04x} n={eh_ent} p={(ee_hi << 32) | ee_lo}")
# lost+found block: "." -> 11
lb = p1[133 * bsz:134 * bsz]
ino, rec, nl, ft = struct.unpack_from("<IHBB", lb, 0)
hcheck("lpf dirent '.'", (ino, rec, nl) == (11, 12, 1))

# -- FAT32 volume (p2): BPB + OEM + type byte already 0x0C --
p2 = raw[P2_LBA * SEC:]
hcheck("fat32 OEM", p2[82:90] == b"FAT32   ", str(p2[82:90]))
hcheck("fat32 boot sig", p2[510:512] == b"\x55\xaa")
spc = p2[13]
reserved, = struct.unpack_from("<H", p2, 14)
nfats = p2[16]
root_clus, = struct.unpack_from("<I", p2, 44)
hcheck("fat32 geometry", spc in (1, 8) and reserved == 32 and nfats == 2
       and root_clus == 2, f"spc={spc} res={reserved}")
fat_ent0, fat_ent1, fat_ent2 = struct.unpack_from("<III", p2, 32 * SEC)
hcheck("fat32 fat entries", (fat_ent0 & 0x0FFFFFF8) == 0x0FFFFFF8 and
       (fat_ent1 & 0x0FFFFFF8) == 0x0FFFFFF8 and
       fat_ent2 == 0x0FFFFFFF)

print("\n[b3 HOST summary] fails:", hfails if hfails else "none")
sys.exit(1 if hfails else 0)
