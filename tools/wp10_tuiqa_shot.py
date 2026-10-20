#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""WP-10-tui-qa Chinese-rendering screenshot gate (framebuffer pixels).

Boots the ISO with the real framebuffer console (NO tui.backend=serial),
drives the shell over the serial line, and captures monitor screendumps:

  [1] font-test   : the CJK glyph sample row - pixel-verified (every
                    16x16 glyph block must have fg+bg) and exported as
                    an 8x nearest-neighbor ZOOM so the bottom pixel rows
                    of each glyph are inspectable by eye
  [2] zh desktop  : lang zh + tui - the desktop renders Chinese labels
                    to the framebuffer; teal title bar + CJK menu band
                    pixel-verified; breadcrumb + menu crops zoomed 6x
  [3] en desktop  : same session switched back - the crops must differ
                    from the zh frame (labels really changed)

Usage: python3 tools/wp10_tuiqa_shot.py <iso> <outdir> [bios|uefi]
Exit code: number of failed checks."""
import os, re, sys, time, socket, subprocess
import pexpect
from PIL import Image

OC_TOOLS = os.environ.get("OC_TOOLS", "/home/z/opt/extract")
QEMU = f"{OC_TOOLS}/usr/bin/qemu-system-x86_64"

def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    m = re.match(rb"P6\n(\d+) (\d+)\n255\n", data)
    if not m:
        return None, None
    w, h = int(m.group(1)), int(m.group(2))
    return (w, h), data[m.end():]

def px(buf, w, x, y):
    o = (y * w + x) * 3
    return (buf[o], buf[o+1], buf[o+2])

RESULTS = []
def check(name, ok, ev=""):
    RESULTS.append((name, bool(ok)))
    print(f"  {'PASS' if ok else 'FAIL'}  {name}" + (f"   [{ev}]" if ev else ""))

def main():
    iso = sys.argv[1]
    outdir = sys.argv[2]
    mode = sys.argv[3] if len(sys.argv) > 3 else "bios"
    os.makedirs(outdir, exist_ok=True)
    qmon = "/tmp/tuiqa_qmon.sock"
    if os.path.exists(qmon):
        os.unlink(qmon)

    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = (f"{OC_TOOLS}/usr/lib/x86_64-linux-gnu:"
                              f"{OC_TOOLS}/lib/x86_64-linux-gnu")
    args = ["-m", "512M", "-cdrom", iso, "-boot", "d",
            "-L", f"{OC_TOOLS}/usr/share/qemu",
            "-L", f"{OC_TOOLS}/usr/share/seabios",
            "-display", "vnc=127.0.0.1:0",
            "-serial", "stdio", "-monitor", "unix:" + qmon + ",server,nowait",
            "-vga", "std", "-no-reboot", "-snapshot"]
    if mode == "uefi":
        args += ["-bios", f"{OC_TOOLS}/usr/share/ovmf/OVMF.fd"]
    child = pexpect.spawn(QEMU, args, timeout=200, encoding="latin-1",
                          codec_errors="replace", env=env)

    def drain(secs):
        out = ""
        end = time.time() + secs
        while time.time() < end:
            try:
                out += child.read_nonblocking(size=65536, timeout=0.3)
            except Exception:
                pass
        return out

    def run(cmd, t=60):
        child.sendline(cmd)
        child.expect(r"oc>\s*", timeout=t)
        return child.before or ""

    def screendump(name):
        ppm = os.path.join(outdir, name + ".ppm")
        mon = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        mon.connect(qmon)
        time.sleep(0.2)
        try:
            mon.setblocking(False)
            while True:
                d = mon.recv(4096)
                if not d:
                    break
        except Exception:
            pass
        mon.setblocking(True)
        mon.sendall(b"screendump " + ppm.encode() + b"\n")
        time.sleep(1.2)
        try:
            mon.setblocking(False)
            while True:
                d = mon.recv(4096)
                if not d:
                    break
        except Exception:
            pass
        mon.close()
        return ppm

    def save_zoom(ppm, crop, zoom, name):
        """crop=(x,y,w,h) -> nearest-neighbor zoomed PNG (PIL box form)"""
        x, y, w, h = crop
        crop = (x, y, x + w, y + h)
        size, buf = read_ppm(ppm)
        if not size:
            return None
        im = Image.open(ppm)
        c = im.crop(crop)
        z = c.resize((c.width * zoom, c.height * zoom), Image.NEAREST)
        out = os.path.join(outdir, name + ".png")
        z.save(out)
        return out

    child.expect(r"oc>\s*", timeout=200)

    # ---- [1] font-test CJK sample row ----
    # The sample row is drawn by render_chinese_string() as WHITE glyphs
    # on PURE BLACK at the current console row - the only pure-black
    # band on the (16,16,20) console background, so it is locatable by
    # its color signature.  Sample text: 中文测试 OpenCube 测试.
    run("lang zh")
    run("font-test")
    time.sleep(1.0)
    ppm1 = screendump("cn_font_test")
    size, buf = read_ppm(ppm1)
    ok_font = False
    if size:
        sample_row = -1
        for row in range(36, 2, -1):    # scan from the bottom up
            white = black = 0
            for x in range(0, 176, 2):
                for y in range(row * 16, row * 16 + 16, 4):
                    p = px(buf, size[0], x, y)
                    if p == (255, 255, 255):
                        white += 1
                    elif p == (0, 0, 0):
                        black += 1
            if white >= 30 and black > 200:
                sample_row = row
                break
        if sample_row >= 0:
            sy = sample_row * 16
            blocks = 0
            for gx in range(4):          # 中文测试 = 4 leading CJK glyphs
                w_cnt = sum(1 for y in range(sy, sy + 16)
                            for x in range(gx * 16, gx * 16 + 16)
                            if px(buf, size[0], x, y) == (255, 255, 255))
                if w_cnt >= 20:
                    blocks += 1
            ok_font = blocks >= 4
            check("font-test: CJK glyph blocks on screen (fb pixels)",
                  ok_font, f"row {sample_row}, {blocks}/4 glyphs (16x16 white-on-black)")
            save_zoom(ppm1, (0, sy + 8, 176, 8), 12, "zoom_cn_font_bottom")
            save_zoom(ppm1, (0, sy, 176, 16), 8, "zoom_cn_font_full")
        else:
            check("font-test: CJK glyph blocks on screen (fb pixels)",
                  False, "sample row not found by color signature")
    else:
        check("font-test: CJK glyph blocks on screen (fb pixels)", False,
              "screendump unreadable")

    # ---- [2] zh desktop ----
    child.sendline("tui")
    time.sleep(4.0)
    drain(1.0)
    ppm2 = screendump("cn_desktop_zh")
    size2, buf2 = read_ppm(ppm2)
    if size2:
        teal = sum(1 for x in range(0, size2[0], 2)
                   if px(buf2, size2[0], x, 4) == (0x00, 0xAA, 0xAA))
        # CJK menu band: 16px CJK glyphs reach the BOTTOM quarter of the
        # cell (rows y+12..y+15); the 8x16 ASCII font does not.  Rows 4..11
        # (row 3 is the reversed-video focus row - excluded).
        cjk_rows = 0
        for row in range(4, 12):
            bottom = sum(1 for yy in range(row * 16 + 12, row * 16 + 16)
                         for xx in range(8, 320, 2)
                         if px(buf2, size2[0], xx, yy) not in
                         ((0, 0, 0), (0x00, 0xAA, 0xAA)))
            if bottom >= 30:
                cjk_rows += 1
        check("zh desktop: teal title bar on framebuffer", teal > 100,
              f"{teal} teal px")
        check("zh desktop: CJK glyph rows in menu band", cjk_rows >= 5,
              f"{cjk_rows}/8 rows carry 16px glyphs (bottom-quarter px)")
        save_zoom(ppm2, (0, 0, 320, 32), 6, "zoom_cn_desktop_title")   # title+breadcrumb
        save_zoom(ppm2, (8, 48, 312, 144), 4, "zoom_cn_desktop_menu")  # menu rows 3-11
    else:
        check("zh desktop: teal title bar on framebuffer", False)
        check("zh desktop: CJK glyph rows in menu band", False)

    # leave the desktop (Esc at root; serial bridge double-ESC)
    child.send("\x1b\x1b")
    time.sleep(2.0)
    try:
        child.expect(r"oc>\s*", timeout=15)
    except Exception:
        pass

    # ---- [3] en desktop (same session, labels must differ) ----
    run("lang en")
    child.sendline("tui")
    time.sleep(4.0)
    drain(1.0)
    ppm3 = screendump("cn_desktop_en")
    size3, buf3 = read_ppm(ppm3)
    if size3 and size2:
        # menu band only (x 8..320, y 64..192): the zh and en label
        # glyphs must differ substantially there
        diff = tot = 0
        for yy in range(64, 192, 2):
            for xx in range(8, 320, 2):
                o = (yy * size2[0] + xx) * 3
                tot += 1
                if buf2[o:o+3] != buf3[o:o+3]:
                    diff += 1
        check("en desktop: menu band differs from zh (labels changed)",
              diff > tot * 15 // 100, f"{diff}/{tot} menu-band px differ")
        save_zoom(ppm3, (8, 48, 312, 144), 4, "zoom_en_desktop_menu")
    else:
        check("en desktop: frame differs from zh (labels changed)", False)

    child.send("\x1b\x1b")            # leave the en desktop
    time.sleep(2.0)
    try:
        child.expect(r"oc>\s*", timeout=15)
    except Exception:
        pass
    run("echo shot-final-alive")
    child.terminate(force=True)

    ok = sum(1 for _, v in RESULTS if v)
    print(f"=== {ok}/{len(RESULTS)} screenshot checks passed ({mode}) ===")
    sys.exit(len(RESULTS) - ok)

if __name__ == "__main__":
    main()
