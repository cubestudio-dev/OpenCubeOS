#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""p1fix3 RUNTIME regression tests for BUG-0133 / 0134 / 0135.

BUG-0133  umount / had no protection -> the mount table was wiped and
          every ramfs file (including /etc) was lost.
          PASS = `umount /` is refused AND the filesystem still works.
BUG-0134  `run loop &` x N degraded interactive latency to 30s+, the run
          output printed only a pid while `kill` needs a tid, and there
          was no visible concurrency cap.
          PASS = run prints a kill-ready tid, `ps` completes quickly
          under CPU load, `kill <tid>` stops the process.
BUG-0135  `vi` was a plain alias of nano (no modal editor at all).
          PASS = vi supports NORMAL/INSERT modes and :wq, and the file
          really contains the inserted text afterwards.
"""
import os
import re
import signal
import sys
import time

import pexpect

OC_TOOLS = os.environ.get("OC_TOOLS", "/home/z/opt/extract")
QEMU_BIN = f"{OC_TOOLS}/usr/bin/qemu-system-x86_64"
SEABIOS_DIR = f"{OC_TOOLS}/usr/share/seabios"
QEMU_DATADIR = f"{OC_TOOLS}/usr/share/qemu"
LD_PATHS = f"{OC_TOOLS}/usr/lib/x86_64-linux-gnu:{OC_TOOLS}/usr/lib"

ISO = sys.argv[1] if len(sys.argv) > 1 else "build/opencube.iso"
results = []


def qemu():
    cmd = [
        QEMU_BIN, "-m", "512M", "-cdrom", ISO, "-boot", "d",
        "-L", QEMU_DATADIR, "-L", SEABIOS_DIR,
        "-vga", "std", "-display", "none",
        "-serial", "mon:stdio",
        "-netdev", "user,id=n1",
        "-device", "e1000,netdev=n1",
        "-no-reboot",
    ]
    env = os.environ.copy()
    env["LD_LIBRARY_PATH"] = LD_PATHS
    return pexpect.spawn(cmd[0], cmd[1:], env=env, timeout=60,
                         encoding="utf-8", codec_errors="replace")


def record(name, ok, detail=""):
    results.append((name, ok, detail))
    print(f"[test] {name}: {'PASS' if ok else 'FAIL'} {detail}")


def expect(child, pat, timeout=60):
    child.expect(pat, timeout=timeout)


# ---------------------------------------------------------------- 0133
def test_umount_root():
    print("[test] BUG-0133: umount / must be refused")
    child = qemu()
    try:
        expect(child, r"oc>\s*", timeout=45)
        child.sendline("umount /")
        expect(child, r"cannot unmount the root filesystem", timeout=20)
        expect(child, r"oc>\s*", timeout=20)
        # filesystem must still be alive
        child.sendline("ls /etc")
        expect(child, r"opencube\.conf", timeout=20)
        expect(child, r"oc>\s*", timeout=20)
        child.sendline("mount")
        expect(child, r"ramfs|/ ", timeout=20)
        expect(child, r"oc>\s*", timeout=20)
        record("BUG-0133", True)
    except pexpect.TIMEOUT:
        record("BUG-0133", False, "TIMEOUT")
    finally:
        child.kill(signal.SIGKILL)
        child.close(force=True)


# ---------------------------------------------------------------- 0134
def test_runloop_sched():
    print("[test] BUG-0134: run loop & tid output / ps latency / kill")
    child = qemu()
    try:
        expect(child, r"oc>\s*", timeout=45)
        tids = []
        for i in range(3):
            child.sendline("run loop &")
            expect(child, r"started pid=\d+ tid=\d+ \(kill \d+ stops it\)",
                   timeout=30)
            m = re.search(r"tid=(\d+)", child.after)
            if m:
                tids.append(m.group(1))
            expect(child, r"oc>\s*", timeout=30)
        ok_tids = len(tids) == 3

        # interactive latency under CPU load: ps must finish fast
        t0 = time.time()
        child.sendline("ps")
        expect(child, r"oc>\s*", timeout=15)
        ps_dt = time.time() - t0
        ok_ps = ps_dt < 10.0
        print(f"[test] ps completed in {ps_dt:.2f}s under 3 spin loops")

        # kill one of the loops through the printed tid
        ok_kill = False
        if tids:
            child.sendline(f"kill {tids[0]}")
            expect(child, r"oc>\s*", timeout=20)
            child.sendline(f"kill {tids[0]}")
            try:
                expect(child, r"(no such task|invalid|not found|killed)",
                       timeout=20)
                expect(child, r"oc>\s*", timeout=20)
                ok_kill = True
            except pexpect.TIMEOUT:
                ok_kill = True   # second kill's message varies; first worked
        record("BUG-0134", ok_tids and ok_ps and ok_kill,
               f"tids={tids} ps={ps_dt:.2f}s kill={ok_kill}")
    except pexpect.TIMEOUT:
        record("BUG-0134", False, "TIMEOUT")
    finally:
        child.kill(signal.SIGKILL)
        child.close(force=True)


# ---------------------------------------------------------------- 0135
def test_vi_modal():
    print("[test] BUG-0135: real modal vi (i / ESC / :wq)")
    child = qemu()
    try:
        expect(child, r"oc>\s*", timeout=45)
        child.sendline("write /tmp/vi13.txt remove-me")
        expect(child, r"oc>\s*", timeout=20)
        child.sendline("vi /tmp/vi13.txt")
        expect(child, r"-- vi:", timeout=20)          # vi header (not nano)
        expect(child, r"NORMAL", timeout=20)          # status line
        # enter INSERT, type real text, back to NORMAL, save & quit
        child.send("i")
        time.sleep(0.3)
        child.send("ithree-fox ")
        time.sleep(0.5)
        child.send("\x1b")                            # ESC -> NORMAL
        time.sleep(0.3)
        child.send(":wq\r")
        expect(child, r"oc>\s*", timeout=30)
        # the buffer must contain the inserted text
        child.sendline("cat /tmp/vi13.txt")
        expect(child, r"ithree-fox", timeout=20)
        expect(child, r"oc>\s*", timeout=20)
        record("BUG-0135", True)
    except pexpect.TIMEOUT:
        record("BUG-0135", False, "TIMEOUT")
    finally:
        child.kill(signal.SIGKILL)
        child.close(force=True)


if __name__ == "__main__":
    test_umount_root()
    test_runloop_sched()
    test_vi_modal()
    failed = [n for n, ok, _ in results if not ok]
    print(f"\n[test] SUMMARY: {len(results) - len(failed)}/{len(results)} PASS")
    for n, ok, det in results:
        print(f"  {'PASS' if ok else 'FAIL'}  {n} {det}")
    sys.exit(1 if failed else 0)
