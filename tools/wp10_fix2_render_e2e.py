#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# WP-10-chinese_support-fix2 rendering regression: pixel-verify that
# TUI hanzi render FULL 16x16 glyphs (the fix1..earlier bug repainted
# the TUI_WIDE_CONT cell with the background, erasing every glyph's
# right half). Boot the ISO, compare the on-screen 16x16 blocks against
# the EMBEDDED font bitmaps parsed from chinese/chinese_font_data.c.
# Position-independent: scan the whole frame for each glyph.
#
# v2.0.0 surface: the tui-cn launcher was removed at tui-unified and
# `font-test` moved to the ring-3 SVC surface (whose captured dispatch
# suppresses the framebuffer draw), so the fb-rendering hanzi surfaces
# are the zh DESKTOP pages: the root menu, the control panel, the
# Processes app and the Update page.  Language is set through the
# run-ush bridge (lang is an SVC command in v2).
import os, re, socket, subprocess, sys, time

OC_TOOLS = os.environ.get("OC_TOOLS", "/home/z/opt/extract/root")
QEMU = f"{OC_TOOLS}/usr/bin/qemu-system-x86_64"
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = "/home/z/oc-work/render_e2e"
os.makedirs(OUT, exist_ok=True)

smon, qmon = f"{OUT}/ser.sock", f"{OUT}/qmon.sock"
for p in (smon, qmon):
    if os.path.exists(p):
        os.unlink(p)

args = ["-m", "512M", "-cdrom", f"{ROOT}/build/opencube.iso", "-boot", "d",
        "-no-reboot", "-snapshot",
        "-L", f"{OC_TOOLS}/usr/share/qemu",
        "-L", f"{OC_TOOLS}/usr/share/seabios",
        "-vga", "std", "-display", "none",
        "-serial", f"unix:{smon},server,nowait",
        "-monitor", f"unix:{qmon},server,nowait",
        "-netdev", "user,id=n1", "-device", "e1000,netdev=n1"]
env = dict(os.environ)
env["LD_LIBRARY_PATH"] = (f"{OC_TOOLS}/usr/lib/x86_64-linux-gnu:"
                          f"{OC_TOOLS}/lib/x86_64-linux-gnu")
logf = open(f"{OUT}/serial.log", "wb")
p = subprocess.Popen([QEMU] + args, env=env, stdout=logf, stderr=logf,
                     stdin=subprocess.DEVNULL)

def ser_connect():
    for _ in range(100):
        try:
            s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            s.connect(smon)
            s.setblocking(False)
            return s
        except OSError:
            time.sleep(0.2)
    raise RuntimeError("no serial")

ser = ser_connect()
buf = b""

def expect(pat, timeout=240):
    global buf
    rx = re.compile(pat.encode() if isinstance(pat, str) else pat)
    dl = time.time() + timeout
    while time.time() < dl:
        if p.poll() is not None:
            raise RuntimeError(f"qemu exited rc={p.returncode}")
        try:
            d = ser.recv(65536)
            if d:
                buf += d
                logf.write(d)
                logf.flush()
        except BlockingIOError:
            pass
        m = rx.search(buf)
        if m:
            out = buf[:m.end()]
            buf = buf[m.end():]
            return out.decode("latin-1")
        time.sleep(0.1)
    raise TimeoutError(f"expect {pat!r}; tail={buf[-200:]!r}")

def run(cmd, timeout=120):
    global buf
    buf = b""
    ser.sendall(cmd.encode() + b"\n")
    out = expect(re.escape(cmd), timeout=30)
    out += expect(r"oc>", timeout=timeout)
    return out

def mon(cmd, wait=1.0):
    m = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    m.connect(qmon)
    m.setblocking(True)
    time.sleep(0.2)
    try:
        m.setblocking(False)
        while True:
            d = m.recv(4096)
            if not d:
                break
    except BlockingIOError:
        pass
    m.setblocking(True)
    m.sendall(cmd.encode() + b"\n")
    time.sleep(wait)
    m.close()

# ---- parse the embedded font bitmaps for the probe characters ----
def font_bitmap(cp):
    """Return 16 rows x 16-bit ints for a Unicode codepoint from the
    embedded tables (None when the glyph is absent)."""
    src = open(f"{ROOT}/chinese/chinese_font_data.c", encoding="utf-8").read()
    def arr_u16(name):
        m = re.search(name + r"\[[^\]]*\]\s*=\s*\{(.*?)\};", src, re.S)
        return [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]+)", m.group(1))]
    uto = arr_u16("chinese_font_uto_gb")
    # sorted by unicode: binary search
    lo, hi = 0, len(uto) // 2 - 1
    idx = None
    while lo <= hi:
        mid = (lo + hi) // 2
        u = uto[mid * 2]
        if u == cp:
            idx = uto[mid * 2 + 1]
            break
        if u < cp:
            lo = mid + 1
        else:
            hi = mid - 1
    if idx is None:
        return None
    if idx & 0x8000:
        table = arr_u16("chinese_font_symbols")
        base = idx & 0x7FFF
    else:
        table = None
    gsrc = open(f"{ROOT}/chinese/chinese_font_data.c", encoding="utf-8").read()
    if idx & 0x8000:
        m = re.search(r"chinese_font_symbols\[682\]\[32\]\s*=\s*\{(.*?)\};", gsrc, re.S)
    else:
        m = re.search(r"chinese_font_glyphs\[6763\]\[32\]\s*=\s*\{(.*?)\};", gsrc, re.S)
    vals = re.findall(r"0x([0-9A-Fa-f]{2})", m.group(1))
    base_off = (idx & 0x7FFF if idx & 0x8000 else idx) * 32
    rows = []
    for r in range(16):
        hi_b = int(vals[base_off + r * 2], 16)
        lo_b = int(vals[base_off + r * 2 + 1], 16)
        rows.append((hi_b << 8) | lo_b)
    return rows

PROBES = [("硬", 0x786C), ("无", 0x65E0), ("系", 0x7CFB), ("中", 0x4E2D)]

def glyph_present(w, h, px, rows, fg_test):
    """Scan the frame for a 16x16 block whose ink mask matches `rows`
    exactly, evaluating 'ink' with fg_test(pixel)."""
    gw, gh = 16, 16
    for oy in range(0, h - gh):
        for ox in range(0, w - gw):
            ok = True
            for ry in range(gh):
                bits = rows[ry]
                rowbase = (oy + ry) * w + ox
                for rx in range(gw):
                    want = (bits >> (15 - rx)) & 1
                    got = 1 if fg_test(px[rowbase + rx]) else 0
                    if want != got:
                        ok = False
                        break
                if not ok:
                    break
            if ok:
                return (ox, oy)
    return None

try:
    # v2.0.0: boot lands in the ring-3 ush shell; leave it for the L0
    # recovery prompt (the desktop rounds need it)
    expect(r"(?:ush>|oc>)", timeout=240)
    if b"ush>" in buf:
        ser.sendall(b"exit\n")
        expect(r"oc>", timeout=90)
    print("boot ok")

    # v2: lang is a ring-3 SVC command - set zh through the run-ush
    # bridge (the language persists into the L0 desktop)
    ser.sendall(b"run ush\n")
    expect(r"ush>", timeout=90)
    ser.sendall(b"lang zh\n")
    expect(r"ush>", timeout=30)
    ser.sendall(b"exit\n")
    expect(r"oc>", timeout=60)

    # zh desktop: root menu (系统信息 / 中文输入 give 系 and 中),
    # the Processes app (任务管理) and the control panel (硬件 gives
    # 硬; the Network description 无线 gives 无), the Update page
    # (检查更新).  FB backend - deterministic, unlike the serial
    # backend timing the interactive suites race against.
    buf = b""
    ser.sendall(b"tui\n")
    time.sleep(4)
    mon(f"screendump {OUT}/tui_cn_launcher.ppm", wait=1.5)

    # root -> Processes app (quick-open digit 2)
    ser.sendall(b"2")
    time.sleep(3)
    mon(f"screendump {OUT}/cn_processes.ppm", wait=1.5)
    ser.sendall(b"\x1bz")      # Esc leaves the app ('z' pairs the Esc)
    time.sleep(2)

    # root -> control panel (digit 3)
    ser.sendall(b"3")
    time.sleep(2.5)
    mon(f"screendump {OUT}/cn_panel.ppm", wait=1.5)

    # panel -> Update page (digit 6; crash-test sits at 5)
    ser.sendall(b"6")
    time.sleep(2.5)
    mon(f"screendump {OUT}/cn_update.ppm", wait=1.5)
    ser.sendall(b"\x1b[26~")   # F10 leaves the page
    time.sleep(1.5)
    ser.sendall(b"\x1bz")      # Esc: panel -> root
    time.sleep(1.5)
    ser.sendall(b"\x1bz")      # Esc: root -> leave the desktop
    time.sleep(2)
    expect(r"oc>", timeout=60)

    # restore English through the ring-3 lang command
    ser.sendall(b"run ush\n")
    expect(r"ush>", timeout=90)
    ser.sendall(b"lang en\n")
    expect(r"ush>", timeout=30)
    ser.sendall(b"exit\n")
    expect(r"oc>", timeout=60)

    from PIL import Image
    fails = 0

    def load(name):
        im = Image.open(f"{OUT}/{name}.ppm").convert("RGB")
        w, h = im.size
        px = list(im.getdata())
        return w, h, px

    ink_tests = {
        "white": lambda p: p[0] > 128 and p[1] > 128 and p[2] > 128,
        "dark": lambda p: p[0] < 90 and p[1] < 90 and p[2] < 90,
        # aqua/colored header ink (TUI_COLOR_AQUA on black)
        "bright": lambda p: (p[0] + p[1] + p[2]) > 250,
    }

    def find_glyph(name, ppm, cp):
        w, h, px = load(ppm)
        rows = font_bitmap(cp)
        if rows is None:
            return None
        for tag, test in ink_tests.items():
            pos = glyph_present(w, h, px, rows, test)
            if pos:
                return f"{pos}({tag})"
        return None

    # v2: the console font-test path renders through the SVC capture
    # machinery, which suppresses the framebuffer draw - the fb hanzi
    # surfaces in v2 are the zh desktop pages (probes below).

    # zh root menu: 系 (系统信息) and 中 (中文输入)
    for name, cp in [("系", 0x7CFB), ("中", 0x4E2D)]:
        pos = find_glyph(name, "tui_cn_launcher", cp)
        print(("PASS" if pos else "FAIL") + f" zh root renders FULL {name} (U+{cp:04X}) glyph {pos}")
        if not pos:
            fails += 1

    # control panel menu: 硬 (硬件) and 无 (Network description 无线)
    for name, cp in [("硬", 0x786C), ("无", 0x65E0)]:
        pos = find_glyph(name, "cn_panel", cp)
        print(("PASS" if pos else "FAIL") + f" zh panel renders FULL {name} (U+{cp:04X}) glyph {pos}")
        if not pos:
            fails += 1

    # v2 navigation checks: the Processes app opened (任务管理) and
    # the Update page opened (检查更新 lines)
    def page_has(name, probes):
        w, h, px = load(name)
        hits = 0
        for cp in probes:
            rows = font_bitmap(cp)
            if not rows:
                continue
            for test in ink_tests.values():   # selected rows invert ink
                if glyph_present(w, h, px, rows, test):
                    hits += 1
                    break
        return hits, len(probes)

    # 任务管理 on the processes app (任 0x4EFB, 务 0x52A1, 管 0x7BA1, 理 0x7406)
    hits, tot = page_has("cn_processes", [0x4EFB, 0x52A1, 0x7BA1, 0x7406])
    ok = hits == tot
    print(("PASS" if ok else "FAIL") + f" zh Processes app opened (任务管理 glyphs {hits}/{tot})")
    if not ok:
        fails += 1

    # 检查更新 on the update page (检 0x68C0, 查 0x67E5, 更 0x66F4, 新 0x65B0)
    hits, tot = page_has("cn_update", [0x68C0, 0x67E5, 0x66F4, 0x65B0])
    ok = hits == tot
    print(("PASS" if ok else "FAIL") + f" zh Update page opened (检查更新 glyphs {hits}/{tot})")
    if not ok:
        fails += 1
finally:
    if p.poll() is None:
        try:
            mon("quit", wait=0.5)
        except Exception:
            pass
        p.terminate()
    logf.close()

print(f"RENDER E2E: {'ALL PASS' if fails == 0 else f'{fails} FAIL'}")
sys.exit(1 if fails else 0)
