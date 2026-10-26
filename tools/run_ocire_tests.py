#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - 3.0.0-WP-01-ocire-core
# File: tools/run_ocire_tests.py
# Purpose: Automated QEMU test battery for the OCIRE independent
#          recovery kernel (standalone ISO image).
#
# Cases (per firmware, BIOS and UEFI):
#   boot      - GRUB menu -> OCIRE banner -> shell prompt appears
#   help      - 'help' lists help/shutdown/reboot
#   unknown   - an unknown command gets an honest error hint
#   backspace - line editing: backspace erases the echoed char
#   shutdown  - 'shutdown' prints the notice and QEMU exits (S5)
#   reboot    - 'reboot' prints the notice and QEMU exits (-no-reboot)
#
# Each case boots its own QEMU so shutdown/reboot are real end states.
# Full serial transcripts are written to build/ocire/test-*.log.

import os
import shutil
import sys
import pexpect

OC_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
OC = os.environ.get("OC_TOOLS", "/home/z/opt/extract")
QEMU = os.path.join(OC, "usr/bin/qemu-system-x86_64")
QDATA = os.path.join(OC, "usr/share/qemu")
SEA = os.path.join(OC, "usr/share/seabios")
OVMF = os.path.join(OC, "usr/share/ovmf/OVMF.fd")
ISO = os.path.join(OC_ROOT, "build/ocire/ocire.iso")
OUT = os.path.join(OC_ROOT, "build/ocire")

ENV = dict(os.environ)
ENV["LD_LIBRARY_PATH"] = (
    os.path.join(OC, "usr/lib/x86_64-linux-gnu") + ":"
    + os.path.join(OC, "lib/x86_64-linux-gnu") + ":"
    + os.path.join(OC, "usr/lib"))

RESULTS = []


def qemu_cmd(uefi, extra=None):
    a = [QEMU, "-L", QDATA, "-L", SEA,
         "-m", "128M", "-cdrom", ISO, "-boot", "d",
         "-no-reboot", "-display", "none", "-serial", "stdio",
         "-monitor", "none", "-vga", "std"]
    if uefi:
        work = os.path.join(OUT, "OVMF_TEST.fd")
        shutil.copyfile(OVMF, work)
        a += ["-drive", f"if=pflash,format=raw,file={work}"]
    if extra:
        a += extra
    return a


def logname(case, uefi):
    return os.path.join(OUT, f"test-{case}-{'uefi' if uefi else 'bios'}.log")


def record(case, uefi, ok, note=""):
    RESULTS.append((case, uefi, ok, note))
    print(f"[{'PASS' if ok else 'FAIL'}] {case} ({'UEFI' if uefi else 'BIOS'})"
          + (f" - {note}" if note else ""))


def expect_boot(child, label, timeout=120):
    i = child.expect(["ocire>", "OCIRE FAULT", pexpect.EOF, pexpect.TIMEOUT])
    if i == 0:
        return True
    record(label, child.uefi_flag, False,
           "no shell prompt" if i == 1 else
           "EOF before prompt" if i == 2 else "timeout")
    return False


def case_boot_and_shell(uefi, interactive_cmds, label, base):
    """Boot to the shell, run interactive (non-power) checks, then quit."""
    child = pexpect.spawn(qemu_cmd(uefi)[0], qemu_cmd(uefi)[1:], timeout=120,
                          encoding="utf-8", codec_errors="replace", env=ENV)
    child.uefi_flag = uefi
    try:
        with open(logname(base, uefi), "w") as lf:
            child.logfile = lf
            if not expect_boot(child, f"{label}/boot", uefi):
                return False
            for send, want, sub in interactive_cmds:
                child.sendline(send)
                i = child.expect([want, pexpect.TIMEOUT])
                record(f"{label}:{sub}", uefi, i == 0)
            return True
    finally:
        child.close(force=True)


def case_power(uefi, cmd, want, label, base):
    """Boot to the shell, send a power command, expect QEMU to exit."""
    child = pexpect.spawn(qemu_cmd(uefi)[0], qemu_cmd(uefi)[1:], timeout=120,
                          encoding="utf-8", codec_errors="replace", env=ENV)
    child.uefi_flag = uefi
    try:
        with open(logname(base, uefi), "w") as lf:
            child.logfile = lf
            if not expect_boot(child, f"{label}/boot", uefi):
                return False
            child.sendline(cmd)
            i = child.expect([want, pexpect.TIMEOUT])
            if i != 0:
                record(f"{label}:notice", uefi, False, f"no {want!r} output")
                return False
            record(f"{label}:notice", uefi, True)
            # QEMU must exit (S5 poweroff / reset with -no-reboot).
            j = child.expect([pexpect.EOF, pexpect.TIMEOUT])
            record(f"{label}:qemu-exit", uefi, j == 0,
                   "" if j == 0 else "QEMU still running")
            return j == 0
    finally:
        child.close(force=True)


def main():
    if not os.path.exists(ISO):
        print(f"FATAL: {ISO} missing (run: make ocire-iso)", file=sys.stderr)
        return 1
    for uefi in (False, True):
        tag = "uefi" if uefi else "bios"

        # boot + banner + help + unknown-command + backspace editing
        case_boot_and_shell(uefi, [
            ("help", "shutdown", "help"),
            ("kermit", "Unknown command: kermit", "unknown"),
            # 'helx' + BS + 'p' + Enter: backspace must delete the 'x'
            # so the kernel sees a clean 'help' (functional BS proof).
            ("helx\x08p", "shutdown", "backspace"),
        ], "shell", f"shell-{tag}")

        # Shutdown: real S5 -> QEMU exits
        case_power(uefi, "shutdown", "Shutting down",
                   "shutdown", f"shutdown-{tag}")
        # Reboot: 8042/RESET_REG -> reset -> QEMU exits (-no-reboot)
        case_power(uefi, "reboot", "Rebooting",
                   "reboot", f"reboot-{tag}")

    fails = [r for r in RESULTS if not r[2]]
    print("\n===== OCIRE test battery summary =====")
    print(f"total cases: {len(RESULTS)}, passed: {len(RESULTS)-len(fails)}, "
          f"failed: {len(fails)}")
    for case, uefi, ok, note in fails:
        print(f"  FAIL: {case} ({'UEFI' if uefi else 'BIOS'}) {note}")
    print(f"logs: {OUT}/test-*.log")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
