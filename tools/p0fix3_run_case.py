#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""P0fix3 case driver: boots the ISO, drives one hostile case against the
guest kernel and prints guest serial + host peer logs, with an explicit
ALIVE proof (`ps` after the case) and a machine-readable verdict.

Usage:
  python3.13 tools/p0fix3_run_case.py <iso> <case> [port]

Cases (see tools/p0fix3_hostile_ssh.py / p0fix3_evil_tls.py):
  client-side (guest `ssh` connects to a hostile server):
    a04_kslen_wrap  a05_flen_wrap  a05g_flen_wrap
  sshd-side (hostile client connects to guest `sshd 22 oc oc`):
    a09_elen_wrap  a13_ulen_wrap  a13_slen_wrap  a13_pwlen_wrap  a38_reqlen_wrap
  sshd-side cases need /etc with a pre-seeded host key (fast path; the
  keygen path is covered separately by the NEW-01 first-run test):
    OC_ETC_IMG=<img>   (default: build/etc_test.img; empty = no /etc)
  TLS-side (guest `checkupdate` -> hostile TLS server):
    a18_chain_wrap  a18_chain_multi
"""
import os
import subprocess
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu_runner import run_qemu_commands, strip_ansi  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))

CLIENT_CASES = {"a04_kslen_wrap", "a05_flen_wrap", "a05g_flen_wrap"}
SSHD_CASES = {"a09_elen_wrap", "a13_ulen_wrap", "a13_slen_wrap",
              "a13_pwlen_wrap", "a38_reqlen_wrap"}
TLS_CASES = {"a18_chain_wrap", "a18_chain_multi"}

CRASH_MARKERS = ("*** EXCEPTION", "*** HALTING", "Halting.",
                 "double fault", "kernel PANIC", "General protection")


def run(iso, case, port):
    if os.environ.get("OC_ETC_IMG", "-").strip() and case in SSHD_CASES:
        etc = os.environ.get("OC_ETC_IMG", "build/etc_test.img").strip()
        os.environ["OC_EXTRA_QEMU_ARGS"] = \
            os.environ.get("OC_EXTRA_QEMU_ARGS", "").strip() + \
            f" -drive if=ide,format=raw,file={os.path.abspath(etc)}"
        print(f"[runner] attaching etc image: {etc}")

    holder = {}

    def peer_bg():
        # Only CLIENT-mode peers (paramiko/plain sockets connecting to the
        # guest's hostfwd port) must wait for the guest: QEMU's slirp
        # accepts the host-side connection immediately (before the guest
        # boots), and paramiko's 15s banner timeout would fire before
        # sshd ever listens. Server-mode peers (guest connects OUT to
        # 10.0.2.2) must bind IMMEDIATELY so the port is open when the
        # guest arrives.
        if case in SSHD_CASES:
            time.sleep(28)
        if case in CLIENT_CASES or case in SSHD_CASES:
            cmd = [sys.executable, os.path.join(HERE, "p0fix3_hostile_ssh.py"),
                   "--role", "server" if case in CLIENT_CASES else "client",
                   "--case", case, "--port", str(port)]
        else:
            cmd = [sys.executable, os.path.join(HERE, "p0fix3_evil_tls.py"),
                   "--case", case, "--port", str(port)]
        try:
            p = subprocess.run(cmd, capture_output=True, text=True, timeout=420)
            holder["log"] = (p.stdout or "") + (p.stderr or "")
        except subprocess.TimeoutExpired as e:
            holder["log"] = ((e.stdout or b"").decode() if isinstance(e.stdout, bytes) else (e.stdout or "")) + \
                            "((peer timed out))"

    if case in CLIENT_CASES:
        cmds = ["dhcp", f"ssh 10.0.2.2 {port} oc oc", "ps"]
    elif case in SSHD_CASES:
        cmds = ["sshd 22 oc oc", "ps"]
    else:  # TLS
        cmds = ["dhcp",
                f"config set update_url https://10.0.2.2:{port}/x",
                "checkupdate",
                "ps"]

    th = threading.Thread(target=peer_bg)
    th.start()
    # the peer needs a moment to bind; the guest boot itself takes ~15s
    import io
    import contextlib
    raw = io.StringIO()
    with contextlib.redirect_stdout(raw):
        out = run_qemu_commands(iso, cmds, timeout_per_cmd=300)
    th.join(timeout=430)
    print(raw.getvalue())

    print("\n" + "=" * 70)
    print(f"GUEST SERIAL LOG (case={case})")
    print("=" * 70)
    guest_text = ""
    for tag, text in out:
        if tag == "BOOT":
            continue
        t = strip_ansi(text)
        guest_text += t + "\n"
        print(f"--- [{tag}] ---\n{t}")
    print("\n" + "=" * 70)
    print("HOST PEER LOG")
    print("=" * 70)
    peer_log = holder.get("log", "(no log)")
    print(peer_log)

    # verdict: `ps` prints the "TID  PRIO  STATE ... CPU_TICKS" header.
    # Crash markers are scanned in BOTH the per-command capture and the
    # raw pexpect stream (a kernel #PF can print outside the capture
    # window when the prompt never returns).
    crashed = any(m in guest_text for m in CRASH_MARKERS) or \
              any(m in raw.getvalue() for m in CRASH_MARKERS)
    alive = ("CPU_TICKS" in guest_text) or ("TID  PRIO" in guest_text)
    print("=" * 70)
    print(f"VERDICT: crash_markers={crashed} alive_proof={alive}")
    print("=" * 70)
    return 1 if crashed else 0


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(2)
    iso = sys.argv[1]
    case = sys.argv[2]
    port = int(sys.argv[3]) if len(sys.argv) > 3 else (
        2222 if case in CLIENT_CASES else 2223 if case in SSHD_CASES else 8553)
    sys.exit(run(iso, case, port))


if __name__ == "__main__":
    main()
