#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - tests/ocse_t7_regress.py (3.0.0-WP06a-ocse)
# Regression battery for the OCBL changes WP-06a made (scan/main/EFI
# entry learned the OCSE class): the two existing payloads must boot
# unchanged on both firmwares.
#   T7.1  OCIRE recovery ISO on SeaBIOS   -> ocire> prompt
#   T7.2  OCIRE recovery ISO on OVMF      -> ocire> prompt
#   T7.3  OCOS release ISO on OVMF        -> ush> prompt (B5-7 chain:
#         GOP framebuffer -> ExitBootServices -> low trampoline -> kmain)
# (OCOS on SeaBIOS is covered by T6.2: it boots to ush from the disk.)
import pexpect
import sys

BUILD = "/home/z/oc-public/build"
QEMU = "qemu-system-x86_64"
COMMON = (f"-L /home/z/opt/extract/usr/share/qemu "
          f"-L /home/z/opt/extract/usr/share/seabios -m 256M "
          f"-no-reboot -display none -serial stdio -monitor none -vga std")


def boot(name, extra, prompt, timeout):
    child = pexpect.spawn(f"{QEMU} {COMMON} {extra}", timeout=timeout,
                          encoding="utf-8", codec_errors="replace")
    child.logfile = sys.stdout
    try:
        child.expect(prompt, timeout=timeout)
        print(f"\n[{name} PASS]")
        return True
    except Exception as e:
        print(f"\n[{name} FAIL] {e}")
        return False
    finally:
        child.terminate(force=True)


ok = True
ok &= boot("T7.1 OCIRE BIOS",
           f"-cdrom {BUILD}/ocire/ocire.iso -boot d",
           r"ocire>", 180)
ok &= boot("T7.2 OCIRE OVMF",
           f"-cdrom {BUILD}/ocire/ocire.iso -boot d "
           f"-bios /home/z/opt/extract/usr/share/ovmf/OVMF.fd",
           r"ocire>", 240)
ok &= boot("T7.3 OCOS OVMF",
           f"-cdrom {BUILD}/opencube.iso -boot d "
           f"-bios /home/z/opt/extract/usr/share/ovmf/OVMF.fd",
           r"ush>", 240)

if ok:
    print("\n[T7 PASS] regression: OCIRE x2 firmwares + OCOS UEFI")
else:
    print("\n[T7 FAIL] at least one regression smoke failed")
    sys.exit(1)
