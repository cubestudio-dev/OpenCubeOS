#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio.qq.com>
"""3.0.0-WP04-fix1 OCIRE VGA-text scroll capture (bug 10).

Boots the WP-02 dual-boot fixture DISK variant whose GRUB menu runs in
TEXT mode (no gfx payload - the fixture is a declared WP-02 development
tool; this variant only drops the gfxterm surface so the handed-off
kernel sees no framebuffer tag). OCIRE then falls back to the WP-01
VGA text console with REAL VGA device memory at 0xB8000, the exact
backend vga_scroll() maintains. 30 marker lines force ~60 logical rows
(several scrolls); the monitor pmemsave dumps the 4000-byte cell array
a real 80x25 text screen would show, and the assertions compare that
array against the expected scrolled tail.

Usage: fix1_ocire_vga_scroll.py <out.log> <before|after>
"""
import os, sys, time, socket, re
import pexpect

OC_TOOLS = os.environ.get("OC_TOOLS", "/home/z/opt/extract")
QEMU = f"{OC_TOOLS}/usr/bin/qemu-system-x86_64"
SSOCK = "/tmp/fix1_vga_ser.sock"
MSOCK = "/tmp/fix1_vga_mon.sock"

def send_monitor(mon_sock, cmd, wait=0.6):
    mon_sock.sendall((cmd + "\n").encode())
    time.sleep(wait)
    mon_sock.settimeout(0.15)
    out = b""
    end = time.time() + 1.2
    while time.time() < end:
        try:
            out += mon_sock.recv(65536)
        except socket.timeout:
            break
    return out.decode(errors="replace")

def main():
    outlog, phase = sys.argv[1], sys.argv[2]
    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = (f"{OC_TOOLS}/usr/lib/x86_64-linux-gnu:"
                             f"{OC_TOOLS}/lib/x86_64-linux-gnu")
    for s in (SSOCK, MSOCK):
        if os.path.exists(s): os.unlink(s)
    args = ["-m", "512M", "-no-reboot",
            "-L", f"{OC_TOOLS}/usr/share/qemu",
            "-L", f"{OC_TOOLS}/usr/share/seabios",
            "-display", "none", "-vga", "std",
            "-drive", "file=build/ocire/ocire-boot-vga.img,format=raw,if=ide,index=0",
            "-drive", "file=build/ocire/ocire-hd-vga.img,format=raw,if=ide,index=1",
            "-serial", f"unix:{SSOCK},server=on,wait=on",
            "-monitor", f"unix:{MSOCK},server=on,wait=off"]
    log = open(outlog, "w", buffering=1)
    child = pexpect.spawn(QEMU, args, encoding="utf-8",
                          codec_errors="replace", timeout=240, env=env)
    ser = None
    for _ in range(120):
        try:
            ser = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            ser.connect(SSOCK); break
        except OSError:
            time.sleep(0.5)
    mon = None
    for _ in range(120):
        try:
            mon = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            mon.connect(MSOCK); break
        except OSError:
            time.sleep(0.5)
    ser.settimeout(1.0)
    sbuf = bytearray()

    def drain(t=1.2):
        end = time.time() + t
        while time.time() < end:
            try:
                d = ser.recv(65536)
                if d: sbuf.extend(d)
            except socket.timeout:
                pass
        out = bytes(sbuf); sbuf.clear(); return out

    def w(s): log.write(s + "\n")
    def raw(b): log.write(repr(b) + "\n")

    results = {}
    def check(name, cond):
        results[name] = cond
        log.write(f"[{'PASS' if cond else 'FAIL'}] {name}\n")

    try:
        # boot to the GRUB menu, then pick entry 2 (OCIRE full shell)
        seen = b""
        end = time.time() + 120
        while time.time() < end:
            d = drain(0.8)
            if d: seen += d
            if b"OCIRE boot supervisor" in seen or b"recovery (full shell)" in seen:
                break
        w("=== GRUB menu over serial (tail) ===")
        raw(seen[-400:])
        # entry 2 = one cursor-down + CR (menu highlight starts at 1)
        time.sleep(0.8)
        ser.sendall(b"\x1b[B")     # GRUB accepts ANSI down over serial
        time.sleep(0.8)
        ser.sendall(b"\r")
        # boot through to the OCIRE prompt
        seen2 = b""
        end = time.time() + 150
        while time.time() < end:
            d = drain(0.8)
            if d: seen2 += d
            if b"ocire>" in seen2[-64:]: break
        w(f"=== [phase {phase}] OCIRE boot (tail) ===")
        raw(seen2[-500:])
        check("OCIRE reached (full shell)", b"ocire>" in seen2)
        check("OCIRE in VGA text mode (no framebuffer tag)",
              b"VGA text 80x25" in seen2)

        # 30 marker lines force scrolling (2 rows per line + prompt row)
        total = b""
        for i in range(1, 31):
            time.sleep(0.15)
            ser.sendall(f"L{i:02d}\n".encode())
            total += drain(0.35)
        end = time.time() + 15
        while time.time() < end:
            d = drain(0.8)
            if d: total += d
            if total.rstrip().endswith(b"ocire>"): break
        w("=== marker lines echo (tail) ===")
        raw(total[-300:])
        check("30 markers executed", b"L30" in total)

        # dump the REAL VGA text cells
        resp = send_monitor(mon, "pmemsave 4734976 4000 build/fix1_vga_cells.bin", 1.5)
        w("=== pmemsave response ===")
        w(re.sub(r"\x1b\[[0-9;]*[A-Za-z]|\x1b\[K|\r", "", resp).strip()[-160:])
        time.sleep(0.8)
        cells_ok = os.path.exists("build/fix1_vga_cells.bin")
        check("VGA cell dump captured", cells_ok)
        if cells_ok:
            dump = open("build/fix1_vga_cells.bin", "rb").read()
            rows = []
            for r in range(25):
                row = ""
                for c in range(80):
                    cell = dump[(r * 80 + c) * 2] & 0xFF
                    row += chr(cell) if 0x20 <= cell < 0x7F else "."
                rows.append(row)
            w("=== VGA rows (live dump, byte-truth) ===")
            for i, r in enumerate(rows):
                log.write(f"{i:02d}|{r}|\n")
            blob = "\n".join(rows)
            check("late marker L30 present on screen", "L30" in blob)
            check("late marker L29 present on screen", "L29" in blob)
            check("early marker L01 scrolled off (no residue)", "L01" not in blob)
            check("early marker L02 scrolled off (no residue)", "L02" not in blob)
            check("early marker L05 scrolled off (no residue)", "L05" not in blob)
            check("early marker L08 scrolled off (no residue)", "L08" not in blob)
        w(f"=== SUMMARY {phase} ===")
        for k, v in results.items():
            log.write(f"{'PASS' if v else 'FAIL'}: {k}\n")
        n_pass = sum(1 for v in results.values() if v)
        log.write(f"TOTAL {n_pass}/{len(results)}\n")
    finally:
        try: ser.close()
        except Exception: pass
        try: mon.close()
        except Exception: pass
        child.close(force=True)
        log.close()
        sys.exit(0 if all(results.values()) else 1)

if __name__ == "__main__":
    main()
