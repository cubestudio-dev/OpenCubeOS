#!/usr/bin/env python3
"""End-to-end test: Open Cube OS kernel sshd (guest) vs paramiko client (host).

Orchestration:
  1. Boot the kernel ISO in QEMU with hostfwd tcp::2223-:22.
  2. Send `sshd 22 oc oc` at the kernel shell (kernel listens, one session).
  3. From the host, connect paramiko to 127.0.0.1:2223, authenticate with
     password oc/oc, open a session channel and run exec `echo ...`.
  4. Collect kernel-side log and report PASS/FAIL.

Usage: python3 tools/sshd_test.py [iso]
"""
import os
import sys
import re
import time
import signal
import threading
import pexpect
import paramiko

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu_runner import strip_ansi  # noqa: E402

ISO = sys.argv[1] if len(sys.argv) > 1 else "build/opencube.iso"

QEMU = "/home/z/opt/extract/usr/bin/qemu-system-x86_64"
LIB_PATHS = ["/home/z/opt/extract/usr/lib/x86_64-linux-gnu", "/home/z/opt/extract/usr/lib"]
ENV_LD = ":".join(LIB_PATHS)

cmd = [QEMU, "-m", "512", "-cdrom", ISO, "-boot", "d", "-no-reboot",
       "-L", "/home/z/opt/extract/usr/share/qemu",
       "-L", "/home/z/opt/extract/usr/share/seabios",
       "-vga", "std", "-display", "none", "-serial", "mon:stdio",
       "-netdev", "user,id=n1,hostfwd=tcp::2223-:22",
       "-device", "e1000,netdev=n1"]

kernel_output = []
child = pexpect.spawn(cmd[0], cmd[1:], env={**os.environ, "LD_LIBRARY_PATH": ENV_LD},
                      timeout=60, encoding="utf-8", codec_errors="replace")
child.logfile_read = sys.stdout


def client_thread():
    # wait for kernel sshd to be ready
    time.sleep(3)
    for attempt in range(10):
        try:
            t = paramiko.Transport(("127.0.0.1", 2223))
            t.connect()
            t.auth_password("oc", "oc")
            ch = t.open_session(timeout=30)
            ch.exec_command("echo hello-from-host-paramiko-client")
            data = b""
            deadline = time.time() + 30
            while time.time() < deadline:
                if ch.recv_ready():
                    data += ch.recv(4096)
                if ch.exit_status_ready() and not ch.recv_ready():
                    break
                time.sleep(0.05)
            ch.close()
            t.close()
            print(f"\n[paramiko-client] AUTH OK, exec output: {data!r}", flush=True)
            client_thread.result = data.decode(errors="replace").strip()
            return
        except Exception as e:  # noqa
            print(f"\n[paramiko-client] attempt {attempt + 1} failed: {e}", flush=True)
            time.sleep(2)
    client_thread.result = None


client_thread.result = None

try:
    child.expect(r"oc>\s*", timeout=60)
    kernel_output.append(strip_ansi(child.before))
    print("\n[test] boot complete, starting sshd in kernel")
    child.sendline("sshd 22 oc oc")
    th = threading.Thread(target=client_thread)
    th.start()
    # sshd returns to the prompt after serving one session (or timing out)
    child.expect(r"oc>\s*", timeout=180)
    kernel_output.append(strip_ansi(child.before))
    th.join(timeout=120)
finally:
    try:
        child.kill(signal.SIGKILL)
    except Exception:
        pass
    child.close(force=True)

log = "\n".join(kernel_output)
print("\n" + "=" * 60)
print("KERNEL sshd LOG")
print("=" * 60)
print(log)

ok = True
print("\n" + "=" * 60)
checks = [
    ("kernel: listening", "listening on port 22" in log),
    ("kernel: connection accepted", "connection from" in log),
    ("kernel: session finished cleanly", "session finished cleanly" in log),
    ("client: auth + exec succeeded", getattr(client_thread, "result", None) == "hello-from-host-paramiko-client"),
]
for name, passed in checks:
    print(f"  [{'PASS' if passed else 'FAIL'}] {name}")
    ok = ok and passed
print("=" * 60)
sys.exit(0 if ok else 1)
