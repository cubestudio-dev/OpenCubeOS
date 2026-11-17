#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""Retry capturer for entries the main pass could not finish.

Retry classes and their dedicated handling:
  exit      - run `exit`, capture the drop to the L0 recovery shell,
              return via `run ush` (the documented nested-shell path)
  loop      - `run loop` (async), capture the started-ack + the
              spinner line, kill the pid
  stdin     - commands that read standard input with no arguments:
              send Ctrl+D (EOT) to close it
  screen    - full-screen apps that read the KEYBOARD, not the serial
              line: drive the quit key through the QEMU MONITOR
              (`sendkey`) so the app actually receives it

The original JSONL keeps its records; retried names are rewritten in
place (the note records the retry).
"""
import argparse
import json
import os
import re
import socket
import sys
import time

import pexpect

OC = os.environ.get("OC_TOOLS", os.path.expanduser("~/opt/extract"))
QEMU = os.path.join(OC, "usr", "bin", "qemu-system-x86_64")
QDATA = os.path.join(OC, "usr", "share", "qemu")
SEABIOS = os.path.join(OC, "usr", "share", "seabios")
LIB = ":".join([os.path.join(OC, "usr", "lib", "x86_64-linux-gnu"),
                os.path.join(OC, "lib", "x86_64-linux-gnu")])

MON_PATH = "/tmp/oc-dict-retry-mon.sock"

STDIN_READERS = {"sort", "uniq", "wc", "awk", "sed", "head", "tail",
                 "cat", "grep"}
SCREEN_APPS = {"tui", "tui-drv", "tui-hw", "tui-net", "tui-sys",
               "tui-sysinfo", "tui-test", "tui-time", "chinese",
               "chinesetest", "edit", "files", "disk-tool", "net-tool",
               "sound-tool", "sysdiag", "hwinfo", "power", "update"}


def strip_ansi(s):
    s = re.sub(r"\x1b\[[0-9;?]*[a-zA-Z]", "", s)
    s = re.sub(r"\x1b[()][B0]", "", s)
    s = re.sub(r"[\x00-\x08\x0b\x0c\x0e-\x1f]", "", s)
    return s


class Mon:
    """QEMU monitor over a unix socket (human protocol)."""

    def __init__(self, path):
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.s.settimeout(10)
        self.s.connect(path)
        self._drain()

    def _drain(self):
        try:
            self.s.settimeout(0.4)
            while True:
                b = self.s.recv(4096)
                if not b:
                    break
        except socket.timeout:
            pass
        self.s.settimeout(10)

    def sendkey(self, key):
        self.s.sendall(("sendkey %s\n" % key).encode())
        time.sleep(0.25)
        self._drain()

    def key(self, ch):
        """Send one printable character as a keystroke."""
        names = {" ": "spc", "\r": "ret", "\n": "ret",
                 "\x1b": "esc", "\t": "tab"}
        if ch in names:
            self.sendkey(names[ch])
        else:
            self.sendkey(ch)


def quiesce(child, lull=1.6, cap=25):
    buf = []
    deadline = time.time() + cap
    while time.time() < deadline:
        try:
            i = child.expect([".+", pexpect.TIMEOUT], timeout=lull)
            if i == 0:
                buf.append(child.after)
                continue
            break
        except (pexpect.EOF, pexpect.TIMEOUT):
            break
    return "".join(buf)


def probe(child, mark, timeout=8):
    try:
        child.sendline("echo " + mark)
        child.expect(mark, timeout=timeout)
        try:
            child.expect(r"ush>\s", timeout=5)
        except (pexpect.EOF, pexpect.TIMEOUT):
            pass
        return True
    except (pexpect.EOF, pexpect.TIMEOUT):
        return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iso", required=True)
    ap.add_argument("--jsonl", required=True)
    args = ap.parse_args()

    recs = {}
    order = []
    for line in open(args.jsonl):
        r = json.loads(line)
        recs[r["name"]] = r
        order.append(r["name"])

    todo = [n for n in order
            if recs[n].get("note", "").startswith(("wedged", "error"))
            or not recs[n].get("output")]
    print("[retry] %d entries to retry: %s" % (len(todo), todo))

    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = LIB
    if os.path.exists(MON_PATH):
        os.unlink(MON_PATH)
    cmd = ("{q} -m 512 -L {qd} -L {sb} -cdrom {iso} -boot d -net none "
           "-serial stdio -display none -no-reboot "
           "-monitor unix:{mon},server,nowait").format(
               q=QEMU, qd=QDATA, sb=SEABIOS, iso=args.iso, mon=MON_PATH)
    child = pexpect.spawn(cmd, env=env, timeout=180,
                          encoding="latin-1", codec_errors="replace")
    child.expect(r"ush>\s", timeout=180)
    for _ in range(30):
        if os.path.exists(MON_PATH):
            break
        time.sleep(0.5)
    mon = Mon(MON_PATH)

    idx = 0
    for name in todo:
        idx += 1
        r = recs[name]
        out = ""
        note = ""
        try:
            if name == "exit":
                child.sendline("exit")
                out = quiesce(child, lull=1.2, cap=8)
                try:
                    child.expect(r"oc>\s", timeout=10)
                    out += child.before + child.after
                    note = "exit drops to the L0 recovery shell"
                except (pexpect.EOF, pexpect.TIMEOUT):
                    note = ""
                child.sendline("run ush")
                child.expect(r"ush>\s", timeout=60)
            elif name == "loop":
                child.sendline("run loop")
                out = quiesce(child, lull=2, cap=8)
                m = re.search(r"started pid=(\d+)", out)
                if m:
                    child.sendline("kill " + m.group(1))
                    out += quiesce(child, lull=1.5, cap=6)
                note = "run loop + kill (the by-design spinner)"
            elif name in STDIN_READERS:
                child.sendline(name)
                time.sleep(1.0)
                out = quiesce(child, lull=0.6, cap=3)
                child.send("\x04")            # Ctrl+D: close stdin
                out += quiesce(child, lull=1.2, cap=8)
                note = "reads standard input; closed with Ctrl+D"
            elif name in SCREEN_APPS:
                child.sendline(name)
                out = quiesce(child, lull=2.5, cap=12)
                for k in ["q", "esc", "q"]:
                    mon.key(k)
                    out += quiesce(child, lull=1.5, cap=6)
                    if probe(child, "RTP%d" % idx):
                        break
                note = ("full-screen app; quit key driven through the "
                        "QEMU monitor sendkey channel")
            else:
                child.sendline(name)
                out = quiesce(child)
                note = "plain retry"
            ok = probe(child, "RTPF%d" % idx)
            if not ok:
                raise RuntimeError("probe failed after retry")
            r["output"] = strip_ansi(out)
            r["note"] = ("retry OK; " + note).strip("; ")
            print("[retry] %-16s OK (%s)" % (name, note[:40]))
        except Exception as e:
            print("[retry] %-16s STILL FAILING: %s" % (name, e))
            r["note"] = ("retry-failed: %s; original note: %s"
                         % (e, r.get("note", "")))
            # recover the VM for the next entry
            child.terminate(force=True)
            time.sleep(1)
            if os.path.exists(MON_PATH):
                os.unlink(MON_PATH)
            child = pexpect.spawn(cmd, env=env, timeout=180,
                                  encoding="latin-1",
                                  codec_errors="replace")
            child.expect(r"ush>\s", timeout=180)
            for _ in range(30):
                if os.path.exists(MON_PATH):
                    break
                time.sleep(0.5)
            try:
                mon = Mon(MON_PATH)
            except Exception:
                mon = None

    child.terminate(force=True)
    with open(args.jsonl, "w") as f:
        for n in order:
            f.write(json.dumps(recs[n], ensure_ascii=False) + "\n")
    print("[retry] done - %d entries rewritten" % len(todo))
    return 0


if __name__ == "__main__":
    sys.exit(main())
