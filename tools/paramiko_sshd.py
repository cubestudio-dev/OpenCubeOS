# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
#!/usr/bin/env python3
"""paramiko-based SSH server for testing the Open Cube OS kernel ssh client.

Used as external evidence in the WP-09 regression:
  * Logs the negotiated DH shared secret K (hex, first 8 bytes) on the
    SERVER side -- lets us compare byte-for-byte with the kernel's own
    dhtest/K computation without trusting either side's printouts.
  * Accepts password auth (default oc/oc).
  * Executes "exec" channel requests by running /bin/sh -c <cmd> and
    returning stdout, so the kernel's ssh_exec() round trip is verifiable.

Usage:
  python3 tools/paramiko_sshd.py [port] [hostkey_path]

Kernel side (inside QEMU user-mode net, 10.0.2.2 = host loopback):
  ssh 10.0.2.2 2222 oc oc
"""
import os
import sys
import socket
import threading
import binascii
import logging
import paramiko
import paramiko.util
paramiko.util.log_to_file("/tmp/paramiko_debug.log", level="DEBUG")

HOST_KEY_PATH = "/tmp/oc_test_hostkey"

# paramiko >= 4 wipes transport.K right after NEWKEYS (memory hygiene), so we
# intercept the K at the moment the kex engine computes it.
CAPTURED_K = {"bytes": None}
_orig_set_K_H = paramiko.Transport._set_K_H


def _capture_set_K_H(self, k, h):
    try:
        kb = int(k).to_bytes((int(k).bit_length() + 7) // 8, "big")
        CAPTURED_K["bytes"] = kb
    except Exception:
        pass
    return _orig_set_K_H(self, k, h)


paramiko.Transport._set_K_H = _capture_set_K_H




def make_host_key():
    if os.path.exists(HOST_KEY_PATH):
        return paramiko.RSAKey(filename=HOST_KEY_PATH)
    key = paramiko.RSAKey.generate(2048)
    key.write_private_key_file(HOST_KEY_PATH)
    return key


class ServerInterface(paramiko.ServerInterface):
    def __init__(self, log):
        self.log = log

    def get_allowed_auths(self, username):
        return "password,publickey"

    def check_auth_publickey(self, username, key):
        # Accept the kernel's identity key: parse its public half (sshd_rsa_n)
        # straight out of kernel/sshd_rsa_key.h so the test server trusts the
        # exact key the kernel signs with.
        ok = False
        try:
            import re as _re
            hexdata = ""
            with open(os.path.join(os.path.dirname(__file__), "..",
                                   "kernel", "sshd_rsa_key.h")) as f:
                hdr = f.read()
            m = _re.search(r"sshd_rsa_n\[256\] = \{(.*?)\};", hdr, _re.S)
            hexdata = _re.sub(r"0x|[^0-9a-fA-F]", "", m.group(1))
            n = int.from_bytes(bytes.fromhex(hexdata)[:256], "big")
            pn = key.public_numbers
            ok = (key.get_name() == "ssh-rsa" and pn.n == n and pn.e == 65537)
        except Exception as exc:
            ok = False
            self.log(f"publickey check error: {exc}")
        self.log(f"AUTH publickey user={username!r} accepted={ok}")
        if ok and username == "oc":
            return paramiko.AUTH_SUCCESSFUL
        return paramiko.AUTH_FAILED

    def hostkey_n_bytes(self):
        import re as _re, os as _os
        hdr = open(_os.path.join(_os.path.dirname(__file__), "..",
                                 "kernel", "sshd_rsa_key.h")).read()
        mm = _re.search(r"sshd_rsa_n\[256\] = \{(.*?)\};", hdr, _re.S)
        hd = _re.sub(r"0x|[^0-9a-fA-F]", "", mm.group(1))
        return bytes.fromhex(hd)[:256]

    def check_auth_password(self, username, password):
        self.log(f"AUTH password user={username!r} pass={'ok' if password == 'oc' else 'WRONG'}")
        if username == "oc" and password == "oc":
            return paramiko.AUTH_SUCCESSFUL
        return paramiko.AUTH_FAILED

    def check_channel_request(self, kind, chanid):
        self.log(f"CHANNEL request kind={kind}")
        return paramiko.OPEN_SUCCEEDED

    def check_channel_exec_request(self, channel, command):
        self.log(f"EXEC request: {command!r}")
        try:
            import subprocess
            p = subprocess.run(["/bin/sh", "-c", command],
                               capture_output=True, timeout=10)
            out = p.stdout + p.stderr
        except Exception as e:  # noqa
            out = f"server-error: {e}".encode()
        channel.sendall(out)
        channel.shutdown_write()
        return True


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 2222
    legacy = len(sys.argv) > 2 and sys.argv[2] == "legacy"
    host = "127.0.0.1"

    events = []

    def log(msg):
        line = f"[paramiko-sshd] {msg}"
        print(line, flush=True)
        events.append(line)

    hostkey = make_host_key()
    log(f"host key fp: {hostkey.get_fingerprint().hex()}")

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((host, port))
    sock.listen(2)
    log(f"listening on {host}:{port} legacy={legacy}")

    def handle(client):
        CAPTURED_K["bytes"] = None
        client.settimeout(120)
        transport = paramiko.Transport(client)
        transport.add_server_key(hostkey)
        # Mainstream algorithms first (curve25519-sha256, aes128-ctr);
        # group14/aes128-cbc stay as fallbacks for the legacy client path.
        kex_prefs = ["curve25519-sha256@libssh.org",
                     "diffie-hellman-group14-sha256"]
        cipher_prefs = ["aes128-ctr", "aes128-cbc"]
        if legacy:
            # Fallback-path test: force the oldest algorithms the kernel still
            # offers, so the client must negotiate group14 + CBC.
            kex_prefs = ["diffie-hellman-group14-sha256"]
            cipher_prefs = ["aes128-cbc"]
        so = transport.get_security_options()
        so.kex = list(kex_prefs)
        so.ciphers = list(cipher_prefs)
        if hasattr(so, "macs"):
            so.macs = ["hmac-sha2-256"]
        else:
            so.digests = ["hmac-sha2-256"]
        # NOTE: paramiko >= 5 dropped "ssh-rsa" (SHA-1 sigs); rsa-sha2-256 works
        # because the kernel client proposes it first. The signature algorithm
        # is negotiated separately (paramiko picks it up automatically).
        so.key_types = ["rsa-sha2-256"]
        transport.local_version = "SSH-2.0-paramiko_oc_test"
        server = ServerInterface(log)
        try:
            transport.start_server(server=server)
        except paramiko.SSHException as e:
            log(f"KEX FAILED: {e}")
            transport.close()
            return

        kb = CAPTURED_K["bytes"]
        if kb is not None:
            log(f"K (server) len={len(kb)} K[:8]={binascii.hexlify(kb[:8]).decode()}")
        else:
            log("K (server): not captured")

        chan = transport.accept(60)
        if chan is None:
            log("no channel opened (client closed after KEX/auth)")
            log("SESSION-DONE")
            transport.close()
            return
        try:
            chan.settimeout(60)
            data = chan.recv(4096)
            log(f"channel data (direct-mode probe): {data[:100]!r}")
            chan.sendall(b"hello-from-paramiko-server\n")
            chan.shutdown_write()
        except Exception as e:  # noqa
            log(f"channel error: {e}")
        # wait for exec-driven traffic via check_channel_exec_request
        import time
        t0 = time.time()
        while time.time() - t0 < 60 and not chan.closed:
            time.sleep(0.2)
        log("SESSION-DONE")
        transport.close()

    while True:
        client, addr = sock.accept()
        log(f"connection from {addr}")
        threading.Thread(target=handle, args=(client,), daemon=True).start()


if __name__ == "__main__":
    main()
