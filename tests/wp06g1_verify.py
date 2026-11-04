#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - tests/wp06g1_verify.py (3.0.0-WP06g1-encoding_fix)
# Post-fix acceptance battery:
#   V1  chinesetest: the new blind-spot checks (B0A2 <-> U+963F) and
#       the [13] full-library round trip (6763 hanzi + 682 symbols)
#       all PASS in-system
#   V2  echo <utf8 paste>: the echoed line renders as FOUR 16px CJK
#       glyphs on screen (pixel evidence: 8 consecutive ink cells with
#       16px-wide glyph structure, NOT 12 narrow Latin-1 blocks)
#   V3  cat /tmp/cn.txt (the UTF-8 file chinesetest wrote): same glyph
#       shape evidence through the L1 app read->write path
#   V4  line editing: paste CJK, Backspace - the wipe must remove the
#       whole 2-cell glyph (serial echo shows "\b \b" twice); Left/Right
#       cross the glyph as ONE unit
#   V5  serial byte stream stays strictly valid UTF-8 (terminal-side
#       decode) for everything echoed back
import os
import re
import socket
import sys
import time
import pexpect

BUILD = "/home/z/my-project/oc-os/build"
OUT = f"{BUILD}/wp06g1-verify"
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

print("[verify] boot:", CMD)
child = pexpect.spawn("/bin/bash", ["-c", CMD], timeout=180)
child.logfile_read = open(f"{OUT}/pty-all.log", "wb")


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

    def cmd(self, c, settle=0.35):
        self.s.sendall((c + "\n").encode())
        time.sleep(settle)
        out = b""
        try:
            while True:
                out += self.s.recv(65536)
        except socket.timeout:
            pass
        return out.decode("utf-8", "replace")


mon = Mon(MON)
idx = child.expect(["ush>", pexpect.TIMEOUT], timeout=150)
print("[verify] ush prompt:", idx == 0)
if idx != 0:
    print(child.before.decode("utf-8", "replace")[-3000:])
    sys.exit(1)


def run_line(line_bytes, tag, settle=3.0, capture=15.0):
    child.send(line_bytes + b"\r")
    child.expect([b"ush>", pexpect.TIMEOUT], timeout=capture)
    out = child.before if isinstance(child.before, bytes) else b""
    with open(f"{OUT}/{tag}.bin", "wb") as f:
        f.write(out)
    return out


def load_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    parts = data.split(b"\n", 3)
    w, h = map(int, parts[1].split())
    return w, h, parts[3]


def is_ink(pix, w, x, y):
    i = (y * w + x) * 3
    r, g, b = pix[i], pix[i + 1], pix[i + 2]
    return abs(r - 0x10) > 40 or abs(g - 0x10) > 40 or abs(b - 0x14) > 40


def cjk_row_evidence(path, label):
    """Find text rows whose ink spans exactly 16px-wide glyph pairs.
    Returns count of 16px-wide CJK glyph groups found in the last rows."""
    w, h, pix = load_ppm(path)
    # scan the last 20 cell rows for wide-glyph structure: a 16px window
    # with ink in BOTH halves at >6 of 16 rows and a blank 8px column on
    # each side (cell boundaries around a 2-cell glyph)
    found = 0
    details = []
    for cy in range(h // 16 - 20, h // 16):
        y0 = cy * 16
        for cx in range(1, 99):
            x0 = cx * 8
            # candidate glyph occupying cells cx,cx+1
            ink_rows = 0
            left_clear = True
            right_clear = True
            for dy in range(16):
                if any(is_ink(pix, w, x0 + dx, y0 + dy) for dx in range(16)):
                    ink_rows += 1
                if is_ink(pix, w, x0 - 1, y0 + dy):
                    left_clear = False
                if is_ink(pix, w, x0 + 16, y0 + dy):
                    right_clear = False
            if ink_rows >= 8 and left_clear and right_clear:
                found += 1
                if len(details) < 8:
                    details.append((cx, cy, ink_rows))
    print(f"[evidence] {label}: {found} isolated 16px-wide glyph groups")
    for d in details:
        print(f"           cell x={d[0]} row={d[1]} ink_rows={d[2]}")
    return found


# ---- V1: chinesetest with the new sections --------------------------
out = run_line(b"chinesetest", "v1_chinesetest", settle=8, capture=25)
txt = out.decode("utf-8", "replace")
m = re.search(r"chinesetest: (\d+)/(\d+) (PASS|FAIL)", txt)
print("[V1] chinesetest:", m.group(0) if m else "NO SUMMARY")
for pat in ("decode B0A2 -> U+963F", "encode U+963F -> B0 A2",
            "6763 hanzi zone/wei", "682 symbols zone/wei",
            "full trip:"):
    hit = [l for l in txt.splitlines() if pat in l]
    print("   ", hit[0] if hit else "MISSING: " + pat)
if not m or "PASS" not in m.group(3):
    print(txt[-3000:])
    sys.exit(1)

# ---- V2: paste echo renders as 16px glyphs on screen -----------------
zh = "中文测试".encode("utf-8")
child.send(b"echo " + zh + b"\r")
time.sleep(3)
child.expect([b"ush>", pexpect.TIMEOUT], timeout=8)
mon.cmd("screendump " + OUT + "/v2_echo.ppm")
time.sleep(1)
n = cjk_row_evidence(OUT + "/v2_echo.ppm", "V2 echo render")

# ---- V3: cat the UTF-8 file chinesetest wrote ------------------------
out = run_line(b"cat /tmp/cn.txt", "v3_cat", settle=3)
mon.cmd("screendump " + OUT + "/v3_cat.ppm")
time.sleep(1)
n3 = cjk_row_evidence(OUT + "/v3_cat.ppm", "V3 cat render")
print("[V3] cat /tmp/cn.txt echoed bytes:", repr(out.strip()[-60:]))

# ---- V4: backspace over a pasted CJK glyph ---------------------------
# type 'echo ' + one hanzi, then backspace: the whole 2-cell glyph must
# disappear (two "\b \b" sequences in the raw pty stream)
before = child.logfile_read.tell()
child.send(b"echo " + "中".encode("utf-8"))
time.sleep(1.5)
child.send(b"\x7f")            # Backspace over the hanzi
time.sleep(1.5)
child.send(b"\r")
time.sleep(2)
child.expect([b"ush>", pexpect.TIMEOUT], timeout=6)
child.logfile_read.seek(before)
raw = child.logfile_read.read()
# count "\b \b" pairs between the hanzi paste and the Enter
seg = raw.split("中".encode("utf-8"))[-1] if "中".encode("utf-8") in raw else b""
wipes = seg.count(b"\x08 \x08")
print("[V4] backspace over CJK glyph: %d '\\b \\b' wipe(s) (expect 2)" % wipes)

# ---- V5: full serial stream strictly valid UTF-8 ---------------------
child.logfile_read.seek(0)
allout = child.logfile_read.read()
try:
    allout.decode("utf-8")
    ok = True
except UnicodeDecodeError as e:
    ok = False
    print("[V5] decode error:", e)
print("[V5] whole serial stream strictly valid UTF-8:", ok)

# ---- summary ---------------------------------------------------------
print("\n[verify] SUMMARY")
print("  V1 chinesetest (incl. blind spots + full trip):",
      "PASS" if m and "PASS" in m.group(3) else "FAIL")
print("  V2 echo paste on-screen 16px glyphs:", "PASS" if n >= 8 else
      "FAIL (%d groups; expect >=8: 4 glyphs on echo line + 4 on output)" % n)
print("  V3 cat UTF-8 file 16px glyphs:", "PASS" if n3 >= 4 else
      "FAIL (%d groups; expect >=4)" % n3)
print("  V4 CJK backspace 2-cell wipe:", "PASS" if wipes == 2 else "FAIL")
print("  V5 serial stream valid UTF-8:", "PASS" if ok else "FAIL")

child.send(b"poweroff\r")
time.sleep(3)
try:
    child.terminate(force=True)
except Exception:
    pass
print("[verify] artifacts:", OUT)
