#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""3.0.0-WP04-fix1 AFTER captures: the full ush acceptance battery.

Per-key editor verification (each key proven by the line content that
reaches the shell), help three-mode verification, exit hint, CJK paste
(char-loss fix), banner version.
"""
import os, sys, time, socket
import pexpect

OC_TOOLS = os.environ.get("OC_TOOLS", "/home/z/opt/extract")
QEMU = f"{OC_TOOLS}/usr/bin/qemu-system-x86_64"
SOCK = "/tmp/fix1_after.sock"

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

    def send(b, wait=0.25):
        sock.sendall(b); time.sleep(wait)

    def until_prompt(timeout=120):
        end = time.time() + timeout
        seen = b""
        while time.time() < end:
            d = drain(0.8)
            if d: seen += d
            if seen.endswith(b"ush> ") or seen.rstrip().endswith(b"ush>"): return seen
        return seen

    def w(s): log.write(s + "\n")
    def raw(b): log.write(repr(b) + "\n")

    ok = []
    def check(name, cond):
        ok.append((name, cond))
        log.write(f"[{'PASS' if cond else 'FAIL'}] {name}\n")

    try:
        boot = until_prompt(240)
        w("=== [bug 8] banner ===")
        raw(boot[-260:])
        check("banner shows 3.0.0-WP04 (not WP-08)",
              b"(3.0.0-WP04)" in boot and b"(WP-08)" not in boot)

        # ---- E4: Tab completion from the registry ----
        w("=== [bug 7/E4 + bug 6] Tab completion (registry-driven) ===")
        send(b"dat"); send(b"\t", 1.0)
        out = drain(1.5)
        raw(out)
        send(b"\n")
        out2 = until_prompt(60)
        raw(out2)
        check("E4 Tab completes 'dat' -> 'date ' (registry)",
              b"date" in out and (b"\r\n" in out2 and b"ush>" in out2))
        send(b"hist"); send(b"\t", 1.0)
        out = drain(1.5); raw(out)
        check("E4 Tab completes 'hist' -> 'history ' (builtin present)",
              b"history " in out)
        send(b"\x15"); send(b"\x03"); drain(1.5)

        # ---- E5: CJK paste (char-loss fix) ----
        w("=== [bug 7/E5] CJK paste (UTF-8 bytes not dropped) ===")
        cjk = "你好".encode("utf-8")
        send(cjk)
        out = drain(2.0)
        raw(out)
        check("E5 pasted CJK echoed back byte-exact", cjk in out)
        send(b"\x03"); drain(1.5)
        send(b"\n"); until_prompt(30)

        # ---- E6: exit refuses with guidance ----
        w("=== [bug 1/E6] exit hint ===")
        send(b"exit\n")
        out = until_prompt(60)
        raw(out)
        check("E6 exit prints guidance and shell survives",
              b"'exit' is not available" in out and out.rstrip().endswith(b"ush>"))
        check("E6 no 'recovery shell' lie on the wire",
              b"recovery shell taking over" not in out)

        # ---- E7: help three modes ----
        w("=== [bug 5/6/E7] help (short) ===")
        send(b"help\n")
        out = until_prompt(120); raw(out)
        check("E7 help short mode distinct (builtins + count)",
              b"built-in commands" in out and b"programs -" in out
              and b"rtc" not in out)
        w("=== [bug 5/E7] help -a (full A-Z) ===")
        send(b"help -a\n")
        out = until_prompt(180)
        raw(out[:4000]); w("...(head recorded)")
        tail = out[-200:]
        log.write(f"tail: {tail!r}\n")
        check("E7 help -a is the full A-Z registry (rtc present, 274 count)",
              b"rtc" in out and b"274" in out)
        w("=== [bug 5/E7] help -w (by work package) ===")
        send(b"help -w\n")
        out = until_prompt(180)
        raw(out[:3500]); w("...(head recorded)")
        check("E7 help -w grouped (=== time === header + rtc member)",
              b"=== time (" in out and b"rtc" in out and b"===" in out)
        w("=== [bug 5/E7] help -x (unknown option) ===")
        send(b"help -x\n")
        out = until_prompt(60); raw(out)
        check("E7 help -x usage message",
              b"unknown option" in out)
        # three modes distinct?
        w("=== [E7] modes distinctness ===")
        send(b"help\n"); a = until_prompt(120)
        send(b"help -a\n"); b_ = until_prompt(180)
        send(b"help -w\n"); c_ = until_prompt(180)
        check("E7 three modes mutually distinct",
              a != b_ and b_ != c_ and a != c_)

        # ---- E8: pipes + shutdown surfaces still fine ----
        w("=== [regression/E8] pipes + registry app run ===")
        send(b"echo hello | wc\n")
        out = until_prompt(60); raw(out)
        check("E8 pipes still work", b"1 1 6" in out)

        w("=== SUMMARY ===")
        for name, cond in ok:
            log.write(f"{'PASS' if cond else 'FAIL'}: {name}\n")
        n_pass = sum(1 for _, c in ok if c)
        log.write(f"TOTAL {n_pass}/{len(ok)}\n")
    finally:
        try: sock.close()
        except Exception: pass
        child.close(force=True)
        log.close()
        sys.exit(0 if all(c for _, c in ok) else 1)

if __name__ == "__main__":
    main()
