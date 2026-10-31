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
            if seen.endswith(b"ush> "): return seen
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

        # ---- E1: Left/Home/End/Del/Backspace via ANSI ----
        w("=== [bug 7/E1] Left + insert + Home + End + Del + BS ===")
        send(b"ab"); send(b"\x1b[D")          # Left: cursor between a,b
        send(b"X")                              # -> aXb
        send(b"\x1b[H")                         # Home
        send(b"Y")                              # -> YaXb
        send(b"\x1b[F")                         # End
        send(b"Z")                              # -> YaXbZ
        send(b"\x1b[3~")                        # Del at EOL: no-op
        send(b"\x1b[D"); send(b"\x1b[3~")      # Left, Del -> YaXb
        send(b"\n")
        out = until_prompt(60)
        raw(out)
        check("E1 cursor keys assemble YaXb",
              b"YaXb" in out)
        # a second identical run proves no ghost/concat from redraw
        send(b"ab"); send(b"\x1b[D"); send(b"X"); send(b"\x1b[H")
        send(b"Y"); send(b"\x1b[F"); send(b"Z"); send(b"\x1b[D")
        send(b"\x1b[3~"); send(b"\n")
        out = until_prompt(60)
        raw(out)
        check("E1 repeat stable (clean redraw, no ghost)",
              b"command not found: YaXb\r\n" in out
              and b"command not found: YaXbZ" not in out)

        # ---- E2: Ctrl+A/E/W/K + Ctrl+C ----
        w("=== [bug 7/E2] Ctrl+A/E/W/K + Ctrl+C ===")
        send(b"one two")
        send(b"\x01")          # Ctrl+A home
        send(b"X")              # Xone two
        send(b"\x05")          # Ctrl+E end
        send(b"Y")              # Xone twoY
        send(b"\x17")          # Ctrl+W kill prev word -> Xone
        send(b"\x0b")          # Ctrl+K kill to end (cursor at EOL: noop)
        send(b"\n")
        out = until_prompt(60)
        raw(out)
        check("E2 Ctrl+A/E/W assemble Xone",
              b"command not found: Xone\r\n" in out
              and b"command not found: Xone two" not in out)
        send(b"junkjunk")
        send(b"\x03")          # Ctrl+C cancel
        out = drain(2.0)
        raw(out)
        check("E2 Ctrl+C cancels line (^C, no command-not-found)",
              b"^C" in out and b"command not found: junkjunk" not in out)
        send(b"\n")
        until_prompt(30)

        # ---- E3: history Up/Down + pad-clear ghost test ----
        w("=== [bug 7/E3] history Up/Down (ghost-free) ===")
        send(b"echo 1111111111 2222222222 3333333333\n")
        out = until_prompt(60); raw(out)
        send(b"echo s\n")
        out = until_prompt(60); raw(out)
        send(b"\x1b[A")          # Up -> echo s
        time.sleep(1.0)
        send(b"\x1b[A")          # Up -> the long entry
        time.sleep(1.0)
        send(b"\x1b[B")          # Down -> back to echo s (pad-clear!)
        time.sleep(1.0)
        out = drain(2.5)
        raw(out)
        check("E3 history ghost-free after Down to short entry",
              out.rstrip().endswith(b"echo s"))
        send(b"\n")
        out = until_prompt(60); raw(out)
        check("E3 recalled short entry runs (echo s -> s)", b"\r\ns\r\n" in out)

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
        send(b"\x15"); send(b"\x03"); drain(1.5)
        check("E4 Tab completes 'hist' -> 'history ' (builtin present)",
              b"history " in out)

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
