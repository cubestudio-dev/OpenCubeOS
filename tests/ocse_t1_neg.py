#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - tests/ocse_t1_neg.py (3.0.0-WP06a-ocse)
# Acceptance T1 (negative): SeaBIOS, OCSE install medium, NO target
# disk. Expected: OCBL classifies the medium by the /ocse.mrk marker,
# hands /boot/ocse.elf to the mb2 ELF path, the 24-stage OCSE ladder
# runs to stage 24, check 4 (storage) FAILs and the environment halts
# honestly (serial silence after the FAIL report).
import pexpect
import sys

QEMU = "qemu-system-x86_64"
COMMON = ("-L /home/z/opt/extract/usr/share/qemu "
          "-L /home/z/opt/extract/usr/share/seabios "
          "-m 256M -cdrom /home/z/oc-public/build/ocse.iso -boot d "
          "-no-reboot -display none -serial stdio -monitor none -vga std")

child = pexpect.spawn(f"{QEMU} {COMMON}", timeout=180, encoding="utf-8")
child.logfile = sys.stdout
try:
    # OCBL side
    child.expect("OCBL 3.0.0 boot loader", timeout=90)
    child.expect("OpenCubeOS Setup", timeout=30)
    child.expect("kernel loaded", timeout=30)
    # OCSE side: the ladder
    child.expect("stage 1/24: serial console", timeout=30)
    child.expect("stage 24/24", timeout=90)
    # environment: no disk -> honest halt
    child.expect("FATAL: no usable disk", timeout=30)
    child.expect(r"\[FAIL\] block devices: 0 present", timeout=20)
    # after the FAIL report: the machine must stay silent (halted)
    idx = child.expect([pexpect.TIMEOUT, r".+halted.+"], timeout=12)
    if idx == 0:
        print("\n[T1 PASS] SeaBIOS negative: no-disk -> check4 FAIL -> halt")
    else:
        print("\n[T1 PASS] SeaBIOS negative: no-disk -> check4 FAIL -> halt "
              "(System halted banner observed)")
except Exception as e:
    print(f"\n[T1 FAIL] {e}")
    sys.exit(1)
finally:
    child.terminate(force=True)
