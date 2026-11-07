#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""G11c e2e: the full-screen TUI pages, driven over serial + VNC.

For each of the eight interactive pages: launch the app from ush, let
the framebuffer render, take an HMP screendump (PNG proof the page drew),
leave with Esc (sendkey), and assert the serial stream got the ush
prompt back.  tui-drv is the tui-hw alias - it gets one shot of its own.
"""
import os
import re
import signal
import sys
import time

import pexpect

OC_TOOLS = os.environ.get("OC_TOOLS", "/home/z/opt/extract")
QEMU = f"{OC_TOOLS}/usr/bin/qemu-system-x86_64"
ENV = os.environ.copy()
ENV["LD_LIBRARY_PATH"] = ":".join([
    f"{OC_TOOLS}/usr/lib/x86_64-linux-gnu", f"{OC_TOOLS}/usr/lib"])
OUT = os.environ.get("G11C_SHOTS", "/tmp/g11c_shots")
PROMPT = r"ush>\s"

# page command -> shot basename (order = the G11c op table)
PAGES = [
    ("tui", "tui_desktop"),
    ("tui-hw", "tui_hw"),
    ("tui-net", "tui_net"),
    ("tui-sysinfo", "tui_sysinfo"),
    ("tui-time", "tui_time"),
    ("tui-drv", "tui_drv"),
    ("tui-sys", "tui_sys"),
    ("tui-ext", "tui_ext"),
]


def strip_ansi(s):
    s = re.sub(r"\x1b\[[0-9;]*[a-zA-Z]", "", s)
    s = re.sub(r"\x1b\[\?\d+[a-zA-Z]", "", s)
    return s


def main():
    iso = sys.argv[1] if len(sys.argv) > 1 else "build/opencube.iso"
    os.makedirs(OUT, exist_ok=True)
    qmon = "/tmp/g11c_qmon"
    try:
        os.unlink(qmon)
    except FileNotFoundError:
        pass
    cmd = [QEMU, "-m", "512M", "-cdrom", iso, "-boot", "d", "-no-reboot",
           "-L", f"{OC_TOOLS}/usr/share/qemu",
           "-L", f"{OC_TOOLS}/usr/share/seabios",
           "-vga", "std", "-display", "none",
           "-vnc", ":1",
           "-serial", "stdio",
           "-monitor", f"unix:{qmon},server,nowait"]
    child = pexpect.spawn(cmd[0], cmd[1:], env=ENV, timeout=180,
                          encoding="utf-8", codec_errors="replace")
    child.logfile_read = sys.stdout

    # monitor helper
    def mcmd(c, settle=0.4):
        import socket as _s
        sk = _s.socket(_s.AF_UNIX)
        sk.connect(qmon)
        sk.settimeout(3)
        time.sleep(settle)
        try:
            sk.recv(65536)
        except Exception:
            pass
        sk.sendall((c + "\n").encode())
        time.sleep(settle)
        out = b""
        try:
            while True:
                d = sk.recv(65536)
                if not d:
                    break
                out += d
                if b"(qemu)" in out:
                    break
        except Exception:
            pass
        sk.close()
        return out.decode("utf-8", "replace")

    results = []
    try:
        child.expect(PROMPT, timeout=120)
        print("\n[g11c] boot ok")
        for page, shot in PAGES:
            print(f"\n[g11c] launching {page}")
            child.sendline(page)
            time.sleep(6)          # let the TUI session paint (TCG)
            mcmd(f"screendump {OUT}/{shot}.png", settle=1.5)
            print(f"[g11c] shot: {OUT}/{shot}.png")
            mcmd("sendkey esc", settle=1.0)
            # some pages take a second Esc / confirm; give the page a beat
            try:
                child.expect(PROMPT, timeout=45)
                ok = True
            except pexpect.TIMEOUT:
                mcmd("sendkey esc", settle=1.0)
                try:
                    child.expect(PROMPT, timeout=20)
                    ok = True
                except pexpect.TIMEOUT:
                    ok = False
            size = os.path.getsize(f"{OUT}/{shot}.png") if os.path.exists(
                f"{OUT}/{shot}.png") else 0
            results.append((page, ok, size))
            print(f"[g11c] {page}: exit={'ok' if ok else 'TIMEOUT'}, "
                  f"png={size}B")
        print("\n[g11c] SUMMARY")
        allok = True
        for page, ok, size in results:
            drawn = size > 4000          # blank screens compress tiny
            line = f"  {page:12s} exit={'PASS' if ok else 'FAIL'}" \
                   f" render={'PASS' if drawn else 'FAIL'} ({size}B)"
            print(line)
            if not (ok and drawn):
                allok = False
        print("[g11c] " + ("ALL PASS" if allok else "FAILURES PRESENT"))
        return 0 if allok else 1
    except pexpect.TIMEOUT as e:
        print(f"\n[g11c] TIMEOUT: {e}")
        return 2
    except pexpect.EOF:
        print("\n[g11c] EOF (guest died?)")
        return 3
    finally:
        try:
            child.kill(signal.SIGKILL)
        except Exception:
            pass
        child.close(force=True)


if __name__ == "__main__":
    sys.exit(main())
