#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""WP-09 SSH mainstreaming test matrix (5 cases, all real end-to-end runs).

[1] server: paramiko -> kernel sshd, password, curve25519-sha256 + aes128-ctr
[2] server: paramiko -> kernel sshd, publickey (kernel identity key),
    rsa-sha2-512 signature (OpenSSH default preference)
[3] client: kernel ssh -> paramiko sshd, password, curve25519 + ctr +
    kernel verifies the server host key signature over H (TOFU fingerprint)
[4] client: kernel ssh -> paramiko sshd, publickey ("-" password flag),
    kernel signs with its identity key; paramiko verifies the signature
[5] client: kernel ssh -> paramiko sshd forced legacy (group14 + aes128-cbc),
    proving the fallback path still works after mainstreaming

Usage: python3.13 tools/ssh_mainstream_test.py
"""
import os
import re
import sys
import time
import signal
import threading
import socket
import subprocess

import pexpect
import paramiko

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu_runner import strip_ansi  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ISO = os.path.join(ROOT, "build", "opencube.iso")

_OC_TOOLS = os.environ.get("OC_TOOLS", "/home/z/opt/extract")
QEMU = _OC_TOOLS + "/usr/bin/qemu-system-x86_64"
LIB_PATHS = [_OC_TOOLS + "/usr/lib/x86_64-linux-gnu", _OC_TOOLS + "/usr/lib"]
ENV_LD = ":".join(LIB_PATHS)

# ---------------------------------------------------------------- kernel key
def read_kernel_rsa():
    """Read the kernel identity key (n, d) from kernel/sshd_rsa_key.h."""
    hdr = open(os.path.join(ROOT, "kernel", "sshd_rsa_key.h")).read()
    def grab(name):
        m = re.search(name + r"\[256\] = \{(.*?)\};", hdr, re.S)
        hexdata = re.sub(r"0x|[^0-9a-fA-F]", "", m.group(1))
        return int.from_bytes(bytes.fromhex(hexdata)[:256], "big")
    return grab("sshd_rsa_n"), grab("sshd_rsa_d")


class LogSink:
    """file-like sink for pexpect.logfile_read."""
    def __init__(self):
        self.lines = []

    def write(self, s):
        self.lines.append(strip_ansi(s))

    def flush(self):
        pass


# DigestInfo prefixes for PKCS#1 v1.5 (RFC 8017 §9.2 note 1)
DI_SHA256 = bytes.fromhex("3031300d060960864801650304020105000420")
DI_SHA512 = bytes.fromhex("3051300d060960864801650304020305000440")


class KernelIdentityKey(paramiko.PKey):
    """The kernel's own RSA-2048 identity key as a paramiko client key.

    paramiko 5 keeps get_name() as the key *type* ("ssh-rsa") and picks the
    signature algorithm separately (rsa-sha2-256/512 per preferred_pubkeys).
    """

    def __init__(self, n, d):
        super().__init__()
        self.public_blob = None
        self._n, self._d = n, d
        self._e = 65537

    def get_name(self):
        return "ssh-rsa"

    def get_bits(self):
        return 2048

    def asbytes(self):
        m = paramiko.Message()
        m.add_string("ssh-rsa")
        m.add_mpint(self._e)
        m.add_mpint(self._n)
        return bytes(m)

    def get_fingerprint(self):
        import hashlib
        return hashlib.sha256(bytes(self)).digest()

    def sign_ssh_data(self, data, algorithm=None):
        import hashlib
        if algorithm == "rsa-sha2-512":
            di, h = DI_SHA512, hashlib.sha512(data).digest()
            alg = algorithm
        elif algorithm == "rsa-sha2-256":
            di, h = DI_SHA256, hashlib.sha256(data).digest()
            alg = algorithm
        else:
            di, h = DI_SHA256, hashlib.sha256(data).digest()
            alg = "rsa-sha2-256"
        print(f"[sign] algo={alg} data_len={len(data)} "
              f"digest_head={h[:8].hex()} data_head={data[:40].hex()}",
              flush=True)
        k = 256
        em = b"\x00\x01" + b"\xff" * (k - len(di) - len(h) - 3) + b"\x00" + di + h
        sig_int = pow(int.from_bytes(em, "big"), self._d, self._n)
        sig = sig_int.to_bytes(k, "big")
        m = paramiko.Message()
        m.add_string(alg)
        m.add_string(sig)
        return m

    def verify_ssh_sig(self, data, msg):
        return False  # client-side only


def qemu_boot_shell(log_read, extra_args=None):
    """Boot the ISO and wait for the shell prompt. Returns the pexpect child."""
    cmd = [QEMU, "-m", "512", "-cdrom", ISO, "-boot", "d", "-no-reboot",
           "-L", _OC_TOOLS + "/usr/share/qemu",
           "-L", _OC_TOOLS + "/usr/share/seabios",
           "-vga", "std", "-display", "none", "-serial", "mon:stdio",
           "-netdev", "user,id=n1,hostfwd=tcp::2223-:22",
           "-device", "e1000,netdev=n1"]
    if extra_args:
        cmd = [QEMU, "-m", "512", "-cdrom", ISO, "-boot", "d", "-no-reboot",
               "-L", _OC_TOOLS + "/usr/share/qemu",
               "-L", _OC_TOOLS + "/usr/share/seabios",
               "-vga", "std", "-display", "none", "-serial", "mon:stdio",
               "-netdev", "user,id=n1",
               "-device", "e1000,netdev=n1"]
    child = pexpect.spawn(cmd[0], cmd[1:],
                          env={**os.environ, "LD_LIBRARY_PATH": ENV_LD},
                          timeout=90, encoding="utf-8", codec_errors="replace")
    child.logfile_read = log_read
    return child


def wait_free_port(port):
    """Wait until the test port is free (previous QEMU gone)."""
    for _ in range(30):
        s = socket.socket()
        try:
            s.bind(("127.0.0.1", port))
            s.close()
            return
        except OSError:
            s.close()
            time.sleep(1)


# ---------------------------------------------------------------- server test
def run_server_test(case, use_pubkey):
    """Boot kernel sshd; connect with paramiko (password or publickey)."""
    sink = LogSink()

    client_result = {}

    def client_thread():
        time.sleep(3)
        for attempt in range(10):
            try:
                t = paramiko.Transport(("127.0.0.1", 2223))
                t.connect()
                so = t.get_security_options()
                so.key_types = (["rsa-sha2-512"] if use_pubkey
                                else ["rsa-sha2-256"])
                if use_pubkey:
                    n, d = read_kernel_rsa()
                    t.auth_publickey("oc", KernelIdentityKey(n, d))
                else:
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
                client_result["output"] = data.decode(errors="replace").strip()
                print(f"\n[paramiko-client] case {case}: AUTH OK, exec: "
                      f"{client_result['output']!r}", flush=True)
                return
            except Exception as e:  # noqa
                print(f"\n[paramiko-client] case {case} attempt {attempt+1} "
                      f"failed: {e}", flush=True)
                time.sleep(2)
        client_result["output"] = None

    client_thread.result = None
    th = threading.Thread(target=client_thread)
    child = qemu_boot_shell(sink)
    try:
        child.expect(r"oc>\s*", timeout=90)
        auth = "-" if use_pubkey else "oc"
        print(f"\n[test {case}] boot complete -> sshd 22 oc {auth}", flush=True)
        child.sendline(f"sshd 22 oc {auth}")
        th.start()
        child.expect(r"oc>\s*", timeout=240)
        th.join(timeout=120)
    finally:
        try:
            child.kill(signal.SIGKILL)
        except Exception:
            pass
        child.close(force=True)

    log = "".join(sink.lines)
    ok = client_result.get("output") == "hello-from-host-paramiko-client"
    print(f"\n[test {case}] kernel log tail:\n{log[-1500:]}", flush=True)
    return ok, log


# ---------------------------------------------------------------- client test
def run_client_test(case, ssh_args, legacy=False, port=2222):
    """Start paramiko_sshd on the host; connect from the kernel via 10.0.2.2."""
    sink = LogSink()

    wait_free_port(port)
    argv = [sys.executable, os.path.join(ROOT, "tools", "paramiko_sshd.py"),
            str(port)] + (["legacy"] if legacy else [])
    srv = subprocess.Popen(argv, stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, text=True)
    srv_lines = []
    threading.Thread(target=lambda: [srv_lines.append(line)
                                     for line in srv.stdout],
                     daemon=True).start()
    # wait for the server to listen
    ok_listen = False
    for _ in range(40):
        if any("listening on" in l for l in srv_lines):
            ok_listen = True
            break
        time.sleep(0.25)
    if not ok_listen:
        srv.kill()
        print(f"[test {case}] paramiko_sshd failed to start", flush=True)
        return False, ""

    child = qemu_boot_shell(sink)
    try:
        child.expect(r"oc>\s*", timeout=90)
        print(f"\n[test {case}] boot complete -> ssh {' '.join(ssh_args)}",
              flush=True)
        child.sendline("ssh " + ssh_args)
        child.expect(r"oc>\s*", timeout=240)
    finally:
        try:
            child.kill(signal.SIGKILL)
        except Exception:
            pass
        child.close(force=True)
        srv.send_signal(signal.SIGKILL)
        srv.wait(timeout=10)

    log = "".join(sink.lines)
    srvlog = "\n".join(srv_lines)
    ok = ("[ssh] authenticated (USERAUTH_SUCCESS)" in log and
          "hello-from-OpenCubeOS-kernel-ssh" in log and
          "exec: no output" not in log)
    print(f"\n[test {case}] kernel log tail:\n{log[-1600:]}", flush=True)
    print(f"\n[test {case}] paramiko server log:\n{srvlog[-800:]}", flush=True)
    return ok, log + "\n" + srvlog


def main():
    only = sys.argv[1] if len(sys.argv) > 1 else None
    results = []
    print("=" * 70)
    print("WP-09 SSH MAINSTREAMING TEST MATRIX")
    print("=" * 70)

    cases = {
        "1": ("[1] server: paramiko -> kernel sshd, password, curve25519+ctr",
              lambda: run_server_test(1, use_pubkey=False),
              "1 server password curve25519+ctr"),
        "2": ("[2] server: paramiko -> kernel sshd, publickey (rsa-sha2-512)",
              lambda: run_server_test(2, use_pubkey=True),
              "2 server publickey rsa-sha2-512"),
        "3": ("[3] client: kernel ssh -> paramiko, password, curve25519+ctr",
              lambda: run_client_test(3, "10.0.2.2 2222 oc oc"),
              "3 client password curve25519+ctr"),
        "4": ("[4] client: kernel ssh -> paramiko, publickey (-)",
              lambda: run_client_test(4, "10.0.2.2 2222 oc -"),
              "4 client publickey"),
        "5": ("[5] client: kernel ssh -> paramiko legacy (group14+cbc)",
              lambda: run_client_test(5, "10.0.2.2 2222 oc oc", legacy=True),
              "5 client legacy fallback group14+cbc"),
    }
    for key in (sorted(cases) if not only else [only]):
        desc, fn, label = cases[key]
        print(f"\n>>> {desc}")
        ok, _ = fn()
        results.append((label, ok))

    print("\n" + "=" * 70)
    print("RESULT SUMMARY")
    print("=" * 70)
    all_ok = True
    for name, ok in results:
        print(f"  [{'PASS' if ok else 'FAIL'}] {name}")
        all_ok = all_ok and ok
    print("=" * 70)
    print("ALL PASS" if all_ok else "SOME FAILED")
    sys.exit(0 if all_ok else 1)


if __name__ == "__main__":
    main()
