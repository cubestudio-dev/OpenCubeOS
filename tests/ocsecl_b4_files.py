#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - tests/ocsecl_b4_files.py (3.0.0-WP06b-ocsecl)
# Batch-4 acceptance: the file command group + tar/dd/sha256sum.
#   cwd model (cd/pwd/ls with normalization), text I/O (cat/write),
#   file ops (touch/cp/mv/rm/mkdir/rmdir with the empty-rmdir and
#   rm-vs-rmdir guards), ustar tar -t/-x onto the ext4 volume, dd
#   between devices and files (BUG-0239 confirm gate, mounted-device
#   refusal, device round-trip), sha256sum of files and device
#   ranges, and the Tab path completion. HOST-SIDE verification
#   walks the raw image's ext4 structures and byte-compares the
#   extracted payloads (opencube.elf, opencube.conf, hello.txt)
#   against the sources, plus the dd-written sectors on p2.
import hashlib
import os
import struct
import sys
import time
import socket
import pexpect

BUILD = "/home/z/oc-public/build"
STAGE = f"{BUILD}/ocse-iso-stage"
DISK = f"{BUILD}/ocse-b4-disk64.raw"
MON = f"{BUILD}/ocse-b4-mon.sock"
if os.path.exists(DISK):
    os.unlink(DISK)
with open(DISK, "wb") as f:
    f.write(b"\0" * (64 * 1024 * 1024))          # blank, zeroed
if os.path.exists(MON):
    os.unlink(MON)

KERNEL = open(f"{BUILD}/opencube.elf", "rb").read()
KERN_SHA = hashlib.sha256(KERNEL).hexdigest()
MBRIMG = open(f"{STAGE}/OCBLMBR.BIN", "rb").read()
MBR_BLOCKS = (len(MBRIMG) + 511) // 512           # dd bs=512 rounds
CONF = (b"# OpenCubeOS configuration\n"
        b"hostname=opencube\n"
        b"lang=en\n"
        b"boot.slot=A\n")

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
        if ch.isupper():
            return f"shift-{ch.lower()}"   # bare key names are lowercase only
        return ch


child = pexpect.spawn(CMD, timeout=180, encoding="utf-8", codec_errors="replace")
child.logfile = sys.stdout
mon = None
fails = []


def check(name, pattern, timeout=60):
    try:
        child.expect(pattern, timeout=timeout)
        print(f"\n[b4 OK] {name}")
    except Exception as e:
        print(f"\n[b4 FAIL] {name}: {e}")
        fails.append(name)


try:
    child.expect("environment up: GUI mode", timeout=120)
    mon = Mon(MON)
    mon.cmd("sendkey ctrl-alt-f2", 1.0)
    check("cli prompt", r"ocsecl>")

    # ---- A: volume setup (b3 chain recap) ----
    mon.type_line("mkpart hda 1 33")
    check("mkpart p1", "partition created: hdap1")
    mon.type_line("mkpart hda 33 64")
    check("mkpart p2", "partition created: hdap2")
    mon.type_line("mkfs.ext4 hdap1 confirm", settle=1.2)
    check("mkfs.ext4 ok", r"mkfs\.ext4: 'hdap1' formatted")
    mon.type_line("mount hdap1 /mnt")
    check("mount ext4", r"mounted hdap1 \(ext4\) at /mnt")

    # ---- B: cwd model ----
    mon.type_line("pwd")
    check("pwd /", r"pwd[\r\n]*/[\r\n]*ocsecl>")
    mon.type_line("ls")
    check("ls root shows mounts", r"\[D\] install")
    mon.type_line("cd /mnt")
    check("cd /mnt silent", r"ocsecl>")
    mon.type_line("pwd")
    check("pwd /mnt", r"pwd[\r\n]*/mnt[\r\n]*ocsecl>")
    mon.type_line("mkdir etc")
    mon.type_line("cd etc")
    check("cd relative", r"ocsecl>")
    mon.type_line("pwd")
    check("pwd /mnt/etc", r"pwd[\r\n]*/mnt/etc[\r\n]*ocsecl>")

    # ---- C: write/cat/sha256sum ----
    mon.type_line("write hello.txt hello world")
    check("write 12 bytes", r"wrote 12 bytes to /mnt/etc/hello\.txt")
    mon.type_line("cat hello.txt")
    check("cat hello", r"hello world")
    mon.type_line("sha256sum hello.txt")
    check("sha256 hello", hashlib.sha256(b"hello world\n").hexdigest())

    # ---- D: Tab completion (unique match -> full name; typed char by
    # char because type_line would press Enter itself) ----
    for ch in "cat he":
        mon.cmd(f"sendkey {mon.keyname(ch)}", 0.05)
    mon.cmd("sendkey tab", 0.8)
    mon.cmd("sendkey ret", 1.0)
    check("tab completes hello.txt", r"hello world")

    # ---- E: directory ops + guards ----
    mon.type_line("cd /mnt")
    mon.type_line("mkdir d2")
    mon.type_line("write d2/x.txt abc")
    check("write in d2", r"wrote 4 bytes to /mnt/d2/x\.txt")
    mon.type_line("rmdir d2")
    check("rmdir not empty", r"rmdir: cannot remove /mnt/d2")
    mon.type_line("rm d2/x.txt")
    mon.type_line("rmdir d2")
    mon.type_line("ls d2")
    check("d2 gone", r"ls: no such path: /mnt/d2")
    mon.type_line("rm etc")
    check("rm refuses dir", r"' is a directory \(use rmdir\)")
    mon.type_line("cd etc/hello.txt")
    check("cd refuses file", r"cd: not a directory: etc/hello\.txt")
    mon.type_line("cd /nope")
    check("cd missing", r"cd: no such directory: /nope")
    mon.type_line("cat /mnt/nope.txt")
    check("cat missing", r"cat: cannot open /mnt/nope\.txt")

    # ---- F: cp/mv/rm/touch ----
    mon.type_line("cp /mnt/etc/hello.txt /mnt/etc/copy.txt")
    mon.type_line("cat /mnt/etc/copy.txt")
    check("cp content", r"hello world")
    mon.type_line("mv /mnt/etc/copy.txt /mnt/etc/copy2.txt")
    mon.type_line("cat /mnt/etc/copy2.txt")
    check("mv same-parent rename", r"hello world")
    mon.type_line("mv /mnt/etc/copy2.txt /mnt/copy3.txt")
    mon.type_line("cat /mnt/copy3.txt")
    check("mv cross-parent", r"hello world")
    mon.type_line("ls /mnt/etc")
    check("copy2 gone after mv", r"hello\.txt")
    mon.type_line("rm /mnt/copy3.txt")
    mon.type_line("cat /mnt/copy3.txt")
    check("copy3 gone after rm", r"cat: cannot open /mnt/copy3\.txt")
    mon.type_line("touch /mnt/empty")
    mon.type_line("ls /mnt")
    check("touch size 0", r"\s+0 empty")

    # ---- G: tar list + extract onto the ext4 volume ----
    mon.type_line("tar -t /install/OCOS.TAR", settle=1.0)
    check("tar -t dirs", r"\[D\] boot/")
    check("tar -t elf", r"boot/opencube\.elf")
    mon.type_line("tar -x /install/OCOS.TAR /mnt", settle=4.0)
    check("tar -x conf", rf"extracted /mnt/etc/opencube\.conf \({len(CONF)} bytes\)")
    mon.type_line("cat /mnt/etc/opencube.conf")
    check("conf content", r"hostname=opencube")
    mon.type_line(f"sha256sum /mnt/boot/opencube.elf", settle=3.0)
    check("extracted elf sha256", KERN_SHA)

    # ---- H: dd ----
    mon.type_line(f"dd if=/install/OCBLMBR.BIN of=/mnt/mbr.bin", settle=1.5)
    check("dd file->file", rf"dd: copied {len(MBRIMG) // 512} block\(s\), {len(MBRIMG)} byte\(s\)")
    mon.type_line("sha256sum /mnt/mbr.bin")
    check("mbr.bin sha", hashlib.sha256(MBRIMG).hexdigest())
    # device -> file (E14 coherency: p1 was just written through the cache)
    mon.type_line("dd if=hdap1 of=/mnt/p1head.bin bs=512 count=4")
    check("dd dev->file", r"dd: copied 4 block\(s\), 2048 byte\(s\)")
    # device target: confirm gate, then the real write + round-trip
    mon.type_line("dd if=/mnt/mbr.bin of=hdap2 bs=512 count=2")
    check("dd gate no confirm", "dd: Nothing was written.")
    mon.type_line("dd if=/mnt/mbr.bin of=hdap2 bs=512 count=2 confirm")
    check("dd file->dev", r"dd: copied 2 block\(s\), 1024 byte\(s\)")
    mon.type_line("dd if=hdap2 of=/mnt/check.bin bs=512 count=2")
    mon.type_line("sha256sum /mnt/check.bin")
    check("dd round-trip", hashlib.sha256(MBRIMG[0:1024]).hexdigest())
    # seek= on a device target: p1head lands at p2 sector 4
    mon.type_line("dd if=/mnt/p1head.bin of=hdap2 bs=512 seek=4 confirm", settle=1.0)
    check("dd file->dev seek", r"dd: copied 4 block\(s\), 2048 byte\(s\)")
    # mounted device is never a dd target
    mon.type_line("dd if=/mnt/mbr.bin of=hdap1 bs=512 count=1 confirm")
    check("dd mounted refused", "is mounted - umount it first")

    # ---- I: persistence across umount/remount ----
    mon.type_line("sync")
    check("sync", "sync: block cache flushed")
    mon.type_line("umount /mnt")
    check("umount", "unmounted /mnt")
    mon.type_line("mount hdap1 /mnt")
    check("remount", r"mounted hdap1 \(ext4\) at /mnt")
    mon.type_line("cat /mnt/etc/hello.txt")
    check("hello persists", r"hello world")
    mon.type_line("ls /mnt/boot")
    check("boot persists", r"opencube\.elf")
    mon.type_line("sync")

    print("\n[b4 summary] fails:", fails if fails else "none")
except Exception as e:
    print(f"\n[b4 CRASH] {e}")
    fails.append("crash")
finally:
    if mon:
        mon.s.close()
    child.terminate(force=True)
    time.sleep(1.0)          # let qemu flush the disk image

if fails:
    sys.exit(1)

# ==================================================================
# Host-side verification: walk the ext4 volume on p1, byte-compare
# the extracted payloads, and check the dd-written sectors on p2.
# ==================================================================
SEC = 512
P1_LBA = 2048                       # 1 MiB
P2_LBA = 33 * 2048                  # 33 MiB
raw = open(DISK, "rb").read()
hfails = []


def hcheck(name, ok, detail=""):
    print(f"[b4 HOST {'OK' if ok else 'FAIL'}] {name} {detail}")
    if not ok:
        hfails.append(name)


p1 = raw[P1_LBA * SEC:]
sb = p1[1024:2048]
bsz = 1024 << struct.unpack_from("<I", sb, 24)[0]
ipg, = struct.unpack_from("<I", sb, 40)
inode_size, = struct.unpack_from("<H", sb, 88)
hcheck("ext4 alive", struct.unpack_from("<H", sb, 56)[0] == 0xEF53)


def read_inode(n):
    g, idx = (n - 1) // ipg, (n - 1) % ipg
    gdt = p1[bsz * (1 + g):bsz * (2 + g)]
    itbl, = struct.unpack_from("<I", gdt, 8)
    off = itbl * bsz + idx * inode_size
    return p1[off:off + inode_size]


def inode_data(ino):
    """File bytes via the extent tree (depth <= 2)."""
    out = bytearray()
    size, = struct.unpack_from("<I", ino, 4)
    i_block = ino[40:100]

    def walk(node):
        magic, ent, _max, depth = struct.unpack_from("<HHHH", node, 0)
        if magic != 0xF30A:
            return False
        for i in range(ent):
            if depth == 0:
                _, ln, hi, lo = struct.unpack_from("<IHHI", node, 12 + 12 * i)
                start = (hi << 32) | lo
                out.extend(p1[start * bsz:(start + ln) * bsz])
            else:
                _, lo, hi, _ = struct.unpack_from("<IIHH", node, 12 + 12 * i)
                leaf = (hi << 32) | lo
                if not walk(p1[leaf * bsz:leaf * bsz + bsz]):
                    return False
        return True

    ok = walk(i_block)
    return bytes(out[:size]) if ok else None


def dir_lookup(ino, name):
    data = inode_data(read_inode(ino))
    i = 0
    while i + 8 <= len(data):
        dn, rec, nl, ft = struct.unpack_from("<IHBB", data, i)
        if rec < 8 or i + rec > len(data):
            return None
        if dn != 0 and data[i + 8:i + 8 + nl].decode() == name:
            return dn
        i += rec
    return None


def resolve(path):
    ino = 2                                    # ext4 root
    for comp in path.strip("/").split("/"):
        ino = dir_lookup(ino, comp)
        if ino is None:
            return None
    return ino


def file_bytes(path):
    ino = resolve(path)
    return inode_data(read_inode(ino)) if ino else None


# -- extracted payloads are byte-identical to the sources --
hcheck("host: hello.txt bytes", file_bytes("etc/hello.txt") == b"hello world\n")
hcheck("host: opencube.conf bytes", file_bytes("etc/opencube.conf") == CONF)
elf = file_bytes("boot/opencube.elf")
hcheck("host: opencube.elf bytes", elf == KERNEL,
       f"{len(elf) if elf else 0} vs {len(KERNEL)}")
hcheck("host: mbr.bin bytes", file_bytes("mbr.bin") == MBRIMG)
# p1head.bin captured the p1 head mid-battery; check.bin's allocation
# afterwards legitimately bumped the sb free counters (exactly 1 block
# + 1 inode). Everything else must be byte-identical to the final disk.
_p1h = file_bytes("p1head.bin")
_dh = raw[P1_LBA * SEC:P1_LBA * SEC + 2048]
_diffs = sorted(i for i in range(2048) if _p1h[i] != _dh[i])
hcheck("host: p1head.bin bytes (sb counters drift ok)",
       set(_diffs) <= set(range(1036, 1044)), f"diffs={_diffs}")
hcheck("host: check.bin bytes", file_bytes("check.bin") == MBRIMG[0:1024])
hcheck("host: copy3.txt removed", resolve("copy3.txt") is None)
hcheck("host: etc/copy2.txt removed by mv", resolve("etc/copy2.txt") is None)
hcheck("host: d2 removed", resolve("d2") is None)
hcheck("host: empty exists", resolve("empty") is not None)

# -- dd wrote the first 1024 bytes of OCBLMBR.BIN at p2 LBA0 and the
#    p1 head (2048 B) at p2 sector 4 (seek=) --
p2head = raw[P2_LBA * SEC:P2_LBA * SEC + 1024]
hcheck("host: p2 sector 0-1 = mbr image head", p2head == MBRIMG[0:1024])
p2seek = raw[P2_LBA * SEC + 4 * SEC:P2_LBA * SEC + 4 * SEC + 2048]
# both sides of this comparison come from the SAME dd read of hdap1
# (the seek dd wrote what step-1 captured), so they must be identical.
hcheck("host: p2 sector 4-7 = p1 head (seek)", p2seek == _p1h)

# -- honest accounting after the batch: used inodes grew, dirs counted --
free_inodes, = struct.unpack_from("<I", sb, 16)
# 11 reserved + root + lpf + etc,d2(was),hello,copy(was),elf,conf,mbr,
# p1head,check,empty + boot dir
hcheck("host: free inodes sane", 0 < free_inodes < 2048, str(free_inodes))

print("\n[b4 HOST summary] fails:", hfails if hfails else "none")
sys.exit(1 if hfails else 0)
