#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - 3.0.0-WP-runtime-hardening-and-experience-rebase (P2D3C)
# File: tools/verify_download_links.py
# Purpose: headless-browser verification of the download page's five
#          buttons (spec 4.9 acceptance): no dead '#' hrefs, the two dev
#          buttons point at ABSOLUTE .iso URLs, and every href actually
#          resolves (HTTP 200).
#
#          The page is loaded from a REAL local http.server (closest to
#          production; file:// hides relative-path bugs), the hrefs are
#          read AFTER assets/app.js fillSiteData() ran, then:
#            - http(s) URLs  -> HEAD via curl -sI -L (assert 200)
#            - relative URLs -> existence check under --root
#            - --map HOST=DIR redirects an absolute host's URLs to a
#              local mirror (offline/air-gapped verification, e.g.
#              --map ocos.cubestudio.cn=/home/z/gh-pages)
#
# Usage: tools/verify_download_links.py [--root DIR] [--map HOST=DIR]...
#                                        [--port N] [--timeout SECS]
#   --root   the site root to serve (default: <repo>/website; use
#            website/out or a gh-pages checkout for the built site)
# Exit code: 0 iff every button PASSES (5/5).

import argparse
import functools
import http.server
import os
import socket
import subprocess
import sys
import threading

from urllib.parse import urlsplit

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))

# (button element id, human label, must be an absolute .iso URL)
BUTTONS = [
    ("iso-url",     "stable iso",     False),
    ("src-url",     "stable src",     False),
    ("ota-url",     "stable ota",     False),
    ("dev-oc-url",  "dev opencube",   True),
    ("dev-ocse-url", "dev ocse",      True),
]


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


class Server(http.server.ThreadingHTTPServer):
    allow_reuse_address = True
    daemon_threads = True


def start_server(root, port):
    handler = functools.partial(
        http.server.SimpleHTTPRequestHandler, directory=root)
    srv = Server(("127.0.0.1", port), handler)
    th = threading.Thread(target=srv.serve_forever, daemon=True)
    th.start()
    return srv


def head_status(url, timeout):
    """HEAD via curl (follows redirects); returns the final status code
    as an int, or None when curl could not get a definitive answer.
    Up to three attempts - a flaky egress must not fail the gate (only
    a real dead link may)."""
    for _attempt in range(3):
        try:
            out = subprocess.run(
                ["curl", "-sI", "-L", "-m", str(timeout),
                 "-o", "/dev/null", "-w", "%{http_code}", url],
                capture_output=True, text=True, timeout=timeout + 10)
        except (OSError, subprocess.TimeoutExpired):
            continue
        if out.returncode == 0:
            try:
                return int(out.stdout.strip())
            except ValueError:
                return None
    # every attempt failed at the transport level (no route, timeout,
    # reset): report indeterminate, never a fake status code
    return None


def check_href(href, root, host_map, timeout):
    """Reachability for one href. Returns (ok, note)."""
    if href.startswith("http://") or href.startswith("https://"):
        parts = urlsplit(href)
        if parts.hostname in host_map:
            # mapped to a local mirror: existence check
            local = os.path.join(host_map[parts.hostname],
                                 parts.path.lstrip("/"))
            if os.path.isfile(local):
                return True, "local mirror: %s" % local
            return False, "missing in local mirror: %s" % local
        code = head_status(href, timeout)
        if code == 200:
            return True, "HTTP 200"
        if code is None:
            return False, "HEAD failed (no route / curl error)"
        return False, "HTTP %d (expected 200)" % code
    # relative URL: resolve against the served root
    local = os.path.normpath(os.path.join(root, href.lstrip("/")))
    if os.path.isfile(local):
        return True, "local file: %s" % local
    return False, "missing local file: %s" % local


def main():
    ap = argparse.ArgumentParser(
        description="verify the download page's 5 buttons (spec 4.9)")
    ap.add_argument("--root", default=os.path.join(REPO, "website"),
                    help="site root to serve (default: %(default)s)")
    ap.add_argument("--map", action="append", default=[],
                    metavar="HOST=DIR",
                    help="check HOST's absolute URLs as local files under "
                         "DIR (repeatable; offline verification)")
    ap.add_argument("--port", type=int, default=None)
    ap.add_argument("--timeout", type=int, default=20,
                    help="per-URL curl timeout (default: %(default)s)")
    args = ap.parse_args()

    root = os.path.abspath(args.root)
    if not os.path.isfile(os.path.join(root, "download.html")):
        print("FAIL: no download.html under %s" % root)
        return 2

    host_map = {}
    for m in args.map:
        if "=" not in m:
            print("FAIL: --map expects HOST=DIR, got %r" % m)
            return 2
        host, d = m.split("=", 1)
        host_map[host] = os.path.abspath(d)

    port = args.port or free_port()
    srv = start_server(root, port)
    url = "http://127.0.0.1:%d/download.html" % port

    from playwright.sync_api import sync_playwright
    fails = 0
    try:
        with sync_playwright() as pw:
            browser = pw.chromium.launch()
            page = browser.new_page()
            page.goto(url, wait_until="load", timeout=30000)
            # wait (best-effort) until fillSiteData() has overwritten the
            # static href="#" placeholders - on a BROKEN site (the very
            # defect this tool hunts) the wait times out; do not abort,
            # fall through and report the dead buttons per-line below.
            try:
                page.wait_for_function(
                    """() => {
                        const ids = ["iso-url", "src-url", "ota-url",
                                     "dev-oc-url", "dev-ocse-url"];
                        return ids.every(id => {
                            const el = document.getElementById(id);
                            return el && el.getAttribute("href") !== "#";
                        });
                    }""", timeout=5000)
            except Exception:  # noqa: BLE001 - dead hrefs: report below
                print("note: fillSiteData() left href placeholders - "
                      "reporting per-button state")
            hrefs = {}
            for bid, _label, _dev in BUTTONS:
                hrefs[bid] = page.eval_on_selector(
                    "#" + bid, "el => el.getAttribute('href')")
            browser.close()
    except Exception as e:  # noqa: BLE001 - report and fail cleanly
        print("FAIL: could not load %s (%s)" % (url, e))
        srv.shutdown()
        return 2
    srv.shutdown()

    print("page: %s  (root: %s)" % (url, root))
    for bid, label, dev in BUTTONS:
        href = hrefs.get(bid) or ""
        problems = []
        if not href or href == "#":
            problems.append("dead href (%r)" % href)
        if dev:
            ok_iso = (href.startswith("http://") or
                      href.startswith("https://")) and \
                urlsplit(href).path.lower().endswith(".iso")
            if not ok_iso:
                problems.append("not an absolute .iso URL")
        if not problems:
            ok, note = check_href(href, root, host_map, args.timeout)
            if ok:
                print("PASS %-13s %s  [%s]" % (label + ":", href, note))
            else:
                fails += 1
                print("FAIL %-13s %s  [%s]" % (label + ":", href, note))
        else:
            fails += 1
            print("FAIL %-13s %s  [%s]" % (label + ":", href,
                                           "; ".join(problems)))

    total = len(BUTTONS)
    print("%d/%d PASS" % (total - fails, total))
    return 0 if fails == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
