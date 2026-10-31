#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""3.0.0-WP04-fix1 BEFORE: tui over the serial backend (bug 2 mojibake)."""
import os, sys, time, socket
import pexpect

OC_TOOLS = os.environ.get("OC_TOOLS", "/home/z/opt/extract")
QEMU = f"{OC_TOOLS}/usr/bin/qemu-system-x86_64"
SOCK = "/tmp/fix1_tui2.sock"

def main():
    iso, outlog = sys.argv[1], sys.argv[2]
    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = (f"{OC_TOOLS}/usr/lib/x86_64-linux-gnu:"
                              f"{OC_TOOLS}/lib/x86_64-linux-gnu")
    if os.path.exists(SOCK):
        os.unlink(SOCK)
    args = ["-m", "512M", "-cdrom", iso, "-boot", "d",
            "-L", f"{OC_TOOLS}/usr/share/qemu",
            "-L", f"{OC_TOOLS}/usr/share/seabios",
            "-display", "none", "-monitor", "none", "-no-reboot",
            "-serial", f"unix:{SOCK},server=on,wait=on"]
    log = open(outlog, "w", buffering=1)
    child = pexpect.spawn(QEMU, args, encoding="utf-8",
                          codec_errors="replace", timeout=240, env=env)
    sock = None
    for _ in range(120):
        try:
            sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            sock.connect(SOCK); break
        except OSError:
            time.sleep(0.5)
    sock.settimeout(1.0)
    buf = bytearray()

    def drain(t=1.2):
        end = time.time() + t
        while time.time() < end:
            try:
                d = sock.recv(65536)
                if d: buf.extend(d)
            except socket.timeout:
                pass
        out = bytes(buf); buf.clear(); return out

    def send(b):
        sock.sendall(b); time.sleep(0.2)

    def until_prompt(timeout=120):
        end = time.time() + timeout
        seen = b""
        while time.time() < end:
            d = drain(0.8)
            if d: seen += d
            if seen.endswith(b"ush>"): return seen
        return seen

    try:
        boot = until_prompt(240)
        log.write("=== boot tail ===\n" + repr(boot[-120:]) + "\n")
        send(b"config set tui.backend serial\n")
        out = until_prompt(60)
        log.write("=== config set ===\n" + repr(out) + "\n")
        send(b"tui\n")
        time.sleep(8)
        out = drain(8.0)
        log.write("=== tui serial-backend raw output ===\n")
        log.write(repr(out) + "\n\n")
        # UTF-8 strictness check on the CJK/box run
        try:
            out.decode("utf-8", errors="strict")
            log.write("=== UTF-8 strict decode: OK ===\n")
        except UnicodeDecodeError as e:
            log.write(f"=== UTF-8 strict decode FAILED: {e} ===\n")
        # grep visible garbage: raw 0x80..0xFF singles not part of UTF-8
        # try rendering as cp437-ish for comparison of what a broken term shows
        send(b"\x1b")
        time.sleep(3)
        out2 = drain(4.0)
        log.write("=== after ESC ===\n" + repr(out2) + "\n")
        log.write("=== END ===\n")
    finally:
        try: sock.close()
        except Exception: pass
        child.close(force=True)
        log.close()

if __name__ == "__main__":
    main()
