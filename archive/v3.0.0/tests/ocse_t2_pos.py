#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - tests/ocse_t2_pos.py (3.0.0-WP06a-ocse)
# Acceptance T2 (positive, SeaBIOS): OCSE medium + a 64 MiB IDE disk.
# Expected: all four checks PASS and the GUI environment comes up
# (serial: "[ocse] environment up: GUI mode"), disk panel shows the
# target disk. The disk is intentionally blank (no partition table):
# the installer must still report it honestly ([no table]).
import os
import pexpect
import sys

DISK = "/home/z/oc-public/build/ocse-test-disk64.raw"
if not os.path.exists(DISK) or os.path.getsize(DISK) != 64 * 1024 * 1024:
    with open(DISK, "wb") as f:
        f.truncate(64 * 1024 * 1024)

QEMU = "qemu-system-x86_64"
CMD = (f"{QEMU} -L /home/z/opt/extract/usr/share/qemu "
       f"-L /home/z/opt/extract/usr/share/seabios -m 256M "
       f"-drive file={DISK},format=raw,if=ide,index=0,media=disk "
       f"-cdrom /home/z/oc-public/build/ocse.iso -boot d "
       f"-no-reboot -display none -serial stdio -monitor none -vga std")

child = pexpect.spawn(CMD, timeout=180, encoding="utf-8")
child.logfile = sys.stdout
try:
    child.expect("stage 24/24", timeout=120)
    child.expect(r"checks: cpu=PASS mem=PASS disp=PASS disk=PASS", timeout=30)
    child.expect("environment up: GUI mode", timeout=30)
    print("\n[T2 PASS] SeaBIOS positive: 64MB disk -> 4x PASS -> GUI up")
except Exception as e:
    print(f"\n[T2 FAIL] {e}")
    sys.exit(1)
finally:
    child.terminate(force=True)
