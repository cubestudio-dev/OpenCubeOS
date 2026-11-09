#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""P0fix3 NEW-01 phase B/C driver (sandbox-friendly split).

The sandbox reaps detached processes after ~2 minutes, so the full
first-run flow cannot outlive one tool call. Split:

  phase B (this script, "gen"):  boot with a CLEAN /etc image attached
      (WRITABLE - no -snapshot), start `sshd`, ^C out of the wait, then
      wait for the background "host key ready" line. When the
      generation completes the kernel PERSISTS the key into
      /etc/ssh_host_key - proving both completion and the one-time
      persistence promise. Takes as long as TCG needs.

  phase C (this script, "use"):  boot with the SEEDED /etc image; the
      second `sshd` invocation must reach "listening" within seconds
      (key loaded, NO regeneration), and a real paramiko login
      (oc/oc) + exec must work with that key.

Phase A (non-blocking notice, ^C cancel, shell responsiveness) is
covered by tools/p0fix3_new01_test.py (checks 1a/2/3/3b).
"""
import os
import sys
import threading
import time

import pexpect

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from qemu_runner import QEMU_BIN, QEMU_DATADIR, SEABIOS_DIR, ENV_LD  # noqa: E402


def spawn_guest(iso, etc_img):
    cmd = [QEMU_BIN, "-m", "512M", "-cdrom", iso, "-no-reboot", "-boot", "d",
           "-L", QEMU_DATADIR, "-L", SEABIOS_DIR, "-vga", "std", "-display", "none",
           "-serial", "mon:stdio", "-monitor", "none",
           "-netdev", "user,id=n1,hostfwd=tcp::2223-:22",
           "-device", "e1000,netdev=n1",
           "-drive", f"if=ide,format=raw,file={etc_img}"]
    child = pexpect.spawn(cmd[0], cmd[1:], timeout=90, encoding="latin-1",
                          codec_errors="replace",
                          env={**os.environ, "LD_LIBRARY_PATH": ENV_LD})
    child.logfile_read = sys.stdout
    return child


def phase_gen(iso, etc_img, budget_s):
    child = spawn_guest(iso, etc_img)
    ok = False
    try:
        child.expect(r"oc>\s*", timeout=90)
        t0 = time.time()
        child.send("sshd 22 oc oc\r")
        child.expect(r"generation runs in background[^\r\n]*", timeout=30)
        child.send("\x03")
        child.expect(r"oc>\s*", timeout=10)
        print(f"\n[new01b] cancelled wait after {time.time()-t0:.1f}s; "
              f"waiting up to {budget_s}s for the background keygen\n", flush=True)
        idx = child.expect([r"host key generated and saved to[^\r\n]*",
                            r"host key generated \(RAM only[^\r\n]*",
                            pexpect.TIMEOUT], timeout=budget_s)
        if idx == 0:
            ok = True
            print("\n[new01b] PASS: background generation completed AND "
                  "persisted to /etc/ssh_host_key", flush=True)
        elif idx == 1:
            print("\n[new01b] generation completed but /etc was not writable "
                  "(persistence not proven)", flush=True)
        else:
            print(f"\n[new01b] TIMEOUT after {budget_s}s - keygen did not "
                  f"finish inside the tool window", flush=True)
    finally:
        child.close(force=True)
    return 0 if ok else 1


def phase_use(iso, etc_img):
    child = spawn_guest(iso, etc_img)
    results = []

    def check(name, cond):
        results.append((name, bool(cond)))
        print(f"\n[new01c] CHECK {name}: {'PASS' if cond else 'FAIL'}\n", flush=True)

    try:
        child.expect(r"oc>\s*", timeout=90)
        t0 = time.time()
        child.send("sshd 22 oc oc\r")
        idx = child.expect([r"listening on port 22[^\r\n]*",
                            r"generating per-installation host key[^\r\n]*",
                            pexpect.TIMEOUT], timeout=45)
        t_listen = time.time() - t0
        check("second boot: sshd listens from the persisted key (no regen)",
              idx == 0 and t_listen < 45)

        holder = {}

        def ssh_client():
            import paramiko
            try:
                t = paramiko.Transport(("127.0.0.1", 2223))
                t.start_client(timeout=60)
                t.auth_password("oc", "oc")
                ch = t.open_session(timeout=30)
                ch.exec_command("echo HELLO_FROM_NEW01C")
                time.sleep(6)
                out = b""
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
        check("paramiko login with the generated+persisted key", idx == 0 and "err" not in holder)
        check("exec output round-trips", b"HELLO_FROM_NEW01C" in holder.get("out", b""))
    finally:
        child.close(force=True)

    nfail = sum(1 for _, ok in results if not ok)
    for name, ok in results:
        print(f"  {'PASS' if ok else 'FAIL'}  {name}")
    return 1 if nfail else 0


def main():
    phase = sys.argv[1]
    iso = sys.argv[2]
    etc_img = os.path.abspath(sys.argv[3])
    budget = int(sys.argv[4]) if len(sys.argv) > 4 else 480
    if phase == "gen":
        return phase_gen(iso, etc_img, budget)
    if phase == "use":
        return phase_use(iso, etc_img)
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())
