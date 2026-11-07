#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""G11b e2e: interactive `ime -i` console compose line.

The compose loop reads the PS/2 keyboard queue (not the serial input),
so the keystrokes must come from the QEMU human monitor (sendkey) -
the same mechanism the WP-10d USB tests use.  Sequence: type "ni",
digit 1 commits candidate #1 (U+4F60 你), Enter ends the line; the
summary must report one composed character.
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

PROMPT = r"ush>\s"


def strip_ansi(s):
    s = re.sub(r"\x1b\[[0-9;]*[a-zA-Z]", "", s)
    s = re.sub(r"\x1b\[\?\d+[a-zA-Z]", "", s)
    return s


def hmp(child, hcmd, settle=1.2):
    child.send("\x01c")
    child.expect(r"\(qemu\)", timeout=15)
    child.sendline(hcmd)
    child.expect(r"\(qemu\)", timeout=15)
    child.send("\x01c")
    time.sleep(settle)


def main():
    iso = sys.argv[1] if len(sys.argv) > 1 else "build/opencube.iso"
    cmd = [QEMU, "-m", "512M", "-cdrom", iso, "-boot", "d", "-no-reboot",
           "-L", f"{OC_TOOLS}/usr/share/qemu",
           "-L", f"{OC_TOOLS}/usr/share/seabios",
           "-vga", "std", "-display", "none", "-serial", "mon:stdio"]
    child = pexpect.spawn(cmd[0], cmd[1:], env=ENV, timeout=120,
                          encoding="utf-8", codec_errors="replace")
    child.logfile_read = sys.stdout
    try:
        child.expect(PROMPT, timeout=90)
        print("\n[ime-i] boot ok; sending ime -i")
        child.sendline("ime -i")
        child.expect(r"IME> ", timeout=60)
        print("\n[ime-i] compose line is up; typing pinyin 'ni'")
        hmp(child, "sendkey n")
        hmp(child, "sendkey i")
        # After "ni" the strip shows candidates; give the redraw a beat,
        # then commit candidate #1 with digit 1 and end with Enter.
        child.expect(r"\[ni\]", timeout=30)
        print("\n[ime-i] pinyin buffer shows [ni]; committing digit 1")
        hmp(child, "sendkey 1")
        hmp(child, "sendkey ret", settle=2.0)
        child.expect(r"ime: composed (\d+) character", timeout=60)
        n = child.match.group(1)
        print(f"\n[ime-i] compose summary: {n} character(s)")
        child.expect(PROMPT, timeout=30)
        ok = (n == "1")
        print("\n[ime-i] " + ("PASS" if ok else "FAIL"))
        return 0 if ok else 1
    except pexpect.TIMEOUT as e:
        print(f"\n[ime-i] TIMEOUT: {e}")
        return 2
    except pexpect.EOF:
        print("\n[ime-i] EOF (guest died?)")
        return 3
    finally:
        try:
            child.kill(signal.SIGKILL)
        except Exception:
            pass
        child.close(force=True)


if __name__ == "__main__":
    sys.exit(main())
