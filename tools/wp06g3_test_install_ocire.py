#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 */
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - tools/wp06g3_test_install_ocire.py (3.0.0-WP06g3-ocse_enc_fix)
# Task 4.4 end-to-end: OCSE installs OCOS with the dedicated OCIRE
# recovery partition, then the installed disk boots and the OCBL menu
# carries BOTH the system entry and the OCIRE recovery entry.
import os, socket, sys, time
import pexpect

BUILD = "/home/z/oc-os/build"
EV    = "/home/z/oc-os/evidence/3_0_0_wp06g3_ocse_enc_fix"
QEMU  = "/home/z/opt/extract/usr/bin/qemu-system-x86_64"
QL    = "/home/z/opt/extract/usr/share/qemu"
SEA  = "/home/z/opt/extract/usr/share/seabios"
DISK  = f"{EV}/install-e2e-disk.raw"

with open(DISK, "wb") as f:
    f.truncate(64 * 1024 * 1024)

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
            while True:
                r = self.s.recv(65536)
                if not r:
                    break
        except socket.timeout:
            pass

def key(mon, s, settle=0.12):
    for ch in s:
        if ch == " ":
            k = "spc"
        elif ch == "_":
            k = "shift-minus"
        elif ch == ".":
            k = "dot"
        elif ch == "/":
            k = "slash"
        else:
            k = ch
        mon.cmd("sendkey " + k, settle)

mon_path = f"{EV}/install-e2e-mon.sock"
if os.path.exists(mon_path):
    os.unlink(mon_path)

# ---------- phase 1: OCSE install ----------
cmd = (f"{QEMU} -L {QL} -L {SEA} -m 256M "
       f"-drive file={DISK},format=raw,if=none,id=sata0 "
       f"-device ahci,id=ahci0 -device ide-hd,drive=sata0,bus=ahci0.0 "
       f"-cdrom {BUILD}/ocse.iso -boot d "
       f"-no-reboot -display none -serial stdio "
       f"-monitor unix:{mon_path},server,nowait -vga std")

log = open(f"{EV}/fix44_install_e2e.log", "wb")
child = pexpect.spawn(cmd, timeout=180)
child.logfile = log

try:
    child.expect("environment up", timeout=120)
    m = Mon(mon_path)
    time.sleep(1)
    m.cmd("sendkey ctrl-alt-f2")
    child.expect("OCSECL", timeout=30)

    def run_cli(line, expect, tmo=60, settle=0.4):
        key(m, line)
        m.cmd("sendkey ret", settle)
        child.expect(expect, timeout=tmo)

    run_cli("mkpart sda 1 40", "ocsecl>")
    print("[1] mkpart sda 1-40MiB: OK")
    run_cli("mkfs.fat32 sdap1 confirm", "ocsecl>", 120)
    print("[2] mkfs.fat32 sdap1: OK")
    run_cli("mount sdap1 /t", "ocsecl>")
    print("[3] mount sdap1 /t: OK")
    # install: expects the recovery-partition carve + the full tail
    run_cli("install sda 1 confirm",
            r"recovery partition #sdap2 ready \(boot/ocire\.elf", 180, 2.0)
    print("[4] install carved the dedicated OCIRE recovery partition: OK")
    child.expect("complete", timeout=120)
    print("[5] install complete: OK")
    # the block cache is write-back: without an explicit sync the
    # force-terminate drops dirty sectors (core tail) and the installed
    # disk hangs at boot - the b5 battery's lesson, re-learned live.
    key(m, "sync")
    m.cmd("sendkey ret", 3.0)
    child.expect("block cache flushed", timeout=120)
    print("[5b] sync (block cache flushed): OK")
    m.cmd("screendump " + EV + "/fix44_install_complete.ppm", 1.0)
    child.terminate(force=True)
    time.sleep(2.0)
except Exception as e:
    print(f"PHASE1 FAILED: {e}")
    child.terminate(force=True)
    log.close()
    sys.exit(1)
finally:
    try:
        child.close(force=True)
    except Exception:
        pass
    log.close()

time.sleep(1)

# ---------- phase 2: boot the installed disk ----------
mon_path2 = f"{EV}/install-e2e-mon2.sock"
if os.path.exists(mon_path2):
    os.unlink(mon_path2)

# the OCBL battery baseline boots installed disks through the IDE
# flavor (ocbl.mk run targets); AHCI-boot is not a verified OCBL path.
cmd = (f"{QEMU} -L {QL} -L {SEA} -m 256M "
       f"-drive file={DISK},format=raw,if=ide,index=0,media=disk "
       f"-boot c -no-reboot -display none -serial stdio "
       f"-monitor unix:{mon_path2},server,nowait -vga std")

log = open(f"{EV}/fix44_installed_boot.log", "wb")
child = pexpect.spawn(cmd, timeout=180)
child.logfile = log

try:
    child.expect(r"OCOS: Open Cube OS \(hd0", timeout=90)
    print("[6] installed disk boots, OCOS entry scanned: OK")
    child.expect(r"OCIRE: OCIRE recovery \(hd0", timeout=30)
    print("[7] installed disk carries the OCIRE recovery entry: OK")
    m2 = Mon(mon_path2)
    m2.cmd("screendump " + EV + "/fix44_installed_menu.ppm", 1.0)
    time.sleep(1)
    child.terminate(force=True)
except Exception as e:
    print(f"PHASE2 FAILED: {e}")
    child.terminate(force=True)
finally:
    try:
        child.close(force=True)
    except Exception:
        pass
    log.close()
