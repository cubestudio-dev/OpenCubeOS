#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio.qq.com>
"""WP-tui-input-encoding-hardening 4.2: the space-capture probe.

One run = one (target, input-path) cell of the six-path matrix:

    target   ocire | ocos
    path     serial | ps2 | uefi

  serial : BIOS/SeaBIOS boot; the command line is typed over COM1.
  ps2    : BIOS/SeaBIOS boot; the command line is injected as PS/2
           scancodes through the QEMU monitor (sendkey), exactly the
           hardware path the OCIRE kbd_poll()/OCOS drv_input_keyboard
           consume on a real machine.
  uefi   : OVMF boot; the OCBL EFI menu is driven through its own
           16550 serial poll (the ReadKeyStroke/ConIn firmware path
           is exercised by the menu's key contract), the payload then
           boots with boot services exited and the SAME hardware
           input drivers serve the shell - the line is typed over
           COM1 (uefi-serial) or sendkey (uefi-ps2 is covered by the
           ps2 cell; this cell proves the UEFI chain end to end).

Acceptance per cell: a command WITH A SPACE survives - OCIRE
"lang zh" must reach the shell as two words (the argument parser
proves the space: "langzh" is an unknown command), OCOS "echo A B"
must print "A B".

Usage: wp_s2_space_probe.py <iso> <out.log> <ocire|ocos> <serial|ps2|uefi>
"""
import os, socket, subprocess, sys, time

OC_TOOLS = os.environ.get("OC_TOOLS", os.path.expanduser("~/opt/extract"))
QEMU = f"{OC_TOOLS}/usr/bin/qemu-system-x86_64"
OVMF = f"{OC_TOOLS}/usr/share/ovmf/OVMF.fd"

ISO, OUT, TARGET, PATH = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
assert TARGET in ("ocire", "ocos") and PATH in ("serial", "ps2", "uefi")

SSOCK = "/tmp/wp_s2_ser.sock"
MSOCK = "/tmp/wp_s2_mon.sock"
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
if PATH == "uefi":
    args += ["-bios", OVMF]

log = open(OUT, "w", buffering=1)
child = subprocess.Popen([QEMU] + args, stdout=log, stderr=log, env=env)

def connect(sock_path, tries=240):
    for _ in range(tries):
        try:
            s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            s.connect(sock_path)
            return s
        except OSError:
            time.sleep(0.5)
    return None

ser = connect(SSOCK)
mon = connect(MSOCK)
if ser is None:
    log.write("FATAL: no serial socket\n"); child.kill(); sys.exit(2)
if ser: ser.settimeout(1.0)
if mon: mon.settimeout(1.0)

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
        if pat.encode() in buf:
            return True
        drain(0.8)
    return False

def mon_cmd(cmd, wait=0.35):
    mon.sendall((cmd + "\n").encode())
    time.sleep(wait)
    try:
        while True:
            mon.settimeout(0.15)
            mon.recv(65536)
    except socket.timeout:
        pass

def sendkey_line(text):
    """Type a line as PS/2 keys via the monitor (live guest).
    NOTE: QEMU HMP key names are lowercase; an uppercase letter must
    be injected as 'shift-<lc>' (a bare 'X' is an INVALID key name
    and silently injects nothing - probe-verified against OCIRE's
    polling driver)."""
    names = {" ": "spc", "\n": "ret", "\t": "tab"}
    for ch in text:
        if ch in names:
            k = names[ch]
        elif ch.isupper():
            k = "shift-" + ch.lower()
        else:
            k = ch
        mon_cmd(f"sendkey {k}")
        time.sleep(0.12)
        drain(0.25)

results = {}
def check(name, cond):
    results[name] = bool(cond)
    log.write(f"[{'PASS' if cond else 'FAIL'}] {name}\n")

try:
    # ---- boot to the payload prompt ----
    if TARGET == "ocos":
        # default entry boots OCOS -> ush (the countdown does it)
        ok = wait_for("ush>", 300)
    else:
        # navigate: one Down (to "OCIRE recovery") + Enter
        if os.environ.get("WP_S2_OCIRE_ISO"):
            # the standalone OCIRE ISO boots OCIRE as the default entry
            # (no menu navigation: under OVMF the OCBL-EFI menu keying
            # races OVMF's own ConIn terminal driver for the RBR - the
            # serial 'B' gets stolen into ReadKeyStroke and dies in the
            # three-key rule; registered as an extended-audit finding)
            ok = wait_for("ocire>", 300)
        else:
            ok = wait_for("Up/Down to select", 300)
            drain(1.0)
            ser.sendall(b"B")          # OCBL serial keying: 'B' = down
            drain(1.0)
            ser.sendall(b"\r")         # Enter = boot now
            ok = ok and wait_for("ocire>", 300)
    check(f"boot reached the {'ush' if TARGET == 'ocos' else 'ocire'}> prompt", ok)
    drain(2.0)

    # ---- the space probe ----
    if TARGET == "ocire":
        if PATH in ("serial", "uefi"):
            ser.sendall(b"lang zh\n")
            drain(4.0)
            seg = bytes(buf).decode("utf-8", "replace")
            seg = seg[seg.rfind("ocire> lang"):] if "ocire> lang" in seg else seg[-400:]
            check("echo shows the space (lang zh)",
                  "ocire> lang zh" in bytes(buf).decode("utf-8", "replace"))
            check("arg parsed (space survived): language switched",
                  "语言：中文" in seg or "language:" in seg)
        else:  # ps2
            sendkey_line("lang zh\n")
            drain(4.0)
            alltxt = bytes(buf).decode("utf-8", "replace")
            check("echo shows the space (lang zh)", "lang zh" in alltxt)
            check("arg parsed (space survived): language switched",
                  "语言：中文" in alltxt)
            check("no langzh corruption", "langzh" not in alltxt)
    else:
        if PATH in ("serial", "uefi"):
            ser.sendall(b"echo A B\n")
            drain(4.0)
        else:
            sendkey_line("echo A B\n")
            drain(4.0)
        alltxt = bytes(buf).decode("utf-8", "replace")
        check("echo shows the space (echo A B)", "echo A B" in alltxt)
        check("output keeps both words (A B)", "\nA B" in alltxt)
finally:
    try: ser.close()
    except Exception: pass
    try: mon.close()
    except Exception: pass
    child.kill()
    child.wait()
    log.close()

n_pass = sum(1 for v in results.values() if v)
print(f"[{TARGET}/{PATH}] {n_pass}/{len(results)}",
      "PASS" if results and all(results.values()) else "FAIL")
sys.exit(0 if results and all(results.values()) else 1)
