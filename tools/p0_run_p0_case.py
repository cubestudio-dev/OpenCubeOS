#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""P0fix1 single-case driver: boots the ISO, runs the guest commands and the
host-side hostile peer for one audit case, and prints the guest serial log.

Usage:
  python3 tools/p0_run_p0_case.py <iso> client <server-case> <port>
      guest: dhcp + ssh 10.0.2.2 <port>  (host evil server, p0_evil_ssh_server.py)
  python3 tools/p0_run_p0_case.py <iso> sshd <client-case> [port]
      guest: sshd 22 oc oc               (host evil client, p0_evil_sshd_client.py)
"""
import os
import subprocess
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu_runner import run_qemu_commands, strip_ansi  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))


def run(mode, case, port, iso):
    host_log = []

    if mode == "client":
        if case.startswith("a07"):
            srv = subprocess.Popen(
                [sys.executable, os.path.join(HERE, "p0_paramiko_evil.py"),
                 "--port", str(port)],
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        else:
            srv = subprocess.Popen(
                [sys.executable, os.path.join(HERE, "p0_evil_ssh_server.py"),
                 "--case", case, "--port", str(port)],
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        cmds = ["dhcp", f"ssh 10.0.2.2 {port} oc oc"]
        out = run_qemu_commands(iso, cmds, timeout_per_cmd=150)
        try:
            host_out, _ = srv.communicate(timeout=30)
        except subprocess.TimeoutExpired:
            srv.kill()
            host_out, _ = srv.communicate()
        host_log = (host_out or "").splitlines()
    else:  # sshd
        holder = {}

        def client_bg():
            time.sleep(16)   # grub(5s) + boot + sshd startup
            p = subprocess.run(
                [sys.executable, os.path.join(HERE, "p0_evil_sshd_client.py"),
                 "--case", case, "--port", str(port)],
                capture_output=True, text=True, timeout=240)
            holder["log"] = (p.stdout or "") + (p.stderr or "")

        th = threading.Thread(target=client_bg)
        th.start()
        out = run_qemu_commands(iso, ["sshd 22 oc oc"], timeout_per_cmd=240)
        th.join(timeout=260)
        host_log = holder.get("log", "").splitlines()

    print("\n" + "=" * 70)
    print(f"GUEST SERIAL LOG (mode={mode} case={case})")
    print("=" * 70)
    interesting = []
    for tag, text in out:
        t = strip_ansi(text)
        if tag == "BOOT":
            continue
        interesting.append(f"--- [{tag}] ---\n{t}")
    print("\n".join(interesting))
    print("\n" + "=" * 70)
    print("HOST PEER LOG")
    print("=" * 70)
    print("\n".join(host_log))


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        sys.exit(1)
    iso = sys.argv[1]
    mode = sys.argv[2]
    case = sys.argv[3]
    port = int(sys.argv[4]) if len(sys.argv) > 4 else (2222 if mode == "client" else 2223)
    run(mode, case, port, iso)


if __name__ == "__main__":
    main()
