#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""BUG-0132 regression test: Ctrl+C must cancel a blocking sshd accept.

Audit finding: "sshd 启动后阻塞监听，Ctrl+C 无法中断，shell 完全失去响应
（^C 字节未被消费），只能重启虚拟机".

Fixed behavior:
  - sshd prints the listening banner, the keyboard queue keeps being
    drained while net_accept() polls
  - sending ^C makes sshd log "accept cancelled (Ctrl+C)" and return to
    the shell prompt within a few seconds (pre-fix: the ^C byte sat in
    the keyboard queue and the shell stayed frozen for the full 120 s
    accept timeout)
  - after the cancel, the shell executes a normal command again

Pass criteria:
  1. "listening on port" banner appears
  2. after ^C, "accept cancelled (Ctrl+C)" appears within CANCEL_BUDGET s
  3. the oc> prompt returns within CANCEL_BUDGET s of the banner
  4. a follow-up command runs and echoes its marker
"""
import os
import re
import sys
import time
import signal

import pexpect

OC_TOOLS = os.environ.get("OC_TOOLS", "/home/z/opt/extract")
QEMU_BIN = f"{OC_TOOLS}/usr/bin/qemu-system-x86_64"
SEABIOS_DIR = f"{OC_TOOLS}/usr/share/seabios"
QEMU_DATADIR = f"{OC_TOOLS}/usr/share/qemu"
LD_PATHS = f"{OC_TOOLS}/usr/lib/x86_64-linux-gnu:{OC_TOOLS}/usr/lib"

ISO = sys.argv[1] if len(sys.argv) > 1 else "build/opencube.iso"

CANCEL_BUDGET = 15      # seconds from ^C to prompt (pre-fix: 120 s)
MARKER = "BUG0132_SHELL_ALIVE"


def main() -> int:
    # Persisted /etc volume (FAT32 on ata0): the per-installation host key
    # is generated ONCE and stored at /etc/ssh_host_key, so later runs of
    # this test load it instantly instead of re-running the minutes-long
    # RSA-2048 keygen under TCG (which makes the test budget unpredictable).
    etc_img = os.path.join(os.path.dirname(os.path.dirname(
        os.path.abspath(__file__))), "build", "etc.img")
    cmd = [
        QEMU_BIN, "-m", "512M", "-cdrom", ISO, "-boot", "d",
        "-L", QEMU_DATADIR, "-L", SEABIOS_DIR,
        "-vga", "std", "-display", "none",
        "-serial", "mon:stdio",
        "-netdev", "user,id=n1",
        "-device", "e1000,netdev=n1",
        "-no-reboot",
    ]
    if os.path.exists(etc_img):
        cmd += ["-drive", f"if=ide,format=raw,file={etc_img}"]
        print(f"[test] attaching persistent /etc volume: {etc_img}")
    env = os.environ.copy()
    env["LD_LIBRARY_PATH"] = LD_PATHS

    print(f"[test] booting {ISO}")
    child = pexpect.spawn(cmd[0], cmd[1:], env=env, timeout=60,
                          encoding="utf-8", codec_errors="replace")
    child.logfile_read = sys.stdout
    ok = False
    try:
        child.expect(r"oc>\s*", timeout=45)
        print("[test] boot complete, starting sshd")

        child.sendline("sshd 2222")
        # first boot generates a per-installation RSA-2048 host key:
        # minutes under TCG (same 420 s budget as tools/sshd_test.py)
        child.expect(r"listening on port 2222", timeout=420)
        print("[test] sshd listening; sending Ctrl+C")

        time.sleep(2.0)          # make sure the accept loop is polling
        t0 = time.time()
        child.send("\x03")       # Ctrl+C -> ETX into the keyboard queue

        child.expect(r"accept cancelled \(Ctrl\+C\)", timeout=CANCEL_BUDGET)
        t_banner = time.time() - t0
        child.expect(r"oc>\s*", timeout=CANCEL_BUDGET)
        t_prompt = time.time() - t0
        print(f"[test] cancel banner after {t_banner:.1f}s, "
              f"prompt after {t_prompt:.1f}s")

        child.sendline(f"echo {MARKER}")
        child.expect(MARKER, timeout=20)
        child.expect(r"oc>\s*", timeout=20)
        print(f"[test] follow-up command echoed {MARKER}; shell fully alive")
        ok = True
    except pexpect.TIMEOUT as e:
        print(f"[test] FAIL: TIMEOUT ({e})")
    except pexpect.EOF:
        print("[test] FAIL: EOF (guest died)")
    finally:
        try:
            child.kill(signal.SIGKILL)
        except Exception:
            pass
        child.close(force=True)

    print(f"[test] RESULT: {'PASS' if ok else 'FAIL'} (BUG-0132)")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
