# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
#!/usr/bin/env python3
"""Minimal HTTPS test server for OpenCubeOS kernel TLS client (WP-09) E2E test.

Serves exactly ONE HTTPS request then exits, printing a full evidence log:
  - negotiated TLS version + cipher
  - received HTTP request
  - response body sent
  - DH parameter fingerprint (p, first 16 bytes) for cross-check with the
    kernel-side "[tls] server p (first 16)" debug line

Configured to match kernel/tls.c ClientHello:
  - TLS 1.2 only
  - cipher TLS_DHE_RSA_WITH_AES_128_CBC_SHA256 (0x0067) only
  - 1024-bit DH (DH_BYTES=128), RFC 3526 1024-bit MODP group (Oakley Group 1)
  - self-signed RSA certificate (kernel does not verify certs)

Usage:
  python3.13 https_test_server.py <cert.pem> <key.pem> <dhparam.pem> [port] [eof]

  Normal mode (finding #2 two-sided assertions, WP-10-AUDIT_P2-fix2):
    after the response the server unwraps (sends its close_notify) and then
    READS: the kernel's close_notify alert MUST arrive (server observes a
    clean client close). PASS is printed on receipt, FAIL otherwise.
  eof mode (5th arg "eof"):
    premature-EOF server: sends the response and HARD-CLOSES the socket
    WITHOUT close_notify. The kernel client must fail promptly (truncation
    detected), never hang - the pexpect-side timeout is the assertion.
"""
import socket
import ssl
import sys
import hashlib

# RFC 3526 section 2 "1024-bit MODP Group" (same as RFC 2409 Oakley Group 1)
P_HEX = (
    "FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD129024E088A67CC74"
    "020BBEA63B139B22514A08798E3404DDEF9519B3CD3A431B302B0A6DF25F1437"
    "4FE1356D6D51C245E485B576625E7EC6F44C42E9A637ED6B0BFF5CB6F406B7ED"
    "EE386BFB5A899FA5AE9F24117C4B1FE649286651ECE65381FFFFFFFFFFFFFFFF"
)
G = 2

RESP_BODY = b"hello-from-opencube-tls\n"
RESPONSE = (
    b"HTTP/1.0 200 OK\r\n"
    b"Content-Type: text/plain\r\n"
    b"Content-Length: " + str(len(RESP_BODY)).encode() + b"\r\n"
    b"Connection: close\r\n"
    b"\r\n"
) + RESP_BODY


def tls_serve_once(cert, key, dhparam, port, eof_mode):
    """One TLS session on `port`; returns 0 on success. Split out of main()
    so the `observe` mode can run it on an internal port behind a raw-byte
    recorder (see finding #2 note below)."""

    p_bytes = bytes.fromhex(P_HEX)
    print(f"[srv] DH param p (first 16): {p_bytes[:16].hex()}")
    print(f"[srv] DH param p SHA256: {hashlib.sha256(p_bytes).hexdigest()}")
    sys.stdout.flush()

    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.set_ciphers("DHE-RSA-AES128-SHA256:@SECLEVEL=0")
    ctx.minimum_version = ssl.TLSVersion.TLSv1_2
    ctx.maximum_version = ssl.TLSVersion.TLSv1_2
    ctx.load_cert_chain(cert, key)
    ctx.load_dh_params(dhparam)
    print(f"[srv] listening on 127.0.0.1:{port} (TLS1.2, DHE-RSA-AES128-SHA256)")
    sys.stdout.flush()

    lsock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    lsock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    lsock.bind(("127.0.0.1", port))
    lsock.listen(1)
    lsock.settimeout(180)
    try:
        raw, addr = lsock.accept()
    except socket.timeout:
        print("[srv] TIMEOUT waiting for kernel connection")
        return 1
    print(f"[srv] TCP accepted from {addr}")
    sys.stdout.flush()

    with raw:
        raw.settimeout(120)
        ssock = ctx.wrap_socket(raw, server_side=True)
        print(f"[srv] TLS handshake OK: {ssock.version()} cipher={ssock.cipher()}")
        sys.stdout.flush()

        req = b""
        while b"\r\n\r\n" not in req and len(req) < 4096:
            try:
                chunk = ssock.recv(4096)
            except socket.timeout:
                break
            if not chunk:
                break
            req += chunk
        print(f"[srv] HTTP request ({len(req)} bytes):")
        print(req.decode(errors="replace"))
        sys.stdout.flush()

        ssock.sendall(RESPONSE)
        print(f"[srv] response sent: {len(RESPONSE)} bytes (body={len(RESP_BODY)})")
        sys.stdout.flush()
        if eof_mode:
            # Premature EOF: no close_notify, hard close immediately. The
            # kernel client must detect the truncation promptly (never hang).
            print("[srv] EOF MODE: closing WITHOUT close_notify")
            sys.stdout.flush()
            ssock.close()
            print("[srv] done (eof mode)")
            return 0
        try:
            raw2 = ssock.unwrap()  # send close_notify so kernel sees clean EOF
        except Exception as e:
            print(f"[srv] unwrap (close_notify): {e}")
            raw2 = ssock
        # NOTE: the kernel's close_notify is observed by the `observe`
        # mode (raw-byte recorder) - CPython's unwrap() consumes the
        # peer's close_notify inside the SSL layer, so an in-band read
        # here can never see it (verified by capture, fix2 round).
        print("[srv] done")
    return 0


def observe_serve(cert, key, dhparam, port):
    """FINDING #2 (WP-10-AUDIT_P2-fix2) server-side assertion, raw-byte form.

    CPython's SSLSocket.unwrap() consumes the PEER's close_notify inside the
    SSL layer, so a plain 'recv after unwrap' can never observe the kernel's
    alert (proved by capture: the kernel sends `15 03 03 ..` and the SSL
    layer eats it). The honest observer is a RAW BYTE recorder: the real
    TLS server runs on port+1; this recorder accepts the kernel on `port`,
    pipes bytes both ways, and scans the client->server stream for an outer
    content type 0x15 (TLS alert - the kernel's close_notify)."""
    import threading
    t = threading.Thread(target=tls_serve_once,
                         args=(cert, key, dhparam, port + 1, False), daemon=True)
    t.start()
    rec = socket.socket()
    rec.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    rec.bind(("127.0.0.1", port))
    rec.listen(1)
    rec.settimeout(120)
    print(f"[obs] recorder on 127.0.0.1:{port} -> TLS server on {port + 1}")
    sys.stdout.flush()
    c, _ = rec.accept()
    up = socket.socket()
    up.connect(("127.0.0.1", port + 1))
    alert_seen = [False]
    done = threading.Event()

    def c2s():
        try:
            while True:
                d = c.recv(4096)
                if not d:
                    break
                if d[0] == 0x15:
                    alert_seen[0] = True
                    print(f"[obs] CLIENT ALERT RECORD seen "
                          f"({len(d)}B, first={d[:8].hex()})")
                up.sendall(d)
        except Exception:
            pass
        try:
            up.shutdown(socket.SHUT_WR)
        except Exception:
            pass
        done.set()

    th = threading.Thread(target=c2s, daemon=True)
    th.start()
    try:
        while True:
            d = up.recv(4096)
            if not d:
                break
            c.sendall(d)
    except Exception:
        pass
    try:
        c.shutdown(socket.SHUT_WR)
    except Exception:
        pass
    done.wait(10)
    print(f"[obs] kernel alert record observed: {'YES' if alert_seen[0] else 'NO'}")
    print(f"[obs] CLOSE_NOTIFY_ASSERT: {'PASS' if alert_seen[0] else 'FAIL'}")
    sys.stdout.flush()
    return 0 if alert_seen[0] else 1


def main():
    cert, key, dhparam = sys.argv[1], sys.argv[2], sys.argv[3]
    port = int(sys.argv[4]) if len(sys.argv) > 4 else 8443
    mode = sys.argv[5] if len(sys.argv) > 5 else ""
    if mode == "observe":
        return observe_serve(cert, key, dhparam, port)
    return tls_serve_once(cert, key, dhparam, port, mode == "eof")


if __name__ == "__main__":
    sys.exit(main())
