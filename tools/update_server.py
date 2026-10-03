# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
#!/usr/bin/env python3
"""Update-manifest test server for OpenCubeOS WP-09-fix5 checkupdate tests.

Serves /update.json responses over plain HTTP or over TLS configured to
match the kernel TLS 1.2 client (TLS 1.2 only, DHE-RSA-AES128-SHA256,
1024-bit DH).  Loops for multiple connections so one server process can
back a whole QEMU test session.

Usage:
  python3.13 update_server.py --mode http  --port 8008 [options]
  python3.13 update_server.py --mode https --port 8443 --cert C --key K --dh D [options]

Options:
  --json '<text>'   exact JSON body to serve (default: the WP-10 manifest)
  --json2 '<text>'  optional body for /update-v2.json (dual-manifest
                    "Plan D" tests: short changes on /update.json, long
                    changes on /update-v2.json).  Without --json2 every
                    path is served the same --json body (legacy behavior).
  --bad             serve a broken (non-JSON) body
  --status N        serve HTTP status N with a plain body
  --pkg <file>      serve this file for every other path (the update
                    package, e.g. build/opencube-wp10c-update.tar.gz).
                    Content-Length is sent exactly; the kernel downloader
                    enforces it against update.json's package_size.
"""
import argparse
import socket
import ssl
import sys

DEFAULT_MANIFEST = (
    '{\n'
    '  "version": "WP-10",\n'
    '  "time": "2026-10-30",\n'
    '  "changes": "driver optimization + config file"\n'
    '}\n'
)

BAD_BODY = "this is not json at all\n"


def build_response(body: bytes, status: int = 200) -> bytes:
    reason = {200: "OK", 404: "Not Found", 500: "Internal Server Error"}.get(
        status, "Status")
    return (
        f"HTTP/1.0 {status} {reason}\r\n"
        f"Content-Type: application/json\r\n"
        f"Content-Length: {len(body)}\r\n"
        f"Connection: close\r\n"
        f"\r\n"
    ).encode() + body


def serve_once(conn, body: bytes, status: int, body2: bytes | None = None,
               pkg_path: str | None = None) -> None:
    req = b""
    while b"\r\n\r\n" not in req and len(req) < 8192:
        try:
            chunk = conn.recv(4096)
        except socket.timeout:
            break
        if not chunk:
            break
        req += chunk
    first = req.split(chr(13).encode())[0].decode(errors="replace")
    print(f"[srv] request: {first}", flush=True)
    out = body
    if body2 is not None and "update-v2.json" in first:
        out = body2
    if pkg_path is not None and ".json" not in first:
        # manifest paths: GET /update.json and GET /update-v2.json
        # (anything ending in .json) get the JSON bodies above; every
        # other path (the package, e.g. GET /pkg.tar.gz) gets the file.
        try:
            with open(pkg_path, "rb") as f:
                out = f.read()
        except OSError as e:
            print(f"[srv] pkg read error: {e}", flush=True)
    conn.sendall(build_response(out, status))
    print(f"[srv] response sent ({status}, {len(out)} bytes)", flush=True)
    if hasattr(conn, "unwrap"):            # TLS: send close_notify
        try:
            conn.unwrap()
        except Exception as e:             # noqa: BLE001
            print(f"[srv] unwrap: {e}", flush=True)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--mode", choices=["http", "https"], required=True)
    ap.add_argument("--port", type=int, required=True)
    ap.add_argument("--json", default=DEFAULT_MANIFEST)
    ap.add_argument("--json2", default=None,
                    help="body served for /update-v2.json (Plan D tests)")
    ap.add_argument("--bad", action="store_true")
    ap.add_argument("--status", type=int, default=200)
    ap.add_argument("--pkg", default=None,
                    help="file served for non-manifest paths (the .tar.gz)")
    ap.add_argument("--cert", default="/tmp/hcert.pem")
    ap.add_argument("--key", default="/tmp/hkey.pem")
    ap.add_argument("--dh", default="/tmp/dhparam.pem")
    ap.add_argument("--count", type=int, default=16,
                    help="number of connections to serve before exiting")
    args = ap.parse_args()

    body = BAD_BODY.encode() if args.bad else args.json.encode()
    body2 = None if args.json2 is None else args.json2.encode()
    if args.pkg is not None and not __import__("os").path.isfile(args.pkg):
        print(f"[srv] error: --pkg file not found: {args.pkg}", flush=True)
        return 1

    lsock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    lsock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    lsock.bind(("0.0.0.0", args.port))
    lsock.listen(4)
    lsock.settimeout(180)
    print(f"[srv] {args.mode} listening on 0.0.0.0:{args.port}", flush=True)

    if args.mode == "https":
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ctx.set_ciphers("DHE-RSA-AES128-SHA256:@SECLEVEL=0")
        ctx.minimum_version = ssl.TLSVersion.TLSv1_2
        ctx.maximum_version = ssl.TLSVersion.TLSv1_2
        ctx.load_cert_chain(args.cert, args.key)
        ctx.load_dh_params(args.dh)

    served = 0
    try:
        while served < args.count:
            raw, addr = lsock.accept()
            print(f"[srv] accepted from {addr}", flush=True)
            try:
                if args.mode == "https":
                    raw.settimeout(120)
                    conn = ctx.wrap_socket(raw, server_side=True)
                    print(f"[srv] TLS handshake OK: {conn.version()} "
                          f"cipher={conn.cipher()}", flush=True)
                else:
                    conn = raw
                conn.settimeout(60)
                serve_once(conn, body, args.status, body2, args.pkg)
            except Exception as e:              # noqa: BLE001
                print(f"[srv] connection error: {e}", flush=True)
            finally:
                try:
                    conn.close()                # type: ignore[name-defined]
                except Exception:               # noqa: BLE001
                    pass
            served += 1
    except socket.timeout:
        print("[srv] listen timeout - exiting", flush=True)
    print("[srv] done", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
