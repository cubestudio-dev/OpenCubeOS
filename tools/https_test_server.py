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
  python3.13 https_test_server.py <cert.pem> <key.pem> <dhparam.pem> [port]
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


def main():
    cert, key, dhparam = sys.argv[1], sys.argv[2], sys.argv[3]
    port = int(sys.argv[4]) if len(sys.argv) > 4 else 8443

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
        try:
            ssock.unwrap()  # send close_notify so kernel sees clean EOF
        except Exception as e:
            print(f"[srv] unwrap (close_notify): {e}")
        print("[srv] done")
    return 0


if __name__ == "__main__":
    sys.exit(main())
