#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - 3.0.0-WP02-ocire_recovery
# File: tools/run_ocire_shell_tests.py
# Purpose: WP-02 group-4 battery - the full recovery shell commands
#          against the real dual-boot disk in QEMU (BIOS).
#
# Cases (all pick GRUB menu entry 2 "OCIRE recovery (full shell)",
# selected over the serial console with the ANSI cursor-down + CR -
# the same recipe the WP-01 dual battery verified):
#   shell_healthy      - help/logread/syscheck/repair-quick on a
#                        pristine copy: 10/10 OK, nothing to repair
#   shell_quick_repair - flip a byte in hd0's OPENCUBE.ELF magic:
#                        syscheck 9 catches DIGEST MISMATCH, repair
#                        quick restores + verifies, syscheck 10/10
#   shell_full_repair  - nuke hd0's FAT32 BPB: syscheck 3 FAIL,
#                        repair full (typed YES) rebuilds the volume
#                        from the golden image, syscheck 10/10
#
# Every case boots THROWAWAY copies (QEMU drives are writable).

import os
import shutil
import sys
import time
import pexpect

OC_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
OC = os.environ.get("OC_TOOLS", "/home/z/opt/extract")
QEMU = os.path.join(OC, "usr/bin/qemu-system-x86_64")
QDATA = os.path.join(OC, "usr/share/qemu")
SEA = os.path.join(OC, "usr/share/seabios")
OUT = os.path.join(OC_ROOT, "build/ocire")

ENV = dict(os.environ)
ENV["LD_LIBRARY_PATH"] = (
    os.path.join(OC, "usr/lib/x86_64-linux-gnu") + ":"
    + os.path.join(OC, "usr/lib/x86_64-linux-gnu") + ":"
    + os.path.join(OC, "lib/x86_64-linux-gnu"))

RESULTS = []
COUNTER = [0]


def record(case, ok, note=""):
    RESULTS.append((case, ok, note))
    print(f"[{'PASS' if ok else 'FAIL'}] {case}" + (f" - {note}" if note else ""))


def throwaway(tag):
    d0 = os.path.join(OUT, f"boot-{tag}.img")
    d1 = os.path.join(OUT, f"hd-{tag}.img")
    shutil.copyfile(os.path.join(OUT, "ocire-boot.img"), d0)
    shutil.copyfile(os.path.join(OUT, "ocire-hd.img"), d1)
    return d0, d1


def spawn_full_shell(disk0, disk1, log_path, timeout=300):
    args = [QEMU, "-L", QDATA, "-L", SEA, "-m", "256M",
            "-drive", f"if=ide,index=0,format=raw,file={disk0}",
            "-drive", f"if=ide,index=1,format=raw,file={disk1}",
            "-no-reboot", "-display", "none", "-serial", "stdio",
            "-monitor", "none", "-vga", "std", "-boot", "c"]
    child = pexpect.spawn(args[0], args[1:], timeout=timeout,
                          encoding="utf-8", codec_errors="replace", env=ENV)
    child.logfile = open(log_path, "w")
    # GRUB menu (serial console input is enabled by the menu config):
    # cursor-down cancels the 8 s timeout and highlights entry 2.
    i = child.expect_exact(["GNU GRUB", pexpect.TIMEOUT, pexpect.EOF],
                            timeout=120)
    if i != 0:
        child.logfile.close()
        child.close(force=True)
        raise RuntimeError("GRUB menu never appeared on serial")
    time.sleep(1.5)
    child.send("\x1b[B")                     # cursor-down -> entry 2
    time.sleep(1.5)
    child.send("\r")                          # boot it
    i = child.expect_exact(["ocire>", pexpect.TIMEOUT, pexpect.EOF],
                           timeout=120)
    if i != 0:
        child.logfile.close()
        child.close(force=True)
        raise RuntimeError("no ocire> prompt after menu pick")
    return child


def run(child, cmd, want, timeout=180):
    child.sendline(cmd)
    i = child.expect_exact([want, pexpect.TIMEOUT, pexpect.EOF],
                           timeout=timeout)
    return i == 0


def case_healthy():
    case = "shell_healthy"
    d0, d1 = throwaway("smoke1")
    try:
        c = spawn_full_shell(d0, d1, os.path.join(OUT, "shell-healthy.log"))
    except RuntimeError as e:
        record(case, False, str(e))
        return
    try:
        ok = True
        ok &= run(c, "help", "repair quick")
        ok &= run(c, "help", "return")
        ok &= run(c, "logread", "no entries recorded")
        ok &= run(c, "logread", "bytes capacity")
        ok &= run(c, "syscheck", "System healthy")
        ok &= run(c, "syscheck", "checks OK, 0 problems found")
        ok &= run(c, "repair quick", "Result: quick repair complete")
        ok &= run(c, "repair quick", "5 unchanged, 0 failed")
        ok &= run(c, "syscheck", "System healthy")
        record(case, ok, "help/logread/syscheck/repair-quick on pristine copy")
    finally:
        c.logfile.close()
        c.close(force=True)


def case_quick_repair():
    case = "shell_quick_repair"
    d0, d1 = throwaway("smoke2")
    # Corrupt the kernel ELF magic on hd0 p1: scan the first 2 MiB of
    # the partition for the first \x7fELF (the kernel is the first
    # file placed on the fresh volume - nothing else is an ELF there).
    with open(d0, "r+b") as f:
        base = 2048 * 512
        f.seek(base)
        blob = f.read(2 * 1024 * 1024)
        off = blob.find(b"\x7fELF")
        if off < 0:
            record(case, False, "kernel ELF magic not found on hd0")
            return
        f.seek(base + off)
        f.write(b"\x7e")
    try:
        c = spawn_full_shell(d0, d1, os.path.join(OUT, "shell-damaged.log"))
    except RuntimeError as e:
        record(case, False, str(e))
        return
    try:
        ok = True
        ok &= run(c, "syscheck", "DIGEST MISMATCH")
        ok &= run(c, "syscheck", "problems found")
        ok &= run(c, "repair quick", "RESTORED (")
        ok &= run(c, "repair quick", "bytes, verified)")
        ok &= run(c, "repair quick", "1 restored, 4 unchanged, 0 failed")
        ok &= run(c, "syscheck", "System healthy")
        record(case, ok, "digest damage -> syscheck catches -> quick repair")
    finally:
        c.logfile.close()
        c.close(force=True)


def case_full_repair():
    """BPB-nuke case.  With hd0 p1's boot sector destroyed, hd0's own
    GRUB cannot read its config (the menu lives on that FAT32) - so
    the machine is booted from the STANDALONE OCIRE ISO (its config
    is embedded, no disk dependency), the realistic 'bootloader
    broken' recovery path.  The ISO menu auto-boots (timeout=1) and
    routes serial input straight to the kernel (no menu keys)."""
    case = "shell_full_repair"
    d0, d1 = throwaway("smoke3")
    # Destroy the p1 FAT32 BPB (boot sector at LBA 2048).
    with open(d0, "r+b") as f:
        f.seek(2048 * 512)
        f.write(b"\xde\xad\xbe\xef" * 128)
    iso = os.path.join(OUT, "ocire.iso")
    if not os.path.exists(iso):
        record(case, False, "ocire.iso missing (make ocire-iso)")
        return
    args = [QEMU, "-L", QDATA, "-L", SEA, "-m", "256M",
            "-cdrom", iso, "-boot", "d",
            "-drive", f"if=ide,index=0,format=raw,file={d0}",
            "-drive", f"if=ide,index=1,format=raw,file={d1}",
            "-no-reboot", "-display", "none", "-serial", "stdio",
            "-monitor", "none", "-vga", "std"]
    try:
        child = pexpect.spawn(args[0], args[1:], timeout=300,
                              encoding="utf-8", codec_errors="replace",
                              env=ENV)
        child.logfile = open(os.path.join(OUT, "shell-fullrepair.log"), "w")
    except RuntimeError as e:
        record(case, False, str(e))
        return
    try:
        ok = True
        steps = []
        def step(name, val):
            steps.append((name, bool(val)))
            return bool(val)
        ok &= step("prompt", run(child, "ocire>", "ocire>",
                                 timeout=120))
        ok &= step("syscheck-fail", run(child, "syscheck", "FAIL", timeout=180))
        ok &= step("syscheck-problems", run(child, "syscheck", "problems found", timeout=180))
        child.sendline("repair full")
        i = child.expect_exact(["Type YES to continue:", pexpect.TIMEOUT,
                                pexpect.EOF], timeout=60)
        ok &= step("confirm", i == 0)
        child.sendline("YES")
        i = child.expect_exact(["full repair complete", pexpect.TIMEOUT,
                                pexpect.EOF], timeout=400)
        ok &= step("repair-complete", i == 0)
        ok &= step("syscheck-healthy", run(child, "syscheck", "System healthy", timeout=180))
        ok &= step("logread", run(child, "logread", "no entries recorded", timeout=120))
        for name, val in steps:
            print(f"    [{name}] {'ok' if val else '*** FAIL'}")
        record(case, ok, "BPB nuked -> ISO boot -> repair full YES -> healthy")
    finally:
        child.logfile.close()
        child.close(force=True)


def case_wdt_timeout():
    """3.0.0-WP03 G4: the supervised watchdog loop, CLOSED.

    The WP-02 edition of this case verified the honest-boundary
    behaviour: OCOS never signalled and never disarmed, so the 60 s
    window expired, the machine reset, and the supervisor degraded
    into the full recovery shell (that evidence lives in
    evidence/3_0_0_wp02_recovery/shell-wdt-timeout.log and stays
    valid for a genuinely-hung OCOS - the OCIRE side is frozen).

    WP-03 G4 closes the loop on the OCOS side: the embed service now
    writes SIGNAL_OK and DISARMS the i6300esb inside the window, so a
    HEALTHY supervised boot must stay up past the old 60 s horizon.
    The case is therefore re-pointed at the new contract: armed ->
    OCOS disarms -> machine ALIVE at t>75 s (the on-disk proof of the
    live agent cycles is the supervised battery's wdt case; this case
    pins the passive end-to-end observation from the boot choreography
    itself).

    Needs real resets allowed: no -no-reboot on this spawn."""
    case = "shell_wdt_timeout"
    d0, d1 = throwaway("smoke4")
    args = [QEMU, "-L", QDATA, "-L", SEA, "-m", "256M",
            "-device", "i6300esb",
            "-drive", f"if=ide,index=0,format=raw,file={d0}",
            "-drive", f"if=ide,index=1,format=raw,file={d1}",
            "-display", "none", "-serial", "stdio",
            "-monitor", "none", "-vga", "std", "-boot", "c"]
    try:
        child = pexpect.spawn(args[0], args[1:], timeout=240,
                              encoding="utf-8", codec_errors="replace",
                              env=ENV)
        child.logfile = open(os.path.join(OUT, "shell-wdt-timeout.log"), "w")
    except Exception as e:                                  # pragma: no cover
        record(case, False, f"spawn failed: {e}")
        return
    try:
        ok = True

        def await_output(want, label, timeout=180):
            # Passive observation: nothing is sent - the boot
            # choreography runs on its own.
            i = child.expect_exact([want, pexpect.TIMEOUT, pexpect.EOF],
                                   timeout=timeout)
            good = (i == 0)
            if not good:
                print(f"    [{label}] *** no {want!r} within {timeout}s")
            return good

        # 1. GRUB menu, default entry (8 s timeout) = supervisor
        ok &= await_output("GNU GRUB", "grub", timeout=120)
        ok &= await_output("OCIRE boot supervisor: watching the "
                           "Open Cube OS boot.", "supervisor banner")
        ok &= await_output("Watchdog armed (i6300esb): 60 s boot window.",
                           "wdt armed", timeout=60)
        # 2. The OCOS embed closes the loop: facts + signal + DISARM
        ok &= await_output("ocire embed: SUPERVISED boot",
                           "ocos embed facts", timeout=180)
        ok &= await_output("boot signal written (SIGNAL_OK, boot_count+1)",
                           "boot signal", timeout=30)
        ok &= await_output("ocire embed: i6300esb watchdog DISARMED",
                           "wdt disarmed", timeout=30)
        ok &= await_output("ush>", "ocos prompt", timeout=60)
        # 3. t+75 s: the machine must STILL be at the prompt - the old
        #    60 s reset horizon is closed by the disarm.  (The reset
        #    path itself stays proven by the WP-02 evidence for a
        #    hung OCOS; nothing in ocire/ changed.)
        print("    (waiting past the 60 s window...)")
        time.sleep(62)
        child.sendline("stats")
        i = child.expect_exact(["ticks=", pexpect.TIMEOUT, pexpect.EOF],
                               timeout=30)
        ok &= (i == 0)
        record(case, ok, "WP-03 G4: wdt armed -> OCOS SIGNAL_OK + DISARM "
                         "-> machine alive past the 60 s window "
                         "(WP-02 degrade flow stays proven for a hung "
                         "OCOS by the frozen evidence)")
    finally:
        child.logfile.close()
        child.close(force=True)


def main():
    if not os.path.exists(os.path.join(OUT, "ocire-boot.img")):
        print("[FATAL] build/ocire disks missing (make ocire-disk)", file=sys.stderr)
        return 1
    only = sys.argv[1] if len(sys.argv) > 1 else None
    if not only or only == "healthy":
        case_healthy()
    if not only or only == "quick":
        case_quick_repair()
    if not only or only == "full":
        case_full_repair()
    if not only or only == "wdt":
        case_wdt_timeout()
    fails = [r for r in RESULTS if not r[1]]
    print(f"\n{len(RESULTS) - len(fails)}/{len(RESULTS)} passed")
    for c, ok, note in RESULTS:
        if not ok:
            print(f"  FAIL: {c} - {note}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
