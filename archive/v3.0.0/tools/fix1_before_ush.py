#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""3.0.0-WP04-fix1 BEFORE captures: ush behavior family (bugs 1,3,5,6,7,8).

Raw-byte serial channel (unix socket chardev) so control bytes are
delivered to the guest verbatim (no host SIGINT passthrough).
"""
import os, sys, time, socket
import pexpect

OC_TOOLS = os.environ.get("OC_TOOLS", "/home/z/opt/extract")
QEMU = f"{OC_TOOLS}/usr/bin/qemu-system-x86_64"
SOCK = "/tmp/fix1_serial.sock"

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
            "-display", "none", "-monitor", "none",
            "-no-reboot",
            "-serial", f"unix:{SOCK},server=on,wait=on"]
    log = open(outlog, "w", buffering=1)
    child = pexpect.spawn(QEMU, args, encoding="utf-8",
                          codec_errors="replace", timeout=240, env=env)

    # connect to the serial socket
    sock = None
    for _ in range(120):
        try:
            sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            sock.connect(SOCK)
            break
        except OSError:
            time.sleep(0.5)
    if sock is None:
        log.write("FATAL: cannot connect serial socket\n")
        child.close(force=True)
        sys.exit(1)
    sock.settimeout(1.0)
    buf = bytearray()

    def drain(t=1.2):
        end = time.time() + t
        while time.time() < end:
            try:
                d = sock.recv(65536)
                if d:
                    buf.extend(d)
            except socket.timeout:
                pass
        out = bytes(buf); buf.clear()
        return out

    def send(b):
        sock.sendall(b)
        time.sleep(0.15)

    def w(s):
        log.write(s + "\n")

    def raw(b):
        log.write(repr(b) + "\n")

    def until_prompt(timeout=120):
        """Read serial until the last line looks like the ush prompt."""
        end = time.time() + timeout
        seen = b""
        while time.time() < end:
            d = drain(0.8)
            if d:
                seen += d
            tail = seen[-8:]
            if b"ush>" in tail or (len(seen) > 0 and seen.endswith(b"ush>")):
                # let any echo settle
                extra = drain(0.6)
                return seen + extra
        return seen

    try:
        boot = until_prompt(240)
        w("=== [boot] raw serial up to first ush> (bug 8 banner) ===")
        raw(boot)
        w("")

        w("=== [bug3] type 'ec' then ESC[A ESC[D ESC[C ESC[B, backspaces ===")
        send(b"ec")
        send(b"\x1b[A"); send(b"\x1b[D"); send(b"\x1b[C"); send(b"\x1b[B")
        time.sleep(1.2)
        send(b"\x08\x08\x08\x08\x08\x08")   # backspaces to clear the junk
        time.sleep(1.0)
        out = drain(2.0)
        raw(out)
        w("")

        w("=== [bug3] Tab completion: 'dat' + TAB + enter ===")
        send(b"dat"); time.sleep(0.4)
        send(b"\t"); time.sleep(1.2)
        send(b"\n")
        out = until_prompt(60)
        raw(out)
        w("")

        w("=== [bug7] editor keys: 'abc' Home ESC[H 'X' End ESC[F 'Y' Del ESC[3~ Ctrl-U ===")
        send(b"abc"); time.sleep(0.5)
        send(b"\x1b[H"); time.sleep(0.5)   # Home
        send(b"X"); time.sleep(0.5)          # insert before 'a'
        send(b"\x1b[F"); time.sleep(0.5)   # End
        send(b"Y"); time.sleep(0.5)          # append
        send(b"\x1b[3~"); time.sleep(0.8)  # Delete
        send(b"\x15"); time.sleep(0.8)      # Ctrl+U
        out = drain(2.0)
        raw(out)
        w("")

        w("=== [bug7] history: long cmd, short cmd, Up ESC[A, Down ESC[B, enter ===")
        send(b"echo 1111111111 2222222222 3333333333\n")
        out = until_prompt(60); raw(out)
        send(b"echo s\n")
        out = until_prompt(60); raw(out)
        send(b"\x1b[A"); time.sleep(1.0)   # Up -> long entry
        send(b"\x1b[B"); time.sleep(1.0)   # Down -> back to draft
        send(b"\n")
        out = until_prompt(60); raw(out)
        w("")

        w("=== [bug5] help -a ===")
        send(b"help -a\n")
        out = until_prompt(120); raw(out)
        w("")
        w("=== [bug5] help -w ===")
        send(b"help -w\n")
        out = until_prompt(120); raw(out)
        w("")
        w("=== [bug6] help (head only recorded) ===")
        send(b"help\n")
        out = until_prompt(120)
        raw(out[:2500]); w("...(truncated)")
        w("")
        w("=== [bug11] pipes work: echo hello | wc ===")
        send(b"echo hello | wc\n")
        out = until_prompt(60); raw(out)
        w("")
        w("=== [bug1] exit -> observe 12s window ===")
        send(b"exit\n")
        time.sleep(12)
        out = drain(4.0)
        if out:
            raw(out)
            w("[exit: serial still has output]")
        else:
            w("[exit: NO output within 12s - blank state confirmed]")
        # poke input
        send(b"\n")
        time.sleep(5)
        out = drain(3.0)
        if out:
            raw(out)
            w("[input after exit got a response]")
        else:
            w("[input after exit: silence - machine parked blank]")
        w("")
        w("=== END before-capture ===")
    finally:
        try: sock.close()
        except Exception: pass
        child.close(force=True)
        log.close()

if __name__ == "__main__":
    main()
