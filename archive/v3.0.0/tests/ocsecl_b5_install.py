#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - tests/ocsecl_b5_install.py (3.0.0-WP06b-ocsecl)
# Batch-5 acceptance: the installer chain, end to end.
#   Phase 1 (OCSE VM, boot from CD): mkpart + mkfs.fat32 + mount,
#   install without confirm refused, `install hda 1 confirm` runs the
#   whole state machine (medium check -> target checks -> OCOS.TAR
#   extraction -> ocbl-install boot code -> boot flag -> completion
#   notice), config show/set round trip with key validation, sync.
#   Phase 2 (fresh VM, boot from DISK, no CD): the really-installed
#   chain boots - OCBL MBR flavor (stage1 -> core) -> scan ->
#   "OCOS: Open Cube OS (hd0)" -> countdown -> mb2 load -> kmain ->
#   ush> prompt. The installed system actually runs.
#   HOST-SIDE: LBA0 == OCBLMBR.BIN head with the patch block
#   (magic/'OCBL', core_lba=1, core_sectors) and the REAL partition
#   table (boot flag set); LBA1.. == the core image bytes; the FAT32
#   volume carries /boot/opencube.elf byte-identical to the build
#   and /etc/opencube.conf with the config-set hostname line.
import hashlib
import os
import struct
import sys
import time
import socket
import pexpect

BUILD = "/home/z/oc-public/build"
STAGE = f"{BUILD}/ocse-iso-stage"
DISK = f"{BUILD}/ocse-b5-disk.raw"
MON = f"{BUILD}/ocse-b5-mon.sock"
if os.path.exists(DISK):
    os.unlink(DISK)
with open(DISK, "wb") as f:
    f.write(b"\0" * (64 * 1024 * 1024))          # blank, zeroed
if os.path.exists(MON):
    os.unlink(MON)

KERNEL = open(f"{BUILD}/opencube.elf", "rb").read()
MBRIMG = open(f"{STAGE}/OCBLMBR.BIN", "rb").read()
CORE_SECS = (len(MBRIMG) - 512 + 511) // 512
CONF_TMPL = (b"# OpenCubeOS configuration\n"
             b"hostname=opencube\n"
             b"lang=en\n"
             b"boot.slot=A\n")
HOSTNAME_LINE = b"hostname=cube01\n"

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
        print(f"\n[b5 OK] {name}")
    except Exception as e:
        print(f"\n[b5 FAIL] {name}: {e}")
        fails.append(name)


try:
    child.expect("environment up: GUI mode", timeout=120)
    mon = Mon(MON)
    mon.cmd("sendkey ctrl-alt-f2", 1.0)
    check("cli prompt", r"ocsecl>")

    # ---- A: volume setup (FAT32 - OCBL boots from FAT) ----
    mon.type_line("mkpart hda 1 60")
    check("mkpart p1", "partition created: hdap1")
    mon.type_line("mkfs.fat32 hdap1 confirm", settle=1.2)
    check("mkfs.fat32 ok", r"mkfs\.fat32: 'hdap1' formatted")
    mon.type_line("mount hdap1 /mnt")
    check("mount fat32", r"mounted hdap1 \(fat32\) at /mnt")

    # ---- B: install gate ----
    mon.type_line("install hda 1")
    check("install needs confirm", "install: Nothing was written.")

    # ---- C: the state machine ----
    mon.type_line("install hda 1 confirm", settle=6.0)
    check("medium ok", r"install: medium ok \(ocos\.tar ")
    check("target ok", r"install: target hdap1 \(fat32\) mounted at /mnt")
    check("extract", r"extracted /mnt/boot/opencube\.elf")
    check("kernel in place", r"install: kernel in place \(")
    check("boot code written", r"ocbl-install: boot code written")
    check("bootable", r"marked bootable")
    # #132 (WP-fix-p2-p3-cleanup): the install state machine now lands
    # everything DURABLY before the completion banner (step 9 flush).
    # The old test ran an explicit `sync` here, which masked the
    # defect: without it, a power-cut right after "Installation
    # complete" left a zero-holed, unbootable boot core (byte-level
    # forensics in the audit).  Phase 2 below now proves the install's
    # OWN flush is sufficient - QEMU is SIGKILLed, no guest shutdown,
    # no manual sync.
    check("install flush", "install: flushing all disks")
    check("complete", "Installation complete.")

    # ---- D: config ----
    mon.type_line("config")
    check("config show", r"hostname=opencube")
    mon.type_line("config set hostname cube01")
    check("config set", "config: hostname=cube01 written")
    mon.type_line("config set lang xx")
    check("config invalid refused", "config: invalid key or value")
    mon.type_line("config set boot.slot B")
    check("config slot B", "config: boot.slot=B written")
    mon.type_line("cat /mnt/etc/opencube.conf")
    check("conf on disk", r"hostname=cube01")
    # #132: the explicit `sync` step is GONE - see the flush note above.
    mon.type_line("umount /mnt")
    check("umount", "unmounted /mnt")

    print("\n[b5 phase1 summary] fails:", fails if fails else "none")
except Exception as e:
    print(f"\n[b5 CRASH] {e}")
    fails.append("crash")
finally:
    if mon:
        mon.s.close()
    child.terminate(force=True)
    time.sleep(1.0)          # let qemu flush the disk image

if fails:
    print("\n[b5 FAILING EARLY - skipping the boot phase]")
    sys.exit(1)

# ==================================================================
# Phase 2: boot the REALLY installed disk (no CD, boot from disk)
# ==================================================================
MON2 = f"{BUILD}/ocse-b5-mon2.sock"
if os.path.exists(MON2):
    os.unlink(MON2)
CMD2 = (f"{QEMU} -L /home/z/opt/extract/usr/share/qemu "
        f"-L /home/z/opt/extract/usr/share/seabios -m 256M "
        f"-drive file={DISK},format=raw,if=ide,index=0,media=disk "
        f"-boot c -display none -serial stdio "
        f"-monitor unix:{MON2},server,nowait -vga std")

child = pexpect.spawn(CMD2, timeout=240, encoding="utf-8", codec_errors="replace")
child.logfile = sys.stdout
try:
    # WP06g3-4.4: the extracted system volume carries /boot/ocire.elf,
    # so the installed disk legitimately scans TWO entries (OCOS +
    # in-volume OCIRE recovery).  The "entries: 1" expectation below
    # was a WP06b-era leftover and went stale when the recovery entry
    # landed (found while re-running the battery for #132).
    check("ocbl scan", r"\[scan\] entries: 2", timeout=120)
    check("ocos entry", r"OCOS: Open Cube OS \(hd0 p1\)")
    check("countdown", r"Booting ")
    check("kernel loaded", r"\[boot\] kernel loaded", timeout=90)
    check("kmain", r"kmain entered", timeout=180)
    check("ush prompt", r"ush>", timeout=180)
    print("\n[b5 phase2 summary] fails:", fails if fails else "none")
except Exception as e:
    print(f"\n[b5 PHASE2 CRASH] {e}")
    fails.append("phase2-crash")
finally:
    child.terminate(force=True)
    time.sleep(1.0)

if fails:
    sys.exit(1)

# ==================================================================
# Host-side verification
# ==================================================================
SEC = 512
raw = open(DISK, "rb").read()
hfails = []


def hcheck(name, ok, detail=""):
    print(f"[b5 HOST {'OK' if ok else 'FAIL'}] {name} {detail}")
    if not ok:
        hfails.append(name)


# -- LBA0: OCBL stage1 with the patch block + the real table --
mbr = raw[0:512]
# the patch block (0x1A0..0x1AD) is installer-filled BY DESIGN
_pdiffs = sorted(i for i in range(446) if mbr[i] != MBRIMG[i])
hcheck("mbr == stage1 head (patch block excluded)",
       set(_pdiffs) <= set(range(0x1A0, 0x1AE)), f"diffs={_pdiffs}")
magic, lba_lo, lba_hi, secs = struct.unpack_from("<IIIH", mbr, 0x1A0)
hcheck("patch magic OCBL", magic == 0x4C42434F, hex(magic))
hcheck("patch core lba 1", lba_lo == 1 and lba_hi == 0)
hcheck("patch core sectors", secs == CORE_SECS, str(secs))
hcheck("mbr signature", mbr[510:512] == b"\x55\xaa")
# partition table preserved + boot flag
p1 = mbr[446:462]
hcheck("p1 boot flag", p1[0] == 0x80, hex(p1[0]))
p1_lba, p1_secs = struct.unpack_from("<II", p1, 8)
hcheck("p1 start 2048", p1_lba == 2048, str(p1_lba))
hcheck("p1 type 0x0C", p1[4] == 0x0C, hex(p1[4]))

# -- the core image at LBA 1.. --
core = raw[SEC:SEC + CORE_SECS * SEC]
hcheck("core bytes identical", core == MBRIMG[512:], f"{len(core)} B")

# -- the FAT32 volume: /boot/opencube.elf + /etc/opencube.conf --
fat = raw[p1_lba * SEC:]
bps, = struct.unpack_from("<H", fat, 11)
spc = fat[13]
rsvd, = struct.unpack_from("<H", fat, 14)
nfats = fat[16]
root_clus, = struct.unpack_from("<I", fat, 44)
hcheck("fat32 bps 512 spc 1", bps == 512 and spc == 1)
fat_off = rsvd * bps
data_off = (rsvd + nfats * 2 + 1) * bps   # FAT32: 2 sectors FAT on 60MiB? derive


def fat_entry(n):
    off = fat_off + n * 4
    return struct.unpack_from("<I", fat, off)[0] & 0x0FFFFFFF


# derive the data area: cluster 2 = first data sector; find via root dir
# scan for the FAT size instead of assuming
fatsz, = struct.unpack_from("<I", fat, 36)
data_off = (rsvd + nfats * fatsz) * bps
clus_sz = bps * spc


def clus_bytes(n):
    off = data_off + (n - 2) * clus_sz
    return fat[off:off + clus_sz]


def walk_dir(clus):
    """yield (name_str, first_clus, size, is_dir); LFN-aware"""
    data = b""
    c = clus
    while 2 <= c < 0x0FFFFFF8:
        data += clus_bytes(c)
        c = fat_entry(c)
    out = []
    lfn = {}                          # seq -> 13 UTF-16 chars
    for i in range(0, len(data) - 32, 32):
        e = data[i:i + 32]
        if e[0] == 0:
            break
        if e[0] == 0xE5:
            lfn = {}
            continue
        attr = e[11]
        if attr == 0x0F:              # LFN slot
            seq = e[0] & 0x1F
            lfn[seq] = e[1:11] + e[14:26] + e[28:32]
            continue
        if attr & 0x08:               # volume label
            lfn = {}
            continue
        name = None
        if lfn:
            buf = b"".join(lfn[k] for k in sorted(lfn, reverse=True))
            s2 = ""
            for j in range(0, len(buf), 2):
                cp = buf[j] | (buf[j + 1] << 8)
                if cp == 0 or cp == 0xFFFF:
                    break
                s2 += chr(cp)
            if s2:
                name = s2
            lfn = {}
        if name is None:
            base = e[0:8].rstrip(b" ").decode(errors="replace")
            ext = e[8:11].rstrip(b" ").decode(errors="replace")
            name = base + ("." + ext if ext else "")
        fc = struct.unpack_from("<H", e, 26)[0] | \
             (struct.unpack_from("<H", e, 20)[0] << 16)
        sz = struct.unpack_from("<I", e, 28)[0]
        out.append((name, fc, sz, bool(attr & 0x10)))
    return out


def read_file(clus, size):
    data = b""
    c = clus
    while 2 <= c < 0x0FFFFFF8 and len(data) < size:
        data += clus_bytes(c)
        c = fat_entry(c)
    return data[:size]


def lookup(root_clus, name):
    for nm, fc, sz, isdir in walk_dir(root_clus):
        if nm.upper() == name.upper():
            return nm, fc, sz, isdir
    return None


boot = lookup(root_clus, "BOOT")
hcheck("fat /boot dir", boot is not None and boot[3])
etc = lookup(root_clus, "ETC")
hcheck("fat /etc dir", etc is not None and etc[3])
elf = lookup(boot[1], "OPENCUBE.ELF")
hcheck("fat /boot/opencube.elf", elf is not None and not elf[3])
elfbytes = read_file(elf[1], elf[2])
hcheck("fat elf bytes identical", elfbytes == KERNEL,
       f"{len(elfbytes)} vs {len(KERNEL)}")
conf = lookup(etc[1], "OPENCUBE.CONF")
confbytes = read_file(conf[1], conf[2])
hcheck("fat conf hostname line", HOSTNAME_LINE in confbytes)
hcheck("fat conf boot.slot B", b"boot.slot=B\n" in confbytes)

print("\n[b5 HOST summary] fails:", hfails if hfails else "none")
sys.exit(1 if hfails else 0)
