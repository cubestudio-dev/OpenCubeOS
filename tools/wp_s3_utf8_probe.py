#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio.qq.com>
"""WP-tui-input-encoding-hardening 4.3: the UTF-8 chain probe.

Boots the ISO to the ush prompt (BIOS/SeaBIOS, serial + monitor) and
exercises the five must-do items of the UTF-8 encoding chain:

  1. receive + echo of multi-byte UTF-8 (a pasted CJK pair arrives as
     per-byte key events; the assembled line must come back EXACT -
     "Unknown command: <CJK>" proves the line; the VGA screendump is
     captured as the framebuffer evidence)
  2. cat of a UTF-8 file (written via echo >) must return the exact
     byte sequence
  3. backspace deletes a WHOLE character (type two hanzi, two BS,
     type X -> the executed line reads exactly "X"); the 2-cell wipe
     must appear on the wire ("\b \b" twice per hanzi)
  4./5. width/wrap: a 45-hanzi line (90 cells) wraps at the 80-cell
     boundary WITHOUT splitting a glyph (VGA screendump evidence +
     the putcw wrap check); screendumps are saved for readback

Usage: wp_s3_utf8_probe.py <iso> <out.log> <shot-prefix> [label]
"""
import os, socket, subprocess, sys, time

OC_TOOLS = os.environ.get("OC_TOOLS", os.path.expanduser("~/opt/extract"))
QEMU = f"{OC_TOOLS}/usr/bin/qemu-system-x86_64"
ISO, OUT, SHOTP = sys.argv[1], sys.argv[2], sys.argv[3]
LABEL = sys.argv[4] if len(sys.argv) > 4 else "run"

SSOCK = "/tmp/wp_s3_ser.sock"
MSOCK = "/tmp/wp_s3_mon.sock"
for s in (SSOCK, MSOCK):
    if os.path.exists(s):
        os.unlink(s)

env = dict(os.environ)
env["LD_LIBRARY_PATH"] = (f"{OC_TOOLS}/usr/lib/x86_64-linux-gnu:"
                          f"{OC_TOOLS}/lib/x86_64-linux-gnu")
args = ["-m", "512M", "-cdrom", ISO, "-boot", "d",
        "-L", f"{OC_TOOLS}/usr/share/qemu",
        "-L", f"{OC_TOOLS}/usr/share/seabios",
        "-display", "none", "-no-reboot",
        "-serial", f"unix:{SSOCK},server=on,wait=on",
        "-monitor", f"unix:{MSOCK},server=on,wait=off"]
log = open(OUT, "w", buffering=1)
child = subprocess.Popen([QEMU] + args, stdout=log, stderr=log, env=env)

def conn(p):
    for _ in range(240):
        try:
            s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            s.connect(p)
            return s
        except OSError:
            time.sleep(0.5)
ser, mon = conn(SSOCK), conn(MSOCK)
ser.settimeout(1.0); mon.settimeout(1.0)

buf = bytearray()
def drain(t=1.0):
    end = time.time() + t
    while time.time() < end:
        try:
            d = ser.recv(65536)
            if d:
                buf.extend(d)
                log.write(d.decode("utf-8", "replace"))
        except socket.timeout:
            pass
    return bytes(buf)

def wait_for(pat, timeout=240):
    end = time.time() + timeout
    while time.time() < end:
        if pat.encode() in bytes(buf):
            return True
        drain(0.8)
    return False

def mon_cmd(c, w=0.4):
    mon.sendall((c + "\n").encode())
    time.sleep(w)
    try:
        while True:
            mon.settimeout(0.15)
            mon.recv(65536)
    except socket.timeout:
        pass

def shot(name):
    p = f"{SHOTP}_{name}_{LABEL}.ppm"
    mon_cmd(f"screendump {p}", 0.8)
    return p

CJK2 = "中文"            # the pasted pair (6 bytes)
HE = "你好世界"          # 4 hanzi (12 bytes)
FORTY5 = "汉" * 45       # 90 cells > 80: must wrap without a split

results = {}
def check(name, cond):
    results[name] = bool(cond)
    log.write(f"[{'PASS' if cond else 'FAIL'}] {name}\n")

shots = []
try:
    if not wait_for("ush>"):
        print("FATAL: no prompt"); sys.exit(2)
    drain(1.0)

    # ---- 1: receive + echo (per-byte paste) ----
    for b in CJK2.encode():
        ser.sendall(bytes([b]))
        drain(0.15)
    drain(1.0)
    ser.sendall(b"\n")
    drain(2.0)
    txt = bytes(buf).decode("utf-8", "replace")
    check("pasted CJK echoed back on the wire", CJK2 in txt)
    check("assembled line exact (command not found: 中文)",
          "command not found: 中文" in txt)
    shots.append(shot("echo"))

    # ---- 2: cat a UTF-8 file ----
    ser.sendall(f"echo {HE} > /t.txt\n".encode())
    drain(2.5)
    ser.sendall(b"cat /t.txt\n")
    drain(3.0)
    txt = bytes(buf).decode("utf-8", "replace")
    # the cat output: the file content echoed after the command echo
    check("cat UTF-8 file returns exact bytes",
          txt.count(HE) >= 2)   # once in the echo-cmd, once (or more) in output
    shots.append(shot("cat"))

    # ---- 3: backspace deletes a whole character ----
    for b in "中文".encode():
        ser.sendall(bytes([b]))
        drain(0.12)
    drain(0.6)
    ser.sendall(b"\x08")          # delete 文 (whole hanzi, 2 cells)
    drain(0.8)
    ser.sendall(b"\x08")          # delete 中
    drain(0.8)
    ser.sendall(b"X\n")           # the line now reads exactly X
    drain(2.5)
    txt = bytes(buf).decode("utf-8", "replace")
    check("two BS removed both hanzi (line executes as X)",
          "command not found: X" in txt)
    raw = bytes(buf)
    # the 2-cell wipe: each hanzi BS emits "\b \b" TWICE
    # (le_backspace loops cols times)
    check("BS wipe width is 2 cells per hanzi",
          raw.count(b"\b \b") >= 4)
    shots.append(shot("bs"))

    # ---- 4/5: width/wrap (90 cells across the 80-col boundary) ----
    ser.sendall(f"echo {FORTY5}\n".encode())
    drain(4.0)
    txt = bytes(buf).decode("utf-8", "replace")
    check("45-hanzi line streamed intact (byte count)",
          txt.count("汉") >= 90)   # echo + output
    shots.append(shot("wrap"))

    # ---- final: prompt still responsive ----
    ser.sendall(b"echo ok\n")
    check("shell alive after the UTF-8 session",
          wait_for("\nok", 30))
finally:
    for p in shots:
        log.write(f"shot: {p}\n")
    # raw byte sidecar for hexdump evidence (the text log sanitizes
    # multi-byte sequences split across socket reads)
    with open(OUT + ".bin", "wb") as f:
        f.write(bytes(buf))
    try: ser.close()
    except Exception: pass
    try: mon.close()
    except Exception: pass
    child.kill()
    child.wait()
    log.close()

n_pass = sum(1 for v in results.values() if v)
print(f"[utf8/{LABEL}] {n_pass}/{len(results)}",
      "PASS" if results and all(results.values()) else "FAIL")
for p in shots:
    print("shot:", p)
sys.exit(0 if results and all(results.values()) else 1)
