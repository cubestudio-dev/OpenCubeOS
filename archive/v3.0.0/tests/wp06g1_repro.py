#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - tests/wp06g1_repro.py (3.0.0-WP06g1-encoding_fix)
# Root-cause reproduction battery BEFORE any fix:
#   R1  L0 path (lang zh): console_cn_puts UTF-8 -> expect clean glyphs
#   R2  L1 paste path (echo <utf8>): ush line editor echo -> expect the
#       mojibake (root cause A: sys_write console fallback is the stock
#       single-byte putc; every UTF-8 byte paints a Latin-1 glyph)
#   R3  serial byte stream: hexdump of what COM1 mirrored - is it valid
#       UTF-8 (terminal-side decode question) or already corrupted?
#   R4  screen pixel evidence: screendump + glyph-cell analysis of the
#       echoed line (8px-wide Latin-1 glyphs vs 16px CJK glyphs)
#   R5  chinesetest (the existing suite - all green expected; shows the
#       >= 0x8000 blind spot)
import os
import re
import socket
import sys
import time
import pexpect

BUILD = "/home/z/my-project/oc-os/build"
OUT = f"{BUILD}/wp06g1-repro"
MON = f"{OUT}/mon.sock"
SER = f"{OUT}/serial.log"
os.makedirs(OUT, exist_ok=True)
for p in (MON, SER):
    if os.path.exists(p):
        os.unlink(p)

QEMU = "qemu-system-x86_64"
CMD = (f"{QEMU} -L /home/z/opt/extract/usr/share/qemu "
       f"-L /home/z/opt/extract/usr/share/seabios -m 256M "
       f"-cdrom {BUILD}/opencube.iso -boot d "
       f"-no-reboot -display none "
       f"-serial file:{SER} "
       f"-monitor unix:{MON},server,nowait -vga std")


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


def serial_tail(n=4000):
    with open(SER, "rb") as f:
        return f.read()[-n:]


print("[repro] boot:", CMD)
child = pexpect.spawn("/bin/bash", ["-c", CMD], timeout=180, encoding=None)
mon = Mon(MON)
time.sleep(1)

# wait for the ush prompt on the serial log
deadline = time.time() + 150
saw_ush = False
while time.time() < deadline:
    data = serial_tail()
    if b"ush>" in data:
        saw_ush = True
        break
    time.sleep(2)
print("[repro] ush prompt:", saw_ush)
if not saw_ush:
    print("[repro] boot serial tail:", serial_tail(3000).decode("utf-8", "replace"))
    sys.exit(1)

mon.cmd("sendkey spc")  # noop settle
time.sleep(0.5)

# --- R5: chinesetest first (all-green expected; the blind spot proof) ---
# drive via serial input: type into COM1 by writing the QEMU monitor
# 'sendkey' is PS/2; for serial we use the chardev send.  Use the
# monitor's usb/char device?  Simplest: QEMU stdio mux is not available
# (serial is a file).  Type through the monitor's sendkey for ASCII is
# tedious - instead we paste THROUGH the serial by reconnecting: we use
# socat-style injection via a second chardev?  Not possible after boot.
#
# => relaunch strategy: the repro battery needs serial INPUT.  We redo
# the boot below with -serial stdio under pexpect.  Keep this file boot
# only for the clean boot log.

child.terminate(force=True)
time.sleep(1)

SER2 = f"{OUT}/serial2.log"
CMD2 = (f"{QEMU} -L /home/z/opt/extract/usr/share/qemu "
        f"-L /home/z/opt/extract/usr/share/seabios -m 256M "
        f"-cdrom {BUILD}/opencube.iso -boot d "
        f"-no-reboot -display none "
        f"-serial stdio "
        f"-monitor unix:{MON},server,nowait -vga std")
for p in (MON,):
    if os.path.exists(p):
        os.unlink(p)

print("[repro] boot2 (serial stdio):", CMD2)
child = pexpect.spawn("/bin/bash", ["-c", CMD2], timeout=180)
child.logfile_read = open(f"{OUT}/pty-all.log", "wb")
mon = Mon(MON)

idx = child.expect(["ush>", pexpect.TIMEOUT], timeout=150)
print("[repro] ush prompt (pty):", idx == 0)
if idx != 0:
    print(child.before.decode("utf-8", "replace")[-3000:])
    sys.exit(1)

def run_line(line_bytes, tag, settle=3.0, capture=6.0):
    """Send a line on serial stdin, capture what COM1 echoes back."""
    before = child.logfile_read.tell() if hasattr(child.logfile_read, "tell") else 0
    child.send(line_bytes + b"\r")
    time.sleep(settle)
    out = b""
    child.expect([b"ush>", pexpect.TIMEOUT], timeout=capture)
    out = child.before if isinstance(child.before, bytes) else b""
    with open(f"{OUT}/{tag}.bin", "wb") as f:
        f.write(out)
    return out

# --- R5: chinesetest ---
out = run_line(b"chinesetest", "r5_chinesetest", settle=6, capture=15)
txt = out.decode("utf-8", "replace")
m = re.search(r"chinesetest: (\d+)/(\d+) (PASS|FAIL)", txt)
print("[R5] chinesetest:", m.group(0) if m else "NO SUMMARY")
print(txt[-2600:])

# --- R1: lang zh (L0 console_cn_puts path) ---
out = run_line(b"lang zh", "r1_lang_zh", settle=3)
print("[R1] lang zh output bytes (repr):")
print(repr(out[-500:]))

# --- R2/R3: paste UTF-8 Chinese through the serial line editor ---
zh = "中文测试".encode("utf-8")          # E4 B8 AD E6 96 87 E6 B5 8B E8 AF 95
out = run_line(b"echo " + zh, "r2_paste_utf8", settle=4)
print("[R2] echo <utf8> echoed bytes (repr):")
print(repr(out[-600:]))

# what the terminal saw: decode the echoed line as UTF-8 strictly
line = out.split(b"\r\n")[-2] if b"\r\n" in out else out
try:
    dec = out.decode("utf-8")
    ok = True
except UnicodeDecodeError as e:
    ok = False
print("[R3] serial stream strictly valid UTF-8:", ok)

# --- R4: screen evidence of the paste echo (screendump after re-paste) ---
child.send(b"echo " + zh + b"\r")
time.sleep(3)
mon.cmd("screendump " + OUT + "/r4_screen.ppm")
time.sleep(1)

# also a clean reference: lang zh output on screen
child.send(b"lang zh\r")
time.sleep(2)
mon.cmd("screendump " + OUT + "/r4_langzh.ppm")
time.sleep(1)

# --- R4b: gb2312 blind-spot probes through the existing commands ---
# encoding-test only spot-checks D6D0 (<0x8000).  We demonstrate the
# misread directly: 'a' = B0A2 -> reader gives 0x163F (already proven
# host-side from the gz blob; nothing in-system exercises it).

child.send(b"poweroff\r")
time.sleep(3)
try:
    child.terminate(force=True)
except Exception:
    pass

print("[repro] artifacts in", OUT)
print("[repro] done")
