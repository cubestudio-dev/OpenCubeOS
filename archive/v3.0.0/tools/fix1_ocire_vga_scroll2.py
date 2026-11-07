#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""3.0.0-WP04-fix1 OCIRE VGA-text scroll (bug 10) - the frozen-band proof.

Environment (all declared fixture variants, see the report):
  - hd0: the WP-02 dual-boot fixture DISK variant whose GRUB core and
    config carry NO video modules, so the menu stays in text mode.
  - hd1: the WP-02 recovery DISK variant carrying a TEST-ONLY OCIRE
    kernel whose multiboot2 header omits the framebuffer REQUEST tag
    (the exact WP-01 header shape) - so even GRUB's built-in VBE
    never sets a mode, the kernel gets a type-2 (text) tag and OCIRE
    falls back to the WP-01 VGA text console at 0xB8000, on a REAL
    std VGA device (QEMU renders text mode via screendump).
  - vga_scroll (the fixed function) is byte-identical to the shipped
    OCIRE - only the header REQUEST differs, exactly as WP-01 shipped.

Proof design (deterministic, no OCR):
  markers L01..L13 -> screendump early.ppm  (screen first filled; the
  bottom half holds early-era content)
  markers L14..L30 -> screendump final.ppm  (25 more rows scrolled past)
  In the BUGGY half-copy build the bottom band (rows 12..23) is NEVER
  rewritten after the first scroll - the two dumps' bottom bands are
  byte-identical (the frozen residual - 残影).
  In the FIXED build the bottom band scrolls on - the bands differ.
  QEMU's text-mode rendering is deterministic, so the equality test is
  exact.

Usage: fix1_ocire_vga_scroll2.py <out.log> <before|after>
"""
import os, sys, time, socket, hashlib, re
import pexpect

OC_TOOLS = os.environ.get("OC_TOOLS", "/home/z/opt/extract")
QEMU = f"{OC_TOOLS}/usr/bin/qemu-system-x86_64"
SSOCK = "/tmp/fix1_v2_ser.sock"
MSOCK = "/tmp/fix1_v2_mon.sock"

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

def band_md5(ppm_path, row0, row1):
    """md5 of the pixel band [row0,row1) of a P6 720x400 PPM."""
    d = open(ppm_path, "rb").read()
    parts = d.split(b"\n", 3)
    pix = parts[3]
    w = 720
    y0, y1 = row0 * 16, row1 * 16
    band = b"".join(pix[(y * w * 3):((y + 1) * w * 3)] for y in range(y0, y1))
    return hashlib.md5(band).hexdigest()

def band_ink_stats(ppm_path, row0, row1):
    d = open(ppm_path, "rb").read()
    pix = d.split(b"\n", 3)[3]
    w = 720
    nb = 0
    for y in range(row0 * 16, row1 * 16):
        base = y * w * 3
        for x in range(0, w * 3, 3):
            if pix[base + x] > 40 or pix[base + x + 1] > 40 or pix[base + x + 2] > 40:
                nb += 1
    return nb

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
        # GRUB menu -> entry 2
        seen = b""
        end = time.time() + 120
        while time.time() < end:
            d = drain(0.8)
            if d: seen += d
            if b"OCIRE boot supervisor" in seen: break
        time.sleep(0.8)
        ser.sendall(b"\x1b[B")
        time.sleep(0.8)
        ser.sendall(b"\r")
        seen2 = b""
        end = time.time() + 150
        while time.time() < end:
            d = drain(0.8)
            if d: seen2 += d
            if b"ocire>" in seen2[-64:]: break
        w(f"=== [{phase}] OCIRE boot (tail) ===")
        raw(seen2[-260:])
        check("OCIRE reached (full shell)", b"ocire>" in seen2)
        check("OCIRE in VGA text mode (type-2 tag fallback)",
              b"VGA text fallback" in seen2)

        # markers L01..L13 -> early dump
        for i in range(1, 14):
            time.sleep(0.15)
            ser.sendall(f"L{i:02d}\n".encode())
            drain(0.35)
        end = time.time() + 10
        while time.time() < end:
            d = drain(0.8)
            if d: seen2 += d
            if seen2.rstrip().endswith(b"ocire>"): break
        send_monitor(mon, "screendump build/fix1_early.ppm", 1.5)
        w("early dump: " + ("ok" if os.path.exists("build/fix1_early.ppm") else "MISSING"))

        # markers L14..L30 -> final dump
        for i in range(14, 31):
            time.sleep(0.15)
            ser.sendall(f"L{i:02d}\n".encode())
            drain(0.35)
        end = time.time() + 10
        while time.time() < end:
            d = drain(0.8)
            if d: seen2 += d
            if seen2.rstrip().endswith(b"ocire>"): break
        send_monitor(mon, "screendump build/fix1_final.ppm", 1.5)
        w("final dump: " + ("ok" if os.path.exists("build/fix1_final.ppm") else "MISSING"))

        e_ok = os.path.exists("build/fix1_early.ppm")
        f_ok = os.path.exists("build/fix1_final.ppm")
        check("both screendumps captured", e_ok and f_ok)
        if e_ok and f_ok:
            top_e = band_md5("build/fix1_early.ppm", 0, 12)
            top_f = band_md5("build/fix1_final.ppm", 0, 12)
            bot_e = band_md5("build/fix1_early.ppm", 12, 24)
            bot_f = band_md5("build/fix1_final.ppm", 12, 24)
            w(f"top band md5 early={top_e} final={top_f}")
            w(f"bottom band md5 early={bot_e} final={bot_f}")
            w(f"bottom ink early={band_ink_stats('build/fix1_early.ppm',12,24)} "
              f"final={band_ink_stats('build/fix1_final.ppm',12,24)}")
            check("top band scrolled on (early != final)", top_e != top_f)
            if phase == "before":
                check("BUG 10 REPRODUCED: bottom band frozen (identical md5 = residual)",
                      bot_e == bot_f)
                check("bottom band still has ink (text frozen on screen)",
                      band_ink_stats("build/fix1_final.ppm", 12, 24) > 200)
            else:
                check("BUG 10 FIXED: bottom band scrolled on (md5 changed)",
                      bot_e != bot_f)
                check("bottom band still has ink (live content)",
                      band_ink_stats("build/fix1_final.ppm", 12, 24) > 200)
        # keep the evidence pair inside the evidence dir
        import shutil
        shutil.copy("build/fix1_early.ppm",
                    f"evidence/3_0_0_wp04_fix1/{phase}_vga_early.ppm")
        shutil.copy("build/fix1_final.ppm",
                    f"evidence/3_0_0_wp04_fix1/{phase}_vga_final.ppm")
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
