#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - tests/wp06g1_verify2.py (3.0.0-WP06g1-encoding_fix)
# V4/V5 continuation: CJK backspace wipe width + whole-stream UTF-8
# validity (the pty log is read from disk, not the write-only stream).
import os
import socket
import sys
import time
import pexpect

BUILD = "/home/z/my-project/oc-os/build"
OUT = f"{BUILD}/wp06g1-verify2"
MON = f"{OUT}/mon.sock"
LOG = f"{OUT}/pty-all.log"
os.makedirs(OUT, exist_ok=True)
for p in (MON, LOG):
    if os.path.exists(p):
        os.unlink(p)

QEMU = "qemu-system-x86_64"
CMD = (f"{QEMU} -L /home/z/opt/extract/usr/share/qemu "
       f"-L /home/z/opt/extract/usr/share/seabios -m 256M "
       f"-cdrom {BUILD}/opencube.iso -boot d "
       f"-no-reboot -display none -serial stdio "
       f"-monitor unix:{MON},server,nowait -vga std")

print("[verify2] boot:", CMD)
child = pexpect.spawn("/bin/bash", ["-c", CMD], timeout=180)
logf = open(LOG, "wb")
child.logfile_read = logf

idx = child.expect(["ush>", pexpect.TIMEOUT], timeout=150)
print("[verify2] ush prompt:", idx == 0)
if idx != 0:
    sys.exit(1)

# ---- V4: backspace over a pasted CJK glyph ---------------------------
zh = "中".encode("utf-8")
logf.flush()
mark_before = os.path.getsize(LOG)
child.send(b"echo " + zh)
time.sleep(1.5)
child.send(b"\x7f")            # Backspace over the hanzi
time.sleep(1.5)
logf.flush()
mark_after_bs = os.path.getsize(LOG)
child.send(b"\r")              # now the line is just "echo "
time.sleep(2)
child.expect([b"ush>", pexpect.TIMEOUT], timeout=6)
logf.flush()

with open(LOG, "rb") as f:
    f.seek(mark_before)
    seg = f.read(mark_after_bs - mark_before)
wipes = seg.count(b"\x08 \x08")
print("[V4] raw backspace segment:", repr(seg[:80]))
print("[V4] backspace over CJK glyph: %d '\\b \\b' wipe(s) (expect 2)" % wipes)

# ---- V4b: left/right over a CJK glyph (cursor jumps the whole glyph) --
logf.flush()
mark = os.path.getsize(LOG)
child.send(b"echo " + zh)      # echo 中
time.sleep(1.5)
child.send(b"\x1b[D")          # Left arrow over the hanzi
time.sleep(1.0)
child.send(b"\x1b[C")          # Right arrow back
time.sleep(1.0)
child.send(b"\x7f")            # Backspace (removes whole glyph)
time.sleep(1.0)
child.send(b"\r")
time.sleep(2)
child.expect([b"ush>", pexpect.TIMEOUT], timeout=6)
logf.flush()
with open(LOG, "rb") as f:
    f.seek(mark)
    seg = f.read()
# the redraw after Left must re-print "echo <zhong>" whole; count wipes
wipes2 = seg.count(b"\x08 \x08")
print("[V4b] left/right/backspace segment wipes: %d (expect 2)" % wipes2)

# ---- V5: whole serial stream strictly valid UTF-8 ---------------------
logf.flush()
with open(LOG, "rb") as f:
    allout = f.read()
try:
    allout.decode("utf-8")
    ok = True
except UnicodeDecodeError as e:
    ok = False
    print("[V5] decode error:", e)
print("[V5] whole serial stream strictly valid UTF-8:", ok)

print("\n[verify2] SUMMARY")
print("  V4  CJK backspace 2-cell wipe:", "PASS" if wipes == 2 else "FAIL")
print("  V4b left/right whole-glyph + wipe:", "PASS" if wipes2 == 2 else "FAIL")
print("  V5  serial stream valid UTF-8:", "PASS" if ok else "FAIL")

child.send(b"poweroff\r")
time.sleep(3)
try:
    child.terminate(force=True)
except Exception:
    pass
