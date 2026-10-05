#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""P0fix2 hostile TLS server for audit cases A14-15..A14-18 (WP-AUDIT-01-p0fix2).

Drives the kernel TLS client (entered via `checkupdate` with
`config set update_url https://10.0.2.2:<port>/x`) into the specific
defects documented in docs/audit/findings/A14.md:

  --case a14-16   BUG-0022 (A14-16): after the client's ClientHello, send a
                  TLS record whose header declares 0x4200 (16896) bytes.  The
                  old check `len > buf_cap + 256` accepted it and read 256
                  bytes past the 16640-byte g_hsrec heap block.  A fixed
                  kernel must reject the record and stay alive.

  --case a14-17   BUG-0023 (A14-17): send a minimal TLS 1.2 ServerHello (no
                  supported_versions -> kernel falls back to the TLS 1.2
                  path), then a record declaring 0x4920 (18720) bytes.  The
                  old check `len > cap + 2080` accepted it and read 18720
                  bytes into 16640-byte static buffers.

  --case a14-18   BUG-0024 (A14-18): send the same TLS 1.2 ServerHello, then
                  a Certificate handshake message whose 3-byte length field
                  says 0x4000 (16384).  The old code copied mlen into the
                  12288-byte static body BEFORE any chain verification.

Each case logs what it sent to stdout so the QEMU-side serial log can be
cross-checked.  A fixed kernel answers with a clean handshake failure; the
hostile server itself never needs to complete a handshake.
"""
import argparse
import socket
import threading
import time


def recv_exact(conn, n):
    buf = b""
    while len(buf) < n:
        chunk = conn.recv(n - len(buf))
        if not chunk:
            return None
        buf += chunk
    return buf


def read_tls_record(conn):
    """Read one TLS record header + body from the kernel client."""
    hdr = recv_exact(conn, 5)
    if hdr is None:
        return None
    ln = (hdr[3] << 8) | hdr[4]
    body = recv_exact(conn, ln) if ln else b""
    return hdr + (body or b"")


def tls12_server_hello():
    """Minimal TLS 1.2 ServerHello: version 3,3 / random / sid=0 /
    cipher 0x0067 / compression 0 / extensions=0 (no supported_versions
    extension so the kernel takes its TLS 1.2 fallback path)."""
    body = bytes([3, 3]) + bytes(32) + bytes([0]) + bytes([0x00, 0x67]) + bytes([0, 0, 0])
    hs = bytes([2]) + len(body).to_bytes(3, "big") + body
    rec = bytes([0x16, 3, 3]) + len(hs).to_bytes(2, "big") + hs
    return rec


def serve_case(case, port):
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("0.0.0.0", port))
    srv.listen(1)
    print(f"[evil-tls] case {case} listening on :{port}", flush=True)
    srv.settimeout(180)
    try:
        conn, _ = srv.accept()
    except socket.timeout:
        print("[evil-tls] no client connected", flush=True)
        return
    conn.settimeout(30)
    try:
        ch = read_tls_record(conn)
        print(f"[evil-tls] got ClientHello {len(ch or b'')} bytes", flush=True)
        if not ch:
            return

        if case == "a14-16":
            # BUG-0022: record length 0x4200 = 16896 (> 16640 cap, <= cap+256)
            payload = bytes(16896)
            rec = bytes([0x16, 3, 3]) + (16896).to_bytes(2, "big") + payload
            conn.sendall(rec)
            print("[evil-tls] sent record len=16896 (0x4200) > g_hsrec 16640", flush=True)
            time.sleep(8)

        elif case == "a14-17":
            # BUG-0023: TLS 1.2 fallback then oversized record 0x4920 = 18720
            conn.sendall(tls12_server_hello())
            print("[evil-tls] sent TLS1.2 ServerHello (48B record)", flush=True)
            time.sleep(1)
            payload = bytes(18720)
            rec = bytes([0x16, 3, 3]) + (18720).to_bytes(2, "big") + payload
            conn.sendall(rec)
            print("[evil-tls] sent record len=18720 (0x4920) > all old 16640 buffers", flush=True)
            time.sleep(8)

        elif case == "a14-18":
            # BUG-0024: TLS 1.2 fallback then Certificate message mlen=0x4000
            conn.sendall(tls12_server_hello())
            print("[evil-tls] sent TLS1.2 ServerHello (48B record)", flush=True)
            time.sleep(1)
            cert_msg = bytes([0x0B]) + (16384).to_bytes(3, "big") + bytes(16384)
            rec = bytes([0x16, 3, 3]) + len(cert_msg).to_bytes(2, "big") + cert_msg
            conn.sendall(rec)
            print("[evil-tls] sent Certificate message mlen=16384 (0x4000) > body 12288", flush=True)
            time.sleep(8)

        # drain whatever the client sends back until it hangs up
        conn.settimeout(10)
        try:
            while True:
                d = conn.recv(4096)
                if not d:
                    break
        except socket.timeout:
            pass
    except (ConnectionResetError, BrokenPipeError, socket.timeout) as e:
        print(f"[evil-tls] connection ended: {e}", flush=True)
    finally:
        conn.close()
        srv.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--case", required=True,
                    choices=["a14-16", "a14-17", "a14-18"])
    ap.add_argument("--port", type=int, default=8443)
    args = ap.parse_args()
    serve_case(args.case, args.port)


if __name__ == "__main__":
    main()
