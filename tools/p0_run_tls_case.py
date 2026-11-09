#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""P0fix2 TLS case driver: boots the ISO, points the guest `checkupdate` at a
hostile local TLS server (tools/p0_evil_tls_server.py) and prints the guest
serial log.

Usage:
  python3 tools/p0_run_tls_case.py <iso> <case> [port]
      cases: a14-16 | a14-17 | a14-18   (BUG-0022/0023/0024)
"""
import os
import subprocess
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu_runner import run_qemu_commands  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))


def run(case, port, iso):
    holder = {}

    def server_bg():
        p = subprocess.run(
            [sys.executable, os.path.join(HERE, "p0_evil_tls_server.py"),
             "--case", case, "--port", str(port)],
            capture_output=True, text=True, timeout=300)
        holder["log"] = (p.stdout or "") + (p.stderr or "")

    th = threading.Thread(target=server_bg)
    th.start()
    time.sleep(1.5)   # let the hostile server bind
    cmds = ["dhcp",
            f"config set update_url https://10.0.2.2:{port}/x",
            "checkupdate",
            "echo ALIVE-MARKER"]
    out = run_qemu_commands(iso, cmds, timeout_per_cmd=120)
    th.join(timeout=60)
    print("=== GUEST SERIAL ===")
    print(out)
    print("=== HOSTILE SERVER LOG ===")
    print(holder.get("log", "(no log)"))


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(2)
    iso = sys.argv[1]
    case = sys.argv[2]
    port = int(sys.argv[3]) if len(sys.argv) > 3 else 8443
    run(case, port, iso)


if __name__ == "__main__":
    main()
