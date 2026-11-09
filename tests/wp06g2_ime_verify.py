#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 -*-
# Copyright 2026 cubestudio-dev <cubestudio@qq.com> -*-
# Open Cube OS - tests/wp06g2_ime_verify.py (3.0.0-WP06g2-l1_i18n)
# IME service battery (live QEMU):
#   I1  `ime` status shows the phrase library (610 phrases)
#   I2  `ime-test` PASSES the full session battery (engine ni/hao/qqq
#       + phrase-first, smart compose, paging, digit pick, space pick,
#       enter raw letters, backspace, esc, passthrough)
#   I3  the serial stream stays strictly valid UTF-8 throughout
import os
import socket
import sys
import time
import pexpect

BUILD = "/home/z/oc-os/build"
OUT = f"{BUILD}/wp06g2-ime-verify"
MON = f"{OUT}/mon.sock"
os.makedirs(OUT, exist_ok=True)
for p in (MON,):
    if os.path.exists(p):
        os.unlink(p)

QEMU = "qemu-system-x86_64"
CMD = (f"{QEMU} -L /home/z/opt/extract/usr/share/qemu "
       f"-L /home/z/opt/extract/usr/share/seabios -m 256M "
       f"-cdrom {BUILD}/opencube.iso -boot d "
       f"-no-reboot -display none "
       f"-serial stdio "
       f"-monitor unix:{MON},server,nowait -vga std")

print("[ime-verify] boot:", CMD)
child = pexpect.spawn("/bin/bash", ["-c", CMD], timeout=180)
child.logfile_read = open(f"{OUT}/pty-all.log", "wb")

fails = 0


def check(name, cond, detail=""):
    global fails
    tag = "PASS" if cond else "FAIL"
    print(f"[{tag}] {name}" + (f"  ({detail})" if detail and not cond else ""))
    if not cond:
        fails += 1


print("[ime-verify] waiting for the ush prompt...")
if child.expect_exact(["ush>", "login:"], timeout=180) != 0:
    print("[ime-verify] FATAL: no ush prompt")
    child.kill(9)
    sys.exit(1)
check("boot to ush prompt", True)

child.sendline("ime")
time.sleep(1.0)
child.expect_exact("ush>", timeout=60)
log = open(f"{OUT}/pty-all.log", "rb").read().decode(errors="replace")
check("I1 ime status shows 622 phrases", "622" in log and "phrases" in log)

child.sendline("ime-test")
time.sleep(1.5)
child.expect_exact("ush>", timeout=60)
log = open(f"{OUT}/pty-all.log", "rb").read().decode(errors="replace")
check("I2 ime-test session battery PASS",
      "ime-test: PASS" in log or "ime-test：通过" in log,
      log[-300:] if "ime-test" not in log else "")

raw = open(f"{OUT}/pty-all.log", "rb").read()
try:
    raw.decode("utf-8", errors="strict")
    t = True
    why = ""
except UnicodeDecodeError as e:
    t = False
    why = str(e)
check("I3 serial stream strictly valid UTF-8", t, why)

print()
if fails == 0:
    print("wp06g2_ime_verify: ALL PASS (3/3)")
    child.kill(9)
    sys.exit(0)
print(f"wp06g2_ime_verify: {fails} FAILURES")
child.kill(9)
sys.exit(1)
