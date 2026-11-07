#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - tests/ocse_t3_uefi.py (3.0.0-WP06a-ocse)
# Acceptance T3 (positive, OVMF UEFI): the same OCSE medium booted
# through OVMF. Expected: the EFI flavor of OCBL classifies the volume
# via /ocse.mrk, performs the GOP framebuffer handoff (800x600), loads
# /boot/ocse.elf through the mb2 ELF path after ExitBootServices and
# the low trampoline, the 24-stage ladder runs and the GUI environment
# comes up.
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
       f"-bios /home/z/opt/extract/usr/share/ovmf/OVMF.fd "
       f"-no-reboot -display none -serial stdio -monitor none -vga std")

child = pexpect.spawn(CMD, timeout=240, encoding="utf-8")
child.logfile = sys.stdout
try:
    # OVMF boot + OCBL EFI flavor (scan log appears after BDS)
    child.expect("OCBL 3.0.0 boot loader", timeout=200)
    child.expect("OpenCubeOS Setup", timeout=40)
    child.expect("stage 24/24", timeout=90)
    child.expect(r"checks: cpu=PASS mem=PASS disp=PASS disk=PASS", timeout=30)
    child.expect("environment up: GUI mode", timeout=30)
    print("\n[T3 PASS] OVMF UEFI positive: dual-firmware handoff -> GUI up")
except Exception as e:
    print(f"\n[T3 FAIL] {e}")
    sys.exit(1)
finally:
    child.terminate(force=True)
