#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""P0fix2 BUG-0021 (A14-15) driver: an HTTP server that 302-redirects the
kernel's OTA check to an https:// URL whose PATH part is ~880 bytes.

Path: guest `checkupdate` -> update_url http://10.0.2.2:<port>/x.json ->
302 Location: https://github.com/<852 'a's> -> kernel follows the redirect
over real TLS (github.com's certificate chain is publicly trusted, so the
handshake succeeds) -> net_tls_https_get builds the request:

    "GET " + path(852) + " HTTP/1.1\r\nHost: " + "github.com" + "\r\nConnection: close\r\n\r\n"

= 4 + 852 + 17 + 10 + 25 + 1 = 909 bytes... plus the long Host header of the
real CDN the request is aimed at; the unfixed code concatenated unchecked
into char req[1024] on a small kernel thread stack and corrupted memory.
The fixed code rejects the oversized request up front (return -1) and the
kernel stays alive with a clean OTA error.
"""
import argparse
import socket
import threading

PATH_LEN = 1300


def handle(conn):
    try:
        conn.settimeout(15)
        req = b""
        while b"\r\n\r\n" not in req and len(req) < 65536:
            d = conn.recv(4096)
            if not d:
                break
            req += d
        long_path = "a" * PATH_LEN
        body = b"redirect\n"
        resp = (
            "HTTP/1.0 302 Found\r\n"
            f"Location: https://github.com/{long_path}\r\n"
            "Content-Length: " + str(len(body)) + "\r\n"
            "Connection: close\r\n"
            "\r\n"
        ).encode() + body
        conn.sendall(resp)
        print(f"[redir] served 302 -> https://github.com/{long_path[:20]}... "
              f"(path {PATH_LEN}B, req total 1357 > 1024)", flush=True)
    except Exception as e:   # noqa: BLE001
        print(f"[redir] conn error: {e}", flush=True)
    finally:
        conn.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8099)
    args = ap.parse_args()
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("0.0.0.0", args.port))
    srv.listen(8)
    print(f"[redir] listening on :{args.port}", flush=True)
    while True:
        conn, _ = srv.accept()
        threading.Thread(target=handle, args=(conn,), daemon=True).start()


if __name__ == "__main__":
    main()
