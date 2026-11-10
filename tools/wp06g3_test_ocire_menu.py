#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 */
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - tools/wp06g3_test_ocire_menu.py (3.0.0-WP06g3-ocse_enc_fix)
# Task 4.4 verification: the main ISO's OCBL menu offers OCIRE and
# selecting it boots the recovery kernel.
import os, socket, sys, time
import pexpect

BUILD = "/home/z/oc-os/build"
EV    = "/home/z/oc-os/evidence/3_0_0_wp06g3_ocse_enc_fix"
QEMU  = "/home/z/tools/usr/bin/qemu-system-x86_64"
QL    = "/home/z/tools/usr/share/qemu"
SEA  = "/home/z/tools/usr/share/seabios"

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

mon_path = f"{EV}/ocire-menu-mon.sock"
if os.path.exists(mon_path):
    os.unlink(mon_path)

cmd = (f"{QEMU} -L {QL} -L {SEA} -m 256M -cdrom {BUILD}/opencube.iso "
       f"-boot d -no-reboot -display none -serial stdio "
       f"-monitor unix:{mon_path},server,nowait -vga std")

log = open(f"{EV}/fix44_main_iso_ocire_menu.log", "wb")
child = pexpect.spawn(cmd, timeout=120)
child.logfile = log

failures = []   # Fake-green #65: collected verdicts; any entry => exit 1

try:
    child.expect(r"OCIRE recovery \(cd0\)", timeout=60)
    print("[1] menu lists OCIRE recovery: OK")
    m = Mon(mon_path)
    # menu highlight starts at entry 0 (OCOS); one Down -> OCIRE, Enter
    m.cmd("sendkey down", 0.5)
    m.cmd("sendkey down", 0.5)
    # entry 0 = OCOS, entry 1 = OCIRE, entry 2 = Shutdown:
    # one Down lands on OCIRE
    m.cmd("sendkey up", 0.3)
    m.cmd("sendkey ret", 1.0)
    child.expect("OCIRE - OpenCubeOS Independent Recovery Environment",
                 timeout=60)
    print("[2] OCIRE kernel boots from the menu pick: OK")
    m.cmd("screendump " + EV + "/fix44_main_iso_ocire_boot.ppm", 1.0)
    time.sleep(2)
    child.terminate(force=True)
except Exception as e:
    print(f"FAILED: {e}")
    child.terminate(force=True)
    failures.append(f"session exception: {e}")
finally:
    try:
        child.close(force=True)
    except Exception:
        pass
    log.close()

# Fake-green #65: propagate the verdict - any failed check => exit 1.
if failures:
    print(f"\n== wp06g3_test_ocire_menu: {len(failures)} check(s) FAILED ==")
    for f in failures:
        print("  FAIL:", f)
    sys.exit(1)
print("\n== wp06g3_test_ocire_menu: all checks PASSED ==")
sys.exit(0)
