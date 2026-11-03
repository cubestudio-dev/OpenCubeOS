#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - tests/ocse_w6_gui.py (3.0.0-WP06c-installer_gui)
# The GUI installer wizard battery. Drives the 9-step wizard through
# the QEMU monitor (sendkey + PS/2 mouse_move/mouse_button - NO
# usb-mouse device: it steals the pointer routing) and asserts the
# whole journey on the serial log, where the wizard mirrors every
# OCSECL command it runs ("[wizard] $ <cmd>") plus each command's
# captured output.
#
#   Phase A  CD boot -> "Install now" click -> the 9 steps by KEYBOARD
#            (Enter = the page's default button; the disk/part/format
#            transitions are probe-paced: every Enter is only sent
#            after the previous page's serial marker matched, so no
#            stray Enter can land on the wrong page):
#            lang -> disk -> part(mkpart from the largest gap) ->
#            format -> user(name+password) -> sys -> confirm -> the
#            install chain (mount/install/config/write users/sync/
#            umount) -> done -> Finish click -> wizard exit.
#   Phase B  the freshly installed disk, NO CD, boot c: OCBL MBR ->
#            core -> scan -> hd0 p1 -> mb2 -> kmain -> ush>.
#   Phase C  HOST-SIDE bytes: LBA0 = OCBL stage1 + patch block + the
#            real MBR table (boot flag, p1 FAT32 geometry); the core
#            image byte-identical; the FAT volume carries /boot/
#            opencube.elf byte-identical to the build, /etc/opencube.conf
#            with the wizard's four keys, and /etc/users with the
#            SHA-256 hex of the typed password (never the plain text).
#
# Usage: python3 tests/ocse_w6_gui.py [abc|a|b|c|de|d|e]
import hashlib
import os
import random
import re
import socket
import sys
import time
import pexpect

BUILD = "/home/z/oc-private/oc-os/build"
DISK = f"{BUILD}/ocse-w6-disk.raw"
MON = f"{BUILD}/ocse-w6-mon.sock"
MB = 128                                   # target disk size
if os.path.exists(DISK):
    os.unlink(DISK)
with open(DISK, "wb") as f:
    f.truncate(MB * 1024 * 1024)           # blank, zeroed
if os.path.exists(MON):
    os.unlink(MON)

KERNEL = open(f"{BUILD}/opencube.elf", "rb").read()
MBRIMG = open(f"{BUILD}/ocse-iso-stage/OCBLMBR.BIN", "rb").read()
CORE_SECS = (len(MBRIMG) - 512 + 511) // 512

USER = "tester"
PASS = "s3cret"
PASS_SHA = hashlib.sha256(PASS.encode()).hexdigest()

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
        try:
            self.s.recv(65536)
        except socket.timeout:
            pass

    def enter(self, settle=0.55):
        self.cmd("sendkey ret", settle)

    def key(self, k, settle=0.14):
        self.cmd(f"sendkey {k}", settle)

    def type_text(self, text):
        for ch in text:
            k = {" ": "spc", ".": "dot", "/": "slash",
                 "-": "minus", "_": "shift-minus"}.get(ch, ch)
            self.key(k)

    def type_line(self, text, settle=0.6):
        # 0.2 s per key: right after a command that ran the disk scan
        # the i8042 ring can still be draining - faster typing drops
        # keys (live-caught: 'mkfs.fat32' arrived as 'mkfsfat32')
        for c in text:
            k = {" ": "spc", ".": "dot", "/": "slash",
                 "-": "minus", "_": "shift-minus"}.get(c, c)
            self.cmd(f"sendkey {k}", 0.2)
        self.cmd("sendkey ret", settle)

    def click(self, x, y):
        # The guest pointer is (400,300) at every page start (panel
        # static init + wz_reset). Walk there in <=100-unit steps:
        # each step becomes ONE fully-flushed PS/2 packet. Longer
        # moves leave a QEMU-side movement RESIDUE that merges into
        # the next host event (live-caught: a -2000 rush made the
        # following +200 land as +105).
        cx, cy = 400, 300
        while cx != x or cy != y:
            nx = cx + max(-100, min(100, x - cx))
            ny = cy + max(-100, min(100, y - cy))
            self.cmd(f"mouse_move {nx - cx} {ny - cy}", 0.30)
            cx, cy = nx, ny
        self.cmd("mouse_button 1", 0.3)
        self.cmd("mouse_button 0", 0.6)


child = pexpect.spawn(CMD, timeout=180, encoding="utf-8",
                      codec_errors="replace")
child.logfile = sys.stdout
ok = True
fails = []


def wcheck(name, pattern, timeout=60):
    """expect one wizard-flow serial marker"""
    global ok
    try:
        child.expect(pattern, timeout=timeout)
        print(f"\n[w6 OK] {name}")
    except Exception as e:
        print(f"\n[w6 FAIL] {name}: {e}")
        fails.append(name)
        ok = False
        raise


def expect_count(pattern, n, timeout=60):
    """consume the stream until the pattern has matched n times"""
    for _ in range(n):
        child.expect(pattern, timeout=timeout)


try:
    child.expect("environment up: GUI mode", timeout=120)
    print("\n[boot] GUI up")
    mon = Mon(MON)

    # ---- enter the wizard (mouse: the 4th panel button) ----
    mon.click(695, 426)                    # Install now (620,400 150x52)
    wcheck("wizard entered", r"\[ocse\] wizard: enter")

    # ---- step 1: welcome+language (default = Next) ----
    mon.enter()
    wcheck("step2 disk page (lsdisk)", r"\[wizard\] \$ lsdisk")

    # ---- step 2: target disk (default = Next once a row exists).
    # Enter fires Next: warm lsdisk + gate lspart + PART entry
    # lsdisk+lspart -> two lspart echoes. ----
    mon.enter()
    expect_count(r"\[wizard\] \$ lspart hda", 2, timeout=60)
    print("\n[w6 OK] step3 part page (gate + entry lspart)")

    # ---- step 3: partitioning (default = Create; gap [1,128)) ----
    mon.enter()
    wcheck("mkpart from the gap", r"\[wizard\] \$ mkpart hda 1 128")
    # fresh partition: ext/0x83 placeholder until the FAT32 format
    # syncs the MBR type (b3 mkfs behavior). [^\r\n]* NOT .*: pexpect
    # compiles string patterns with re.DOTALL, so .* would swallow
    # the loop-head echoes between the two identical table lines.
    wcheck("p1 table line", r"#1 [^\r\n]* start=1MiB end=128MiB")
    expect_count(r"\[wizard\] \$ lspart hda", 1, timeout=60)
    print("\n[w6 OK] part re-read + page reloop (default=Next)")

    # ---- step 4: formatting (default = Format, then Next) ----
    mon.enter()                            # Next -> fmt page
    time.sleep(0.8)
    mon.enter()                            # Format -> mkfs
    wcheck("mkfs echo", r"\[wizard\] \$ mkfs\.fat32 hdap1 confirm")
    wcheck("mkfs formatted", r"mkfs\.fat32: 'hdap1' formatted")

    # ---- step 5: user account ----
    mon.enter()                            # Next -> user page
    time.sleep(0.8)
    mon.type_text(USER)                    # focus is the name input
    mon.key("tab", 0.3)                    # -> full name (left empty)
    mon.key("tab", 0.3)                    # -> password
    mon.type_text(PASS)
    mon.enter()                            # Next -> sys page
    time.sleep(0.8)

    # ---- step 6: system configuration (hostname prefilled) ----
    mon.enter()                            # Next -> confirm page
    time.sleep(0.8)

    # ---- step 7: review (default = Start) -> the install chain ----
    mon.enter(settle=1.0)
    wcheck("mount target", r"\[wizard\] \$ mount hdap1 /mnt")
    wcheck("install cmd", r"\[wizard\] \$ install hda 1 confirm")
    wcheck("install medium ok", r"install: medium ok \(ocos\.tar ",
           timeout=90)
    wcheck("install extracted", r"extracted /mnt/boot/opencube\.elf")
    wcheck("install boot code", r"ocbl-install: boot code written")
    wcheck("install bootable", r"marked bootable")
    wcheck("install complete", "Installation complete.")
    wcheck("config hostname", r"\[wizard\] \$ config set hostname opencube")
    wcheck("config lang", r"\[wizard\] \$ config set lang en")
    wcheck("config auto_check", r"\[wizard\] \$ config set auto_check yes")
    wcheck("users record", r"\[wizard\] \$ write /mnt/etc/users " + USER)
    # the digest alone (64 hex chars, unique on the line): the
    # users-record match already consumed through the username, so a
    # "tester::<sha>" pattern here could never match again
    wcheck("password is sha-256", re.escape(PASS_SHA))
    wcheck("sync", r"\[wizard\] \$ sync")
    wcheck("umount", r"\[wizard\] \$ umount /mnt")

    # ---- step 9: done (Finish = mouse; REBOOT is the default!) ----
    time.sleep(1.0)
    mon.click(545, 523)                    # Finish (470,500 150x46)
    wcheck("wizard exit", r"\[ocse\] wizard: exit")

    mon.cmd(f"screendump {BUILD}/ocse-w6-done.ppm", 1.0)
    print("\n[w6 phase A summary] fails:", fails if fails else "none")
except Exception as e:
    print(f"\n[w6 CRASH] {e}")
    fails.append("crash")
    ok = False
finally:
    try:
        mon.s.close()
    except Exception:
        pass
    child.terminate(force=True)
    time.sleep(1.5)                        # let qemu flush the disk image

phase = sys.argv[1] if len(sys.argv) > 1 else "abc"
if fails or "b" not in phase:
    sys.exit(1 if fails else 0)

# ==================================================================
# Phase B: boot the REALLY installed disk (no CD, boot from disk)
# ==================================================================
MON2 = f"{BUILD}/ocse-w6-mon2.sock"
if os.path.exists(MON2):
    os.unlink(MON2)
CMD2 = (f"{QEMU} -L /home/z/opt/extract/usr/share/qemu "
        f"-L /home/z/opt/extract/usr/share/seabios -m 256M "
        f"-drive file={DISK},format=raw,if=ide,index=0,media=disk "
        f"-boot c -display none -serial stdio "
        f"-monitor unix:{MON2},server,nowait -vga std")

child = pexpect.spawn(CMD2, timeout=240, encoding="utf-8",
                      codec_errors="replace")
child.logfile = sys.stdout
ok2 = True
try:
    def bcheck(name, pattern, timeout=120):
        global ok2
        try:
            child.expect(pattern, timeout=timeout)
            print(f"\n[w6 B OK] {name}")
        except Exception as e:
            print(f"\n[w6 B FAIL] {name}: {e}")
            fails.append(f"B:{name}")
            ok2 = False

    bcheck("ocbl scan", r"\[scan\] entries: 1")
    bcheck("ocos entry", r"OCOS: Open Cube OS \(hd0 p1\)")
    bcheck("countdown", r"Booting ")
    bcheck("kernel loaded", r"\[boot\] kernel loaded")
    bcheck("kmain", r"kmain entered", timeout=180)
    bcheck("ush prompt", r"ush>", timeout=180)
    print("\n[w6 phase B summary] fails:", fails if fails else "none")
except Exception as e:
    print(f"\n[w6 B CRASH] {e}")
    fails.append("B:crash")
    ok2 = False
finally:
    child.terminate(force=True)
    time.sleep(1.0)

if fails or "c" not in phase:
    sys.exit(1 if fails else 0)

# ==================================================================
# Phase C: HOST-SIDE verification of the disk image
# ==================================================================
SEC = 512
raw = open(DISK, "rb").read()
hfails = []


def hcheck(name, cond, detail=""):
    print(f"[w6 HOST {'OK' if cond else 'FAIL'}] {name} {detail}")
    if not cond:
        hfails.append(name)


# -- LBA0: OCBL stage1 with the patch block + the real table --
mbr = raw[0:512]
_pdiffs = sorted(i for i in range(446) if mbr[i] != MBRIMG[i])
hcheck("mbr == stage1 head (patch block excluded)",
       set(_pdiffs) <= set(range(0x1A0, 0x1AE)), f"diffs={_pdiffs}")
magic, lba_lo, lba_hi, secs = __import__("struct").unpack_from(
    "<IIIH", mbr, 0x1A0)
hcheck("patch magic OCBL", magic == 0x4C42434F, hex(magic))
hcheck("patch core lba 1", lba_lo == 1 and lba_hi == 0)
hcheck("patch core sectors", secs == CORE_SECS, str(secs))
hcheck("mbr signature", mbr[510:512] == b"\x55\xaa")
p1 = mbr[446:462]
hcheck("p1 boot flag", p1[0] == 0x80, hex(p1[0]))
p1_lba, p1_secs = __import__("struct").unpack_from("<II", p1, 8)
hcheck("p1 start 2048", p1_lba == 2048, str(p1_lba))
hcheck("p1 end = disk end", 2048 + p1_secs == MB * 2048,
       f"{p1_secs} sectors")
hcheck("p1 type 0x0C", p1[4] == 0x0C, hex(p1[4]))

# -- the core image at LBA 1.. --
core = raw[SEC:SEC + CORE_SECS * SEC]
hcheck("core bytes identical", core == MBRIMG[512:], f"{len(core)} B")

# -- the FAT32 volume --
import struct
fat = raw[p1_lba * SEC:]
bps, = struct.unpack_from("<H", fat, 11)
spc = fat[13]
rsvd, = struct.unpack_from("<H", fat, 14)
nfats = fat[16]
root_clus, = struct.unpack_from("<I", fat, 44)
hcheck("fat32 bps 512 spc 1", bps == 512 and spc == 1)
fat_off = rsvd * bps


def fat_entry(n):
    off = fat_off + n * 4
    return struct.unpack_from("<I", fat, off)[0] & 0x0FFFFFFF


fatsz, = struct.unpack_from("<I", fat, 36)
data_off = (rsvd + nfats * fatsz) * bps
clus_sz = bps * spc


def clus_bytes(n):
    off = data_off + (n - 2) * clus_sz
    return fat[off:off + clus_sz]


def walk_dir(clus):
    data = b""
    c = clus
    while 2 <= c < 0x0FFFFFF8:
        data += clus_bytes(c)
        c = fat_entry(c)
    out = []
    lfn = {}
    for i in range(0, len(data) - 32, 32):
        e = data[i:i + 32]
        if e[0] == 0:
            break
        if e[0] == 0xE5:
            lfn = {}
            continue
        attr = e[11]
        if attr == 0x0F:
            seq = e[0] & 0x1F
            lfn[seq] = e[1:11] + e[14:26] + e[28:32]
            continue
        if attr & 0x08:
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


def lookup(root, name):
    for nm, fc, sz, isdir in walk_dir(root):
        if nm.upper() == name.upper():
            return nm, fc, sz, isdir
    return None


boot = lookup(root_clus, "BOOT")
hcheck("fat /boot dir", boot is not None and boot[3])
etc = lookup(root_clus, "ETC")
hcheck("fat /etc dir", etc is not None and etc[3])
if boot and etc:
    elf = lookup(boot[1], "OPENCUBE.ELF")
    hcheck("fat /boot/opencube.elf", elf is not None and not elf[3])
    if elf:
        elfbytes = read_file(elf[1], elf[2])
        hcheck("fat elf bytes identical", elfbytes == KERNEL,
               f"{len(elfbytes)} vs {len(KERNEL)}")
    conf = lookup(etc[1], "OPENCUBE.CONF")
    hcheck("fat conf file", conf is not None)
    if conf:
        confbytes = read_file(conf[1], conf[2])
        hcheck("conf hostname", b"hostname=opencube\n" in confbytes)
        hcheck("conf lang en", b"lang=en\n" in confbytes)
        hcheck("conf auto_check yes", b"auto_check=yes\n" in confbytes)
        hcheck("conf no update_url (left default)",
               b"update_url" not in confbytes)
    users = lookup(etc[1], "USERS")
    hcheck("fat users file", users is not None)
    if users:
        ub = read_file(users[1], users[2])
        hcheck("users record exact",
               ub == f"{USER}::{PASS_SHA}\n".encode(), repr(ub[:80]))
        hcheck("plain password absent", PASS.encode() not in raw)

print("\n[w6 HOST summary] fails:", hfails if hfails else "none")
if fails or hfails:
    sys.exit(1)
if "d" not in phase and "e" not in phase:
    sys.exit(0)

# ==================================================================
# Phase D: the SAME journey in CHINESE (the i18n toggle on the
# wizard's language page; the zh catalog emits GB2312 over serial -
# through pexpect's UTF-8 decoder the pairs become replacement or
# valid CJK chars, so assertions match NON-ASCII runs, never the
# plain text - the b6 GB-over-serial lesson)
# ==================================================================
DISKZH = f"{BUILD}/ocse-w6-disk-zh.raw"
MONZH = f"{BUILD}/ocse-w6-mon-zh.sock"
if os.path.exists(DISKZH):
    os.unlink(DISKZH)
with open(DISKZH, "wb") as f:
    f.truncate(MB * 1024 * 1024)
if os.path.exists(MONZH):
    os.unlink(MONZH)
CMDZH = (f"{QEMU} -L /home/z/opt/extract/usr/share/qemu "
         f"-L /home/z/opt/extract/usr/share/seabios -m 256M "
         f"-drive file={DISKZH},format=raw,if=ide,index=0,media=disk "
         f"-cdrom {BUILD}/ocse.iso -boot d "
         f"-no-reboot -display none -serial stdio "
         f"-monitor unix:{MONZH},server,nowait -vga std")

child = pexpect.spawn(CMDZH, timeout=180, encoding="utf-8",
                      codec_errors="replace")
child.logfile = sys.stdout
try:
    child.expect("environment up: GUI mode", timeout=120)
    print("\n[boot zh] GUI up")
    mon = Mon(MONZH)

    mon.click(695, 426)                    # Install now
    wcheck("wizard entered (zh)", r"\[ocse\] wizard: enter")
    mon.click(124, 243)                    # Language button -> zh

    mon.enter()
    wcheck("step2 disk page (zh)", r"\[wizard\] \$ lsdisk")
    mon.enter()
    expect_count(r"\[wizard\] \$ lspart hda", 2, timeout=60)
    print("\n[w6 D OK] step3 part page (zh)")

    mon.enter()
    wcheck("mkpart (zh)", r"\[wizard\] \$ mkpart hda 1 128")
    wcheck("p1 table line (zh)", r"#1 [^\r\n]* start=1MiB end=128MiB")
    expect_count(r"\[wizard\] \$ lspart hda", 1, timeout=60)

    mon.enter()                            # Next -> fmt page
    time.sleep(0.8)
    mon.enter()                            # Format -> mkfs
    wcheck("mkfs echo (zh)", r"\[wizard\] \$ mkfs\.fat32 hdap1 confirm")
    # the formatted line in zh: GB2312 bytes right after the device
    wcheck("mkfs zh output", r"mkfs\.fat32: 'hdap1' [^\x00-\x7f]")

    mon.enter()                            # Next -> user page
    time.sleep(0.8)
    mon.type_text(USER)
    mon.key("tab", 0.3)
    mon.key("tab", 0.3)
    mon.type_text(PASS)
    mon.enter()                            # -> sys
    time.sleep(0.8)
    mon.enter()                            # -> confirm
    time.sleep(0.8)
    mon.enter(settle=1.0)                  # Start -> the install chain
    wcheck("mount target (zh)", r"\[wizard\] \$ mount hdap1 /mnt")
    wcheck("install cmd (zh)", r"\[wizard\] \$ install hda 1 confirm")
    # the state machine lines keep the ASCII "install: " prefix and
    # carry the GB2312 message text
    wcheck("install zh output", r"install: [^\x00-\x7f]", timeout=90)
    wcheck("extracted (zh)", r"extracted /mnt/boot/opencube\.elf")
    wcheck("config lang zh", r"\[wizard\] \$ config set lang zh")
    wcheck("users record (zh)", r"\[wizard\] \$ write /mnt/etc/users "
           + USER)
    wcheck("password is sha-256 (zh)", re.escape(PASS_SHA))
    wcheck("sync (zh)", r"\[wizard\] \$ sync")
    wcheck("umount (zh)", r"\[wizard\] \$ umount /mnt")

    time.sleep(1.0)
    mon.click(545, 523)                    # Finish
    wcheck("wizard exit (zh)", r"\[ocse\] wizard: exit")
    print("\n[w6 phase D summary] fails:", fails if fails else "none")
except Exception as e:
    print(f"\n[w6 D CRASH] {e}")
    fails.append("D:crash")
finally:
    try:
        mon.s.close()
    except Exception:
        pass
    child.terminate(force=True)
    time.sleep(1.5)

if fails or "e" not in phase:
    sys.exit(1 if fails else 0)

# host-side: the zh install wrote lang=zh. Rebind the Phase C walker
# globals (raw/fat/root_clus) to the zh disk - lookup/read_file close
# over them, and the Phase C disk's conf (lang=en) at the same cluster
# number would otherwise masquerade as the zh result.
zraw = open(DISKZH, "rb").read()
raw = zraw                            # rebind for lookup/read_file
fat = raw[2048 * SEC:]
root_clus, = struct.unpack_from("<I", fat, 44)
zh_hfails = []


def zhcheck(name, cond, detail=""):
    print(f"[w6 D HOST {'OK' if cond else 'FAIL'}] {name} {detail}")
    if not cond:
        zh_hfails.append(name)


zetc = lookup(root_clus, "ETC")
zhcheck("zh fat /etc", zetc is not None and zetc[3])
if zetc:
    zconf = lookup(zetc[1], "OPENCUBE.CONF")
    if zconf:
        zb = read_file(zconf[1], zconf[2])
        zhcheck("zh conf lang=zh", b"lang=zh\n" in zb, repr(zb[:60]))
    zusers = lookup(zetc[1], "USERS")
    if zusers:
        zb = read_file(zusers[1], zusers[2])
        zhcheck("zh users record exact",
                zb == f"{USER}::{PASS_SHA}\n".encode())
zhcheck("zh plain password absent", PASS.encode() not in zraw)
print("\n[w6 D HOST summary] fails:", zh_hfails if zh_hfails else "none")
if fails or zh_hfails:
    sys.exit(1)

# ==================================================================
# Phase E: the four boundaries. E1: an unknown-filesystem partition
# (NTFS-shaped: type 0x07 + non-FAT bytes) is PRESERVED - the wizard
# builds in the remaining gap and installs onto partition TWO, and
# that system boots. E2: a full disk (no >=64 MiB gap) - the wizard
# cannot create, refuses nothing loudly, and a cancel leaves the disk
# untouched. E3: an existing OCOS system (ext4 with
# /boot/opencube.elf) trips the no-overwrite gate - error page, retry
# back to the disk page, nothing destroyed. E4: cancel right after
# the format - the partition exists (FAT32 type synced) but nothing
# was installed (no core, no boot flag, no OCBL magic).
# ==================================================================


def fresh_disk(path, size_mb):
    if os.path.exists(path):
        os.unlink(path)
    with open(path, "wb") as f:
        f.truncate(size_mb * 1024 * 1024)


def mbr_write_part(path, idx, start_lba, sectors, ptype, bootable=False):
    with open(path, "r+b") as f:
        f.seek(446 + 16 * idx)
        f.write(bytes([0x80 if bootable else 0x00, 0, 0, 0, ptype,
                       0, 0, 0]))
        f.write(struct.pack("<II", start_lba, sectors))
        f.seek(510)
        f.write(b"\x55\xaa")


def mbr_entry(path, idx):
    with open(path, "rb") as f:
        f.seek(446 + 16 * idx)
        e = f.read(16)
    return e[0], e[4], struct.unpack_from("<II", e, 8)


def boot_vm(disk, monsock, with_cd=True):
    if os.path.exists(monsock):
        os.unlink(monsock)
    cd = (f"-cdrom {BUILD}/ocse.iso -boot d " if with_cd else "-boot c ")
    cmd = (f"{QEMU} -L /home/z/opt/extract/usr/share/qemu "
           f"-L /home/z/opt/extract/usr/share/seabios -m 256M "
           f"-drive file={disk},format=raw,if=ide,index=0,media=disk "
           f"{cd}-no-reboot -display none -serial stdio "
           f"-monitor unix:{monsock},server,nowait -vga std")
    ch = pexpect.spawn(cmd, timeout=240, encoding="utf-8",
                       codec_errors="replace")
    ch.logfile = sys.stdout
    return ch


def wizard_install(ch, m, part_expect, install_expect, lang_click=False):
    """Enter-only drive through the 9 steps (en). Caller asserts the
    per-step serial markers it cares about; this helper just performs
    the typed user page and the pacing."""
    ch.expect("environment up: GUI mode", timeout=120)
    m.click(695, 426)
    ch.expect(r"\[ocse\] wizard: enter", timeout=30)
    if lang_click:
        m.click(124, 243)
    m.enter()
    ch.expect(r"\[wizard\] \$ lsdisk", timeout=30)
    m.enter()
    expect_count(r"\[wizard\] \$ lspart hda", 2, timeout=60)
    m.enter()                              # Create
    ch.expect(part_expect, timeout=30)
    expect_count(r"\[wizard\] \$ lspart hda", 1, timeout=60)
    m.enter()                              # Next -> fmt
    time.sleep(0.8)
    m.enter()                              # Format
    ch.expect(r"\[wizard\] \$ mkfs\.fat32 [^\r\n]* confirm", timeout=30)
    ch.expect(r"mkfs\.fat32: '[^\r\n]*' ", timeout=30)
    m.enter()                              # Next -> user
    time.sleep(0.8)
    m.type_text(USER)
    m.key("tab", 0.3)
    m.key("tab", 0.3)
    m.type_text(PASS)
    m.enter()                              # -> sys
    time.sleep(0.8)
    m.enter()                              # -> confirm
    time.sleep(0.8)
    m.enter(settle=1.0)                    # Start
    ch.expect(install_expect, timeout=30)
    ch.expect(r"\[wizard\] \$ sync", timeout=120)
    ch.expect(r"\[wizard\] \$ umount /mnt", timeout=60)
    time.sleep(1.0)
    m.click(545, 523)                      # Finish
    ch.expect(r"\[ocse\] wizard: exit", timeout=30)


# ---- E1: NTFS-shaped partition preserved, install lands on p2 ----
E1 = f"{BUILD}/ocse-w6-e1.raw"
fresh_disk(E1, MB)
mbr_write_part(E1, 0, 2048, 65536, 0x07)     # p1: [1,33) MiB, 0x07
with open(E1, "r+b") as f:                   # non-FAT pseudo-random body
    f.seek(2048 * SEC)
    f.write(random.Random(20261102).randbytes(16384))
    f.seek(2048 * SEC + 510)                 # kill any FAT-ish sig
    f.write(b"\x07\x07")
MON_E1 = f"{BUILD}/ocse-w6-mon-e1.sock"
child = boot_vm(E1, MON_E1)
try:
    print("\n[w6 E1] unknown-fs partition preserved")
    wizard_install(child, Mon(MON_E1),
                   r"\[wizard\] \$ mkpart hda 33 128",
                   r"\[wizard\] \$ install hda 2 confirm")
    e1fails = []
    print("[w6 E1 OK] mkpart into the gap + install onto p2 + exit")
except Exception as e:
    print(f"\n[w6 E1 CRASH] {e}")
    fails.append("E1:crash")
    e1fails = ["crash"]
finally:
    child.terminate(force=True)
    time.sleep(1.5)

if not e1fails:
    # boot the E1 disk: OCBL must find OCOS on hd0 p2 and run it
    MON_E1B = f"{BUILD}/ocse-w6-mon-e1b.sock"
    child = boot_vm(E1, MON_E1B, with_cd=False)
    try:
        child.expect(r"\[scan\] entries: 1", timeout=120)
        child.expect(r"OCOS: Open Cube OS \(hd0 p2\)", timeout=30)
        child.expect(r"ush>", timeout=180)
        print("[w6 E1 OK] installed p2 boots to ush")
    except Exception as e:
        print(f"\n[w6 E1 BOOT FAIL] {e}")
        fails.append("E1:boot")
    finally:
        child.terminate(force=True)
        time.sleep(1.0)
    # host-side: p1 body untouched, p2 bootable FAT32
    e1raw = open(E1, "rb").read()
    want = bytearray(random.Random(20261102).randbytes(16384))
    want[510:512] = b"\x07\x07"          # the setup's own sig-killer
    hcheck("E1 p1 body byte-identical",
           e1raw[2048 * SEC:2048 * SEC + 16384] == bytes(want))
    e1flag, e1type, (e1lba, e1secs) = mbr_entry(E1, 0)
    hcheck("E1 p1 entry preserved",
           e1flag == 0x00 and e1type == 0x07 and e1lba == 2048
           and e1secs == 65536,
           f"flag={e1flag:#x} type={e1type:#x}")
    e1flag2, e1type2, (e1lba2, e1secs2) = mbr_entry(E1, 1)
    hcheck("E1 p2 bootable fat32 [33,128)",
           e1flag2 == 0x80 and e1type2 == 0x0C and e1lba2 == 33 * 2048
           and e1secs2 == 95 * 2048,
           f"flag={e1flag2:#x} type={e1type2:#x} lba={e1lba2}")
    hcheck("E1 core at LBA1", e1raw[SEC:SEC + CORE_SECS * SEC]
           == MBRIMG[512:])
    hcheck("E1 no OCBL magic in p1 body",
           e1raw[2048 * SEC + 0x1A0:2048 * SEC + 0x1AE]
           != struct.pack("<IIIH", 0x4C42434F, 1, 0, CORE_SECS))

# ---- E2: full disk (no >=64 MiB gap): no create possible, cancel ----
E2 = f"{BUILD}/ocse-w6-e2.raw"
fresh_disk(E2, MB)
mbr_write_part(E2, 0, 2048, 133120, 0x07)    # p1: [1,65) MiB
mbr_write_part(E2, 1, 135168, 126976, 0x07)  # p2: [66,128) MiB
MON_E2 = f"{BUILD}/ocse-w6-mon-e2.sock"
child = boot_vm(E2, MON_E2)
try:
    print("\n[w6 E2] full disk - no gap, nothing created")
    ch = child
    m = Mon(MON_E2)
    ch.expect("environment up: GUI mode", timeout=120)
    m.click(695, 426)
    ch.expect(r"\[ocse\] wizard: enter", timeout=30)
    m.enter()
    ch.expect(r"\[wizard\] \$ lsdisk", timeout=30)
    m.enter()                              # gate: two mounts fail
    expect_count(r"\[wizard\] \$ lspart hda", 2, timeout=60)
    m.enter()                              # default=Next (no gap)
    idx = ch.expect([r"\[wizard\] \$ mkpart", pexpect.TIMEOUT],
                    timeout=5)
    if idx == 0:
        raise RuntimeError("mkpart ran on a full disk")
    print("[w6 E2 OK] no mkpart on a full disk")
    m.click(700, 523)                      # Cancel (630,500 140x46)
    m.enter()                              # overlay default = Yes
    ch.expect(r"\[ocse\] wizard: exit", timeout=30)
    print("[w6 E2 OK] cancel -> wizard exit")
except Exception as e:
    print(f"\n[w6 E2 CRASH] {e}")
    fails.append("E2:crash")
finally:
    child.terminate(force=True)
    time.sleep(1.5)
    e2raw = open(E2, "rb").read()
    f2, t2, (l2, s2) = mbr_entry(E2, 0)
    hcheck("E2 p1 entry untouched",
           f2 == 0x00 and t2 == 0x07 and l2 == 2048 and s2 == 133120)
    f2b, t2b, (l2b, s2b) = mbr_entry(E2, 1)
    hcheck("E2 p2 entry untouched",
           f2b == 0x00 and t2b == 0x07 and l2b == 135168)
    hcheck("E2 LBA0 not OCBL (no patch magic)",
           struct.unpack_from("<I", e2raw, 0x1A0)[0] != 0x4C42434F)
    hcheck("E2 p1 still non-FAT",
           e2raw[2048 * SEC + 510:2048 * SEC + 512] != b"\x55\xaa"
           or e2raw[2048 * SEC:2048 * SEC + 16]
           == bytes(16))

# ---- E3: existing OCOS on ext4 -> no-overwrite refusal ----
E3 = f"{BUILD}/ocse-w6-e3.raw"
fresh_disk(E3, MB)
MON_E3 = f"{BUILD}/ocse-w6-mon-e3.sock"
child = boot_vm(E3, MON_E3)
try:
    print("\n[w6 E3] existing system refused")
    ch = child
    m = Mon(MON_E3)
    ch.expect("environment up: GUI mode", timeout=120)
    m.cmd("sendkey ctrl-alt-f2", 1.0)
    ch.expect(r"ocsecl>", timeout=30)
    m.type_line("mkpart hda 1 32", settle=1.0)
    ch.expect("partition created: hdap1", timeout=30)
    m.type_line("mkfs.ext4 hdap1 confirm", settle=1.5)
    ch.expect(r"mkfs\.ext4: 'hdap1' formatted", timeout=30)
    m.type_line("mount hdap1 /mnt", settle=0.8)
    ch.expect(r"mounted hdap1", timeout=30)
    m.type_line("mkdir /mnt/boot", settle=0.6)
    # (mkdir is silent on success) the ext4 mkdir allocation storm
    # blocks IRQ1 long enough to drop a RUN of keystrokes (live-caught:
    # 'OCOSFAKE' vanished and the file was created empty) - let the
    # storm drain before typing
    time.sleep(2.0)
    m.type_line("write /mnt/boot/opencube.elf OCOSFAKE", settle=1.0)
    ch.expect(r"wrote \d+ bytes", timeout=30)
    m.type_line("umount /mnt", settle=0.8)
    ch.expect("unmounted /mnt", timeout=30)
    m.cmd("sendkey ctrl-alt-f1", 1.0)
    ch.expect(r"switched: GUI mode", timeout=30)
    m.click(695, 426)
    ch.expect(r"\[ocse\] wizard: enter", timeout=30)
    m.enter()                              # -> disk page
    ch.expect(r"\[wizard\] \$ lsdisk", timeout=30)
    m.enter()                              # Next -> the gate mounts
    ch.expect(r"\[wizard\] \$ mount hdap1 /mnt", timeout=30)
    ch.expect(r"\[wizard\] \$ ls /mnt/boot", timeout=30)
    ch.expect(r"\[wizard\] \$ umount /mnt", timeout=30)
    # the error page is up: the gate found opencube.elf; RETRY is the
    # default and returns to the DISK page (its entry echoes lsdisk)
    m.enter(settle=1.0)
    ch.expect(r"\[wizard\] \$ lsdisk", timeout=30)
    idx = ch.expect([r"\[wizard\] \$ mkpart", pexpect.TIMEOUT],
                    timeout=5)
    if idx == 0:
        raise RuntimeError("mkpart ran against an existing system")
    print("[w6 E3 OK] refused + retry returned to the disk page")
    m.click(700, 523)                      # Cancel
    m.enter()
    ch.expect(r"\[ocse\] wizard: exit", timeout=30)
except Exception as e:
    print(f"\n[w6 E3 CRASH] {e}")
    fails.append("E3:crash")
finally:
    child.terminate(force=True)
    time.sleep(1.5)
    e3raw = open(E3, "rb").read()
    # ext4 superblock magic 0xEF53 at partition offset 0x438
    sb = e3raw[2048 * SEC + 0x438:2048 * SEC + 0x43A]
    hcheck("E3 ext4 magic intact", sb == b"\x53\xef", sb.hex())
    # the created file's DIRECTORY ENTRY persisted (the name lives in
    # the ext4 dir block; the data bytes are the b4 battery's round
    # trip - this case verifies the REFUSAL, not write content)
    hcheck("E3 opencube.elf dir entry still there",
           b"opencube.elf" in e3raw[2048 * SEC:32 * 2048 * SEC])
    hcheck("E3 LBA0 not OCBL",
           struct.unpack_from("<I", e3raw, 0x1A0)[0] != 0x4C42434F)

# ---- E4: cancel right after the format: formatted, not installed ----
E4 = f"{BUILD}/ocse-w6-e4.raw"
fresh_disk(E4, MB)
MON_E4 = f"{BUILD}/ocse-w6-mon-e4.sock"
child = boot_vm(E4, MON_E4)
try:
    print("\n[w6 E4] cancel after the format")
    ch = child
    m = Mon(MON_E4)
    ch.expect("environment up: GUI mode", timeout=120)
    m.click(695, 426)
    ch.expect(r"\[ocse\] wizard: enter", timeout=30)
    m.enter()
    ch.expect(r"\[wizard\] \$ lsdisk", timeout=30)
    m.enter()
    expect_count(r"\[wizard\] \$ lspart hda", 2, timeout=60)
    m.enter()                              # Create
    ch.expect(r"\[wizard\] \$ mkpart hda 1 128", timeout=30)
    expect_count(r"\[wizard\] \$ lspart hda", 1, timeout=60)
    m.enter()                              # Next -> fmt
    time.sleep(0.8)
    m.enter()                              # Format
    ch.expect(r"mkfs\.fat32: 'hdap1' formatted", timeout=30)
    m.click(700, 523)                      # Cancel
    m.enter()                              # overlay Yes
    ch.expect(r"\[ocse\] wizard: exit", timeout=30)
    print("[w6 E4 OK] canceled after the format")
except Exception as e:
    print(f"\n[w6 E4 CRASH] {e}")
    fails.append("E4:crash")
finally:
    child.terminate(force=True)
    time.sleep(1.5)
    e4raw = open(E4, "rb").read()
    f4, t4, (l4, s4) = mbr_entry(E4, 0)
    hcheck("E4 p1 exists, fat32 type synced, NOT bootable",
           f4 == 0x00 and t4 == 0x0C and l4 == 2048,
           f"flag={f4:#x} type={t4:#x}")
    hcheck("E4 LBA0 not OCBL",
           struct.unpack_from("<I", e4raw, 0x1A0)[0] != 0x4C42434F)
    hcheck("E4 no core at LBA1 (still zeros)",
           e4raw[SEC:SEC + CORE_SECS * SEC] == bytes(CORE_SECS * SEC))
    hcheck("E4 no users file content",
           b"tester::" not in e4raw)

print("\n[w6 phase E summary] fails:", fails if fails else "none")
sys.exit(1 if (fails or hfails) else 0)

