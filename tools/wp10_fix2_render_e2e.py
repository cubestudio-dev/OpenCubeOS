#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# WP-10-chinese_support-fix2 rendering regression: pixel-verify that
# TUI hanzi render FULL 16x16 glyphs (the fix1..earlier bug repainted
# the TUI_WIDE_CONT cell with the background, erasing every glyph's
# right half). Boot the ISO, enter tui-cn, screendump, then compare the
# on-screen 16x16 blocks against the EMBEDDED font bitmaps parsed from
# chinese/chinese_font_data.c. Position-independent: scan the whole
# frame for each glyph.
import os, re, socket, subprocess, sys, time

OC_TOOLS = "/home/z/opt/extract"
QEMU = f"{OC_TOOLS}/usr/bin/qemu-system-x86_64"
ROOT = "/home/z/oc-read/OpenCubeOS"
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
    expect(r"oc>", timeout=240)
    print("boot ok")

    # console path reference: font-test sample row (中) at a known spot
    run("font-test")
    time.sleep(1)
    mon(f"screendump {OUT}/font_test.ppm", wait=1.5)
    # TUI path: the launcher menu (硬/无/系/中) with the WIDE_CONT fix
    buf = b""
    ser.sendall(b"tui-cn\n")
    time.sleep(4)
    mon(f"screendump {OUT}/tui_cn_launcher.ppm", wait=1.5)
    ser.sendall(b"q\n")
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

    # console check: 中 sample row
    pos = find_glyph("zhong", "font_test", 0x4E2D)
    print(("PASS" if pos else "FAIL") + f" console font-test renders FULL 中 glyph {pos}")
    if not pos:
        fails += 1

    # TUI check: launcher menu glyphs (the selected row inverts to dark
    # ink on cyan - both ink polarities are tried per probe)
    for name, cp in PROBES:
        pos = find_glyph(name, "tui_cn_launcher", cp)
        print(("PASS" if pos else "FAIL") + f" TUI renders FULL {name} (U+{cp:04X}) glyph {pos}")
        if not pos:
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
