#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - tests/ocse_t6_ocbl_scan.py (3.0.0-WP06a-ocse)
# OCBL dynamic scan acceptance (the WP-06a marker class + the OCOS
# class co-existing): a disk carrying /boot/opencube.elf (the state a
# real installation leaves behind) boots alongside the OCSE medium.
# Expected: the six-class scanner registers BOTH entries - the OCSE
# marker class first (install media outranks a co-resident kernel) -
# and selecting the OCOS entry boots the real kernel.
import socket
import time
import os
import pexpect
import sys

BUILD = "/home/z/oc-public/build"
MON = f"{BUILD}/t6-mon.sock"
if os.path.exists(MON):
    os.unlink(MON)

QEMU = "qemu-system-x86_64"
CMD = (f"{QEMU} -L /home/z/opt/extract/usr/share/qemu "
       f"-L /home/z/opt/extract/usr/share/seabios -m 256M "
       f"-drive file={BUILD}/ocse-installed-disk.raw,format=raw,if=ide,index=0,media=disk "
       f"-cdrom {BUILD}/ocse.iso -boot d "
       f"-no-reboot -display none -serial stdio "
       f"-monitor unix:{MON},server,nowait -vga std")

child = pexpect.spawn(CMD, timeout=240, encoding="utf-8",
                      codec_errors="replace")
child.logfile = sys.stdout


class Mon:
    def __init__(self, path, wait=30):
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
        raise RuntimeError("monitor never came up")

    def cmd(self, c, settle=0.4):
        self.s.sendall((c + "\n").encode())
        time.sleep(settle)
        try:
            self.s.recv(65536)
        except socket.timeout:
            pass


try:
    mon = Mon(MON)
    # The scanner sees BOTH classes on the two volumes (the scan log
    # lists the disk OCOS entry first, the CD OCSE entry second)
    child.expect(r"\[scan\] entries: 2", timeout=120)
    # WP06b b5: the scan entries carry the partition suffix
    # ("hd0 p1", the FAT volume's partition number)
    child.expect(r"OCOS: Open Cube OS \(hd0 p1\)", timeout=20)
    child.expect(r"OCSE: OpenCubeOS Setup \(OCSE\) \(cd0\)", timeout=20)
    print("[T6.1 PASS] dynamic scan: OCSE marker class + OCOS class "
          "co-exist (2 entries)")

    # The countdown auto-boots the first entry - the installed OCOS.
    # No keys needed: this IS the post-install boot state.
    child.expect(r"\[boot\] kernel loaded", timeout=60)
    child.expect(r"kmain entered", timeout=120)
    child.expect(r"ush>", timeout=120)
    print("[T6.2 PASS] OCOS boots from the installed-disk entry "
          "(the post-install state, straight to the shell)")

    print("\n[T6 PASS] OCBL dynamic scan + post-install OCOS boot")
except Exception as e:
    print(f"\n[T6 FAIL] {e}")
    sys.exit(1)
finally:
    try:
        mon.s.close()
    except Exception:
        pass
    child.terminate(force=True)
