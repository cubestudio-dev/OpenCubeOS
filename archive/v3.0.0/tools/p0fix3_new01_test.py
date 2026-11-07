#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""P0fix3 NEW-01 first-run test: boots the ISO with NO /etc key (fresh
installation state) and drives `sshd` through the one-time host-key
generation:

  1. `sshd 22 oc oc` must NOT block the shell: the generation starts in
     a background thread and the console says so within seconds.
  2. Ctrl+C (0x03 over serial) must return to the shell prompt almost
     immediately while the generation continues in background.
  3. The shell must be usable right after the cancel (`ps` runs and
     shows the sshd-hkgen thread).
  4. When the background thread finishes ("host key ready"), a second
     `sshd 22 oc oc` must reach "listening" and accept a real paramiko
     login (oc/oc) - proving the generated key actually works.

The OLD (pre-P0fix3) kernel fails step 1/2: generation runs in the
sshd command's own thread, ^C is ignored and the shell is dead for the
whole multi-minute keygen (verification round: >300s, no cancel).
"""
import os
import re
import sys
import time

import pexpect

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from qemu_runner import QEMU_BIN, QEMU_DATADIR, SEABIOS_DIR, ENV_LD  # noqa: E402


def main():
    iso = sys.argv[1] if len(sys.argv) > 1 else "build/opencube.iso"
    etc = os.environ.get("OC_ETC_IMG", "").strip()   # must stay EMPTY for the fresh test
    if etc:
        print("[new01] OC_ETC_IMG must NOT be set for this test")
        return 2

    cmd = [QEMU_BIN, "-m", "512M", "-cdrom", iso, "-no-reboot", "-boot", "d",
           "-L", QEMU_DATADIR, "-L", SEABIOS_DIR, "-vga", "std", "-display", "none",
           "-serial", "mon:stdio", "-monitor", "none",
           "-netdev", "user,id=n1,hostfwd=tcp::2223-:22",
           "-device", "e1000,netdev=n1"]
    child = pexpect.spawn(cmd[0], cmd[1:], timeout=600, encoding="latin-1",
                          codec_errors="replace",
                          env={**os.environ, "LD_LIBRARY_PATH": ENV_LD})
    child.logfile_read = sys.stdout

    results = []
    def check(name, cond):
        results.append((name, bool(cond)))
        print(f"\n[new01] CHECK {name}: {'PASS' if cond else 'FAIL'}\n", flush=True)

    try:
        child.expect(r"oc>\s*", timeout=90)              # boot to shell

        t0 = time.time()
        child.send("sshd 22 oc oc\r")
        # 1. non-blocking start: background-generation notice within 30s
        idx = child.expect([r"generation runs in background[^\r\n]*",
                            r"host key generated[^\r\n]*",
                            pexpect.TIMEOUT], timeout=30)
        t_notice = time.time() - t0
        check("1a bg-generation notice (or instant ready) within 30s",
              idx != 2 and t_notice < 30)

        # 2. Ctrl+C cancels the wait within 10s
        t0 = time.time()
        child.send("\x03")
        idx = child.expect([r"oc>\s*", r"cancelled[^\r\n]*",
                            pexpect.TIMEOUT], timeout=10)
        t_cancel = time.time() - t0
        check("2 ^C returns to shell within 10s", idx != 2 and t_cancel < 10)

        # 3. shell usable: ps shows the sshd-hkgen thread
        child.send("ps\r")
        idx = child.expect([r"CPU_TICKS", pexpect.TIMEOUT], timeout=15)
        if idx == 0:
            idx = child.expect([r"sshd-hkgen", pexpect.TIMEOUT], timeout=15)
        check("3 shell alive; ps shows sshd-hkgen thread", idx == 0)

        # 3b. another command works too
        child.send("uname -a\r")
        idx = child.expect([r"Open Cube OS[^\r\n]*", pexpect.TIMEOUT], timeout=15)
        check("3b uname works while generation runs", idx == 0)

        # 4. wait for the background completion (TCG: minutes)
        idx = child.expect([r"host key ready[^\r\n]*", pexpect.TIMEOUT], timeout=570)
        check("4 background generation completes (host key ready)", idx == 0)

        # 5. second sshd invocation listens and serves a real paramiko login
        child.send("sshd 22 oc oc\r")
        idx = child.expect([r"listening on port 22[^\r\n]*", pexpect.TIMEOUT], timeout=30)
        check("5a second sshd reaches listening (no regen)", idx == 0)

        import threading
        holder = {}

        def ssh_client():
            import paramiko
            try:
                t = paramiko.Transport(("127.0.0.1", 2223))
                t.start_client(timeout=60)
                t.auth_password("oc", "oc")
                ch = t.open_session(timeout=30)
                ch.exec_command("echo HELLO_FROM_NEW01")
                out = b""
                import time as _t
                _t.sleep(6)
                if ch.recv_ready():
                    out = ch.recv(4096)
                holder["out"] = out
                t.close()
            except Exception as e:  # noqa
                holder["err"] = repr(e)

        th = threading.Thread(target=ssh_client)
        th.start()
        idx = child.expect([r"USERAUTH_SUCCESS sent", pexpect.TIMEOUT], timeout=90)
        th.join(timeout=120)
        check("5b paramiko login with the generated key (USERAUTH_SUCCESS)",
              idx == 0 and "err" not in holder)
        check("5c exec output captured", b"HELLO_FROM_NEW01" in holder.get("out", b""))
    finally:
        try:
            child.close(force=True)
        except Exception:  # noqa
            pass

    print("\n===== NEW-01 SUMMARY =====")
    for name, ok in results:
        print(f"  {'PASS' if ok else 'FAIL'}  {name}")
    nfail = sum(1 for _, ok in results if not ok)
    print(f"===== {len(results) - nfail}/{len(results)} PASS =====")
    return 1 if nfail else 0


if __name__ == "__main__":
    sys.exit(main())
