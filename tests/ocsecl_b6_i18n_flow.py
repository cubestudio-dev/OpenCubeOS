#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 */
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - tests/ocsecl_b6_i18n_flow.py (3.0.0-WP06b-ocsecl)
# Batch-6 acceptance: the full installer journey IN CHINESE plus the
#   boundary refusal matrix, and the zh-installed disk boots.
#   zh output reaches the serial line as GB2312 (ocse_out converts on
#   the fly for the console_cn renderer); pexpect decodes with
#   errors="replace", so every CJK byte arrives as U+FFFD - the zh
#   assertions match the ASCII skeleton plus U+FFFD runs at the exact
#   positions the translated fragments occupy.
#   Boundary matrix (English, after switching back): ocbl-install on
#   a tableless disk, install onto an ext4 target, install onto an
#   unmounted target, config with nothing mounted, dd beyond the end
#   of a device, tar on a missing archive.
import os
import sys
import time
import socket
import pexpect

BUILD = "/home/z/oc-public/build"
DISK = f"{BUILD}/ocse-b6-disk64.raw"
DISK2 = f"{BUILD}/ocse-b6-disk32.raw"
MON = f"{BUILD}/ocse-b6-mon.sock"
for f in (DISK, DISK2):
    if os.path.exists(f):
        os.unlink(f)
with open(DISK, "wb") as f:
    f.write(b"\0" * (64 * 1024 * 1024))
with open(DISK2, "wb") as f:
    f.write(b"\0" * (32 * 1024 * 1024))
if os.path.exists(MON):
    os.unlink(MON)

QEMU = "qemu-system-x86_64"
CMD = (f"{QEMU} -L /home/z/opt/extract/usr/share/qemu "
       f"-L /home/z/opt/extract/usr/share/seabios -m 256M "
       f"-drive file={DISK},format=raw,if=ide,index=0,media=disk "
       f"-drive file={DISK2},format=raw,if=ide,index=1,media=disk "
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
F = "[^\x00-\x7F]"   # GB2312 decodes to a mix of U+FFFD and
                        # accidentally-valid UTF-8 pairs - match any
                        # non-ASCII run


def check(name, pattern, timeout=60):
    try:
        child.expect(pattern, timeout=timeout)
        print(f"\n[b6 OK] {name}")
    except Exception as e:
        print(f"\n[b6 FAIL] {name}: {e}")
        fails.append(name)


try:
    child.expect("environment up: GUI mode", timeout=120)
    mon = Mon(MON)
    mon.cmd("sendkey ctrl-alt-f2", 1.0)
    check("cli prompt", r"ocsecl>")

    # ---- A: the installer journey in Chinese ----
    mon.type_line("lang zh")
    check("lang zh", r"ocsecl>")
    mon.type_line("mkpart hda 1 60")
    check("mkpart zh", r"[^\x00-\x7F]+: hdap1")
    mon.type_line("mkfs.fat32 hdap1")
    check("mkfs gate warn zh", r"mkfs\.fat32: [^\x00-\x7F]")
    check("mkfs gate nowrite zh", r"'hdap1'[^\x00-\x7F]")
    mon.type_line("mkfs.fat32 hdap1 confirm", settle=1.2)
    check("mkfs formatted zh", r"mkfs\.fat32: 'hdap1' [^\x00-\x7F]")
    mon.type_line("mount hdap1 /mnt")
    check("mount zh", r"[^\x00-\x7F]+ hdap1 \(fat32[^\x00-\x7F]+ /mnt")
    mon.type_line("install hda 1")
    check("install gate zh", r"install: [^\x00-\x7F]")
    check("install gate nowrite zh", r"install: [^\x00-\x7F]")
    mon.type_line("install hda 1 confirm", settle=6.0)
    check("medium ok zh", r"install: [^\x00-\x7F]+ocos\.tar")
    check("target zh", r"install: target hdap1[^\x00-\x7F]+fat32[^\x00-\x7F]+ /mnt")
    check("extract (file-tool EN)", r"extracted /mnt/boot/opencube\.elf")
    check("kernel in place zh", r"install: [^\x00-\x7F]+2075728 B\)")
    check("boot code zh", r"ocbl-install: [^\x00-\x7F]+stage1 @LBA0")
    check("bootable zh", r"partition #1 [^\x00-\x7F]+")
    check("complete zh", r"[^\x00-\x7F]{4,}")
    mon.type_line("config")
    check("config show (file content)", r"hostname=opencube")
    mon.type_line("config set hostname zhbox")
    check("config set zh", r"config: hostname=zhbox [^\x00-\x7F]+")
    mon.type_line("sync")
    check("sync zh", r"sync: [^\x00-\x7F]+")
    mon.type_line("umount /mnt")
    check("umount zh", r"[^\x00-\x7F]+ /mnt")

    # ---- B: the boundary matrix (back to English) ----
    mon.type_line("lang en")
    check("lang en", r"ocsecl>")
    mon.type_line("ocbl-install hdb confirm")
    check("boundary: tableless disk", "no partition table")
    mon.type_line("mkpart hdb 2 10")
    check("mkpart hdbp1", "partition created: hdbp1")
    mon.type_line("mkfs.ext4 hdbp1 confirm", settle=1.2)
    check("ext4 format", r"mkfs\.ext4: 'hdbp1' formatted")
    mon.type_line("install hdb 1 confirm")
    check("boundary: ext4 target refused", "' is not FAT32")
    mon.type_line("mkfs.fat32 hdbp1 confirm", settle=1.0)
    check("refat32 hdbp1", r"mkfs\.fat32: 'hdbp1' formatted")
    mon.type_line("install hdb 1 confirm")
    check("boundary: unmounted target refused", "is not mounted")
    mon.type_line("config")
    check("boundary: config without target", "no installed system mounted")
    mon.type_line("dd if=hdbp1 of=/x bs=512 count=9 skip=99999")
    check("boundary: dd beyond end", "read range beyond the end of the device")
    mon.type_line("tar -t /nope.tar")
    check("boundary: tar missing archive", "tar: cannot open")
    mon.type_line("sync")
    check("sync en", "sync: block cache flushed")

    print("\n[b6 phase1 summary] fails:", fails if fails else "none")
except Exception as e:
    print(f"\n[b6 CRASH] {e}")
    fails.append("crash")
finally:
    if mon:
        mon.s.close()
    child.terminate(force=True)
    time.sleep(1.0)

if fails:
    sys.exit(1)

# ==================================================================
# Phase 2: the zh-installed disk boots (content is language-independent)
# ==================================================================
MON2 = f"{BUILD}/ocse-b6-mon2.sock"
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
    check("ocbl scan", r"\[scan\] entries: 1", timeout=120)
    check("ocos entry", r"OCOS: Open Cube OS \(hd0 p1\)")
    check("countdown", r"Booting ")
    check("kernel loaded", r"\[boot\] kernel loaded", timeout=90)
    check("kmain", r"kmain entered", timeout=180)
    check("ush prompt", r"ush>", timeout=180)
    print("\n[b6 phase2 summary] fails:", fails if fails else "none")
except Exception as e:
    print(f"\n[b6 PHASE2 CRASH] {e}")
    fails.append("phase2-crash")
finally:
    child.terminate(force=True)
    time.sleep(1.0)

sys.exit(1 if fails else 0)
