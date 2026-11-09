#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - tests/ocse_wp06f_fix_battery.py (3.0.0-WP06f-system_fix)
# The WP06f acceptance battery - live evidence for the five fixes:
#
#   A  double buffer + standard arrow pointer
#      - serial declares "gui double-buffer: on (back buffer attached)"
#      - pointer move: two dumps differ in EXACTLY the old+new sprite
#        boxes (the local refresh: only ~16 front-buffer rows/pointer
#        move; the old code rewrote the whole 1.8 MB surface per event)
#      - the 13x16 classic arrow sprite pixels (white outline + black
#        fill) sit at the tracked pointer position
#   B  the fixed Chinese font: zh panel + zh wizard pages screenshots
#   C  Advanced options (GUI/OCSECL parity): default OFF (no delete
#      button), ON shows Delete partition, rmpart runs through the
#      confirm page and the partition disappears
#   D  flicker evidence: rapid dumps across page transitions - every
#      frame carries the full title bar (no blank/partial repaints)
#   E  existing system: default refuses (the WP-06c gate), Advanced
#      proceeds with the declared serial line (second VM, w6 disk)
#
# Pointer driving: the QEMU monitor's PS/2 relative mouse (the WP-06c
# aux pointer).  Deltas are kept small and realistic (<=300 per
# command, split like a real hand) - a synthetic -2000 "park" burst
# wedges QEMU's internal PS/2 queue (keyboard stays alive; verified in
# the WP06f probe runs), so the battery TRACKS the pointer position
# instead: the panel starts at (400,300) and every wizard page's
# wz_reset() recenters to (400,300).
import os
import socket
import sys
import time
import pexpect

BUILD = "/home/z/oc-public/build"
OUTDIR = f"{BUILD}/wp06f"
DISK = f"{BUILD}/ocse-wp06f-disk.raw"
DISK2 = f"{BUILD}/ocse-w6-disk.raw"        # the w6-installed disk (has OCOS)
MON = f"{BUILD}/wp06f-mon.sock"
os.makedirs(OUTDIR, exist_ok=True)
if os.path.exists(DISK):
    os.unlink(DISK)
# 128 MiB like the w6 battery: the wizard refuses to create
# a partition below 64 MiB (WZ_GAP_MIN) and a 64 MiB disk
# only leaves a 63 MiB gap - live-caught in the first run.
with open(DISK, "wb") as f:
    f.truncate(128 * 1024 * 1024)
if os.path.exists(MON):
    os.unlink(MON)

QEMU = "qemu-system-x86_64"
CMD = (f"{QEMU} -L /home/z/opt/extract/usr/share/qemu "
       f"-L /home/z/opt/extract/usr/share/seabios -m 256M "
       f"-drive file={DISK},format=raw,if=ide,index=0,media=disk "
       f"-cdrom {BUILD}/ocse.iso -boot d "
       f"-no-reboot -display none -serial stdio "
       f"-monitor unix:{MON},server,nowait -vga std")


class Mon:
    def __init__(self, path, wait=30):
        for _ in range(wait * 2):
            try:
                self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                self.s.connect(path)
                self.s.settimeout(0.2)
                try:
                    while self.s.recv(4096):
                        pass
                except socket.timeout:
                    pass
                return
            except OSError:
                time.sleep(0.5)
        raise RuntimeError("monitor never came up")

    def cmd(self, c, settle=0.35):
        self.s.sendall((c + "\n").encode())
        time.sleep(settle)
        try:
            self.s.recv(65536)
        except socket.timeout:
            pass

    def dump(self, name, settle=0.3):
        self.cmd(f"screendump {OUTDIR}/{name}.ppm", settle + 0.5)


class Pointer:
    """Tracks the guest pointer and issues realistic small deltas.

    The wizard's wz_reset() recenters every page to (400,300); the
    panel keeps its position across wizard runs."""

    def __init__(self, mon):
        self.mon = mon
        self.panel_x, self.panel_y = 400, 300
        self.wiz_x, self.wiz_y = 400, 300
        self.in_wizard = False

    @property
    def x(self):
        return self.wiz_x if self.in_wizard else self.panel_x

    @property
    def y(self):
        return self.wiz_y if self.in_wizard else self.panel_y

    def page(self):
        """A wizard page change recenters (wz_reset)."""
        if self.in_wizard:
            self.wiz_x, self.wiz_y = 400, 300

    def _set(self, x, y):
        if self.in_wizard:
            self.wiz_x, self.wiz_y = x, y
        else:
            self.panel_x, self.panel_y = x, y

    def move(self, x, y):
        while True:
            dx = max(-300, min(300, x - self.x))
            dy = max(-300, min(300, y - self.y))
            if dx == 0 and dy == 0:
                break
            self.mon.cmd(f"mouse_move {dx} {dy}", 0.3)
            self._set(self.x + dx, self.y + dy)

    def click(self, x, y, settle=0.3):
        self.move(x, y)
        self.mon.cmd("mouse_button 1", 0.28)
        self.mon.cmd("mouse_button 0", 0.5)


def load(ppm):
    with open(ppm, "rb") as f:
        assert f.read(2) == b"P6"
        def tok():
            v = b""
            while True:
                c = f.read(1)
                if c in (b" ", b"\n", b"\r", b"\t"):
                    if v:
                        return int(v)
                elif c == b"#":
                    f.readline()
                else:
                    v += c
        w, h, mx = tok(), tok(), tok()
        return w, h, f.read(w * h * 3)


def diff_bbox(p1, p2):
    w, h, a = load(p1)
    _, _, b = load(p2)
    xs, ys = [], []
    for o in range(0, len(a), 3):
        if a[o:o + 3] != b[o:o + 3]:
            i = o // 3
            xs.append(i % w)
            ys.append(i // w)
    if not xs:
        return w, h, None
    return w, h, (min(xs), max(xs), min(ys), max(ys), len(xs))


fails = []


def check(name, cond, detail=""):
    tag = "PASS" if cond else "FAIL"
    print(f"[WP06f {tag}] {name}" + (f" ({detail})" if detail else ""))
    if not cond:
        fails.append(name)


child = pexpect.spawn(CMD, timeout=180, encoding="utf-8",
                      codec_errors="replace")
child.logfile = sys.stdout
mon = None
ptr = None
try:
    mon = Mon(MON)
    ptr = Pointer(mon)

    # ---- A: double buffer on + pointer sprite + local refresh --------
    child.expect("gui double-buffer: on \(back buffer attached\)",
                 timeout=120)
    child.expect("ps2 aux mouse: present", timeout=30)
    check("A1 double-buffer attached (serial)", True)
    mon.dump("A-panel-arrow", 0.5)
    # pointer move +100,0: old (400,300) erased, new (500,300) drawn
    ptr.move(500, 300)
    mon.dump("A-after-move", 0.5)
    w, h, box = diff_bbox(f"{OUTDIR}/A-panel-arrow.ppm",
                          f"{OUTDIR}/A-after-move.ppm")
    ok = (box is not None and
          box[2] == 300 and box[3] == 315 and
          box[0] == 400 and box[1] == 512 and
          box[4] <= 2 * 13 * 16)
    check("A2 pointer move publishes ONLY the sprite rows "
          "(local refresh)", ok,
          f"bbox x[{box[0]}..{box[1]}] y[{box[2]}..{box[3]}] "
          f"{box[4]}px" if box else "no diff")
    # the arrow sprite at (500,300): row0 = outline(white) fill(black)
    # outline(white); row1 col1 = fill
    _, _, px = load(f"{OUTDIR}/A-after-move.ppm")
    def p(x, y):
        o = ((y * w) + x) * 3
        return px[o], px[o + 1], px[o + 2]
    o_l = p(500, 300)                     # row0 col0 outline
    f_l = p(501, 300)                     # row0 col1 fill
    o_r = p(502, 300)                     # row0 col2 outline
    check("A3 standard arrow sprite (white outline + black fill)",
          o_l[0] > 200 and o_l[1] > 200 and o_l[2] > 200 and
          f_l[0] < 90 and f_l[1] < 90 and f_l[2] < 90 and
          o_r[0] > 200 and o_r[1] > 200 and o_r[2] > 200,
          f"outline={o_l} fill={f_l}")

    # ---- B: the fixed Chinese font (zh panel + wizard pages) ---------
    ptr.click(665, 542)                   # language button -> zh
    child.expect("language toggled", timeout=30)
    time.sleep(0.5)
    mon.dump("B-panel-zh", 0.5)
    ptr.click(695, 426)                   # "立即安装" -> wizard (zh)
    child.expect("wizard: enter", timeout=30)
    ptr.in_wizard = True
    ptr.page()
    time.sleep(0.6)
    mon.dump("B-wizard-welcome-zh", 0.5)
    ptr.click(545, 523)                   # Next -> disk page
    ptr.page()
    time.sleep(0.9)
    mon.dump("B-wizard-disk-zh", 0.5)
    # CJK density check: the zh title row carries 16x16 glyph ink
    _, _, px2 = load(f"{OUTDIR}/B-panel-zh.ppm")
    def ink(img, x0, y0, x1, y1):
        n = 0
        for y in range(y0, y1):
            for x in range(x0, x1):
                o = ((y * w) + x) * 3
                if img[o] < 100 and img[o + 1] < 100 and img[o + 2] < 100:
                    n += 1
        return n
    n_zh = ink(px2, 40, 12, 460, 28)
    check("B1 zh panel renders CJK glyphs (title row ink)", n_zh > 400,
          f"{n_zh} dark px in the title row")

    # ---- C: Advanced options (GUI/OCSECL parity) ---------------------
    # disk page: Next -> part page (blank disk: no partitions yet)
    ptr.click(545, 523)
    ptr.page()
    time.sleep(0.9)
    mon.dump("C-part-safe-default", 0.5)  # default: NO delete button
    _, _, pxs = load(f"{OUTDIR}/C-part-safe-default.ppm")
    def bg_like(img, x, y):
        o = ((y * w) + x) * 3
        return (abs(img[o] - 0xF4) < 12 and abs(img[o + 1] - 0xF1) < 12
                and abs(img[o + 2] - 0xEC) < 14)
    n_bg = sum(1 for y in range(441, 486) for x in range(300, 490)
               if bg_like(pxs, x, y))
    check("C1 default OFF: no Delete button (safe surface)", n_bg > 4300,
          f"{n_bg}/8700 bg px in the advanced row area")
    # create a partition first (so there is something to delete)
    ptr.click(134, 523)                   # Create partition
    child.expect(r"\[wizard\] \$ mkpart hda 1 \d+", timeout=60)
    time.sleep(1.2)
    mon.dump("C-part-created", 0.5)
    # toggle Advanced ON
    ptr.click(134, 463)                   # Advanced options
    time.sleep(0.9)
    mon.dump("C-part-advanced-on", 0.5)
    _, _, pxa = load(f"{OUTDIR}/C-part-advanced-on.ppm")
    n_bg2 = sum(1 for y in range(441, 486) for x in range(300, 490)
                if bg_like(pxa, x, y))
    check("C2 Advanced ON shows the Delete partition button",
          n_bg2 < 4000, f"{n_bg2}/8700 bg px (button present)")
    # click Delete -> confirmation page
    ptr.click(395, 463)
    ptr.page()
    time.sleep(0.9)
    mon.dump("C-delconfirm", 0.5)
    # Yes -> rmpart runs (the wizard is in zh mode here, so the
    # language-independent proof = the command echo + the follow-up
    # lsdisk reporting 0 partitions)
    ptr.click(545, 523)
    child.expect(r"\[wizard\] \$ rmpart hda 1", timeout=60)
    child.expect(r"\[wizard\] \$ lsdisk", timeout=30)
    child.expect(r"0 part\(s\)", timeout=30)
    ptr.page()
    time.sleep(1.2)
    mon.dump("C-part-after-rmpart", 0.5)
    check("C3 rmpart ran through the confirm page (GUI = OCSECL parity)",
          True)
    _, _, pxp = load(f"{OUTDIR}/C-part-after-rmpart.ppm")
    n_ink = ink(pxp, 32, 130, 200, 148)
    check("C4 partition gone from the list after the delete",
          n_ink < 40, f"{n_ink} ink px in the first list row")

    # ---- D: flicker evidence - rapid dumps across transitions --------
    frames = []
    ptr.click(375, 523)                   # Back -> disk page
    ptr.page()
    for i in range(4):
        mon.dump(f"D-f{i}", 0.12)
        frames.append(f"D-f{i}")
    ptr.click(545, 523)                   # Next -> part page
    ptr.page()
    for i in range(4, 8):
        mon.dump(f"D-f{i}", 0.12)
        frames.append(f"D-f{i}")
    ptr.click(134, 463)                   # toggle Advanced
    for i in range(8, 12):
        mon.dump(f"D-f{i}", 0.12)
        frames.append(f"D-f{i}")
    ptr.move(200, 150)
    ptr.move(500, 400)
    ptr.move(300, 200)
    for i in range(12, 16):
        mon.dump(f"D-f{i}", 0.12)
        frames.append(f"D-f{i}")
    good = 0
    for f in frames:
        _, _, d = load(f"{OUTDIR}/{f}.ppm")
        dark = 0
        for x in range(0, w, 4):
            o = ((20 * w) + x) * 3
            if d[o] < 80 and d[o + 1] < 80 and d[o + 2] < 80:
                dark += 1
        bgc = 0
        tot = 0
        for y in range(0, h, 6):
            for x in range(0, w, 6):
                o = ((y * w) + x) * 3
                tot += 1
                if (abs(d[o] - 0xF4) < 8 and abs(d[o + 1] - 0xF1) < 8
                        and abs(d[o + 2] - 0xEC) < 8):
                    bgc += 1
        if dark > (w // 4) * 0.9 and bgc < tot * 0.85:
            good += 1
    check("D1 no blank/partial frame across 16 rapid captures",
          good == len(frames), f"{good}/{len(frames)} complete frames")

    # exit the wizard: Cancel -> confirm page -> Yes (exit)
    ptr.click(700, 523)
    ptr.page()
    time.sleep(0.6)
    ptr.click(545, 523)                   # Yes -> WZ_PG_EXIT
    ptr.in_wizard = False
    child.expect("wizard: exit", timeout=30)

    print("\n[WP06f phase A-D summary] fails:",
          fails if fails else "none")
except Exception as e:
    print(f"\n[WP06f CRASH] {e}")
    fails.append("crash")
finally:
    if mon:
        mon.s.close()
    child.terminate(force=True)
    time.sleep(1.0)

# ---- E: existing system - default refuses, Advanced proceeds ---------
if os.path.exists(DISK2):
    MON2 = f"{BUILD}/wp06f-mon2.sock"
    if os.path.exists(MON2):
        os.unlink(MON2)
    CMD2 = (f"{QEMU} -L /home/z/opt/extract/usr/share/qemu "
            f"-L /home/z/opt/extract/usr/share/seabios -m 256M "
            f"-drive file={DISK2},format=raw,if=ide,index=0,media=disk "
            f"-cdrom {BUILD}/ocse.iso -boot d "
            f"-no-reboot -display none -serial stdio "
            f"-monitor unix:{MON2},server,nowait -vga std")
    child = pexpect.spawn(CMD2, timeout=180, encoding="utf-8",
                          codec_errors="replace")
    child.logfile = sys.stdout
    mon2 = None
    try:
        mon2 = Mon(MON2)
        ptr2 = Pointer(mon2)
        # the OCBL menu defaults to the INSTALLED OCOS entry (first
        # OCOS wins) - interrupt the countdown and pick OCSE (cd0):
        # serial CSI down-arrow + Enter
        child.expect(r"Booting .* in 10s", timeout=90)
        child.send("\x1b[B")             # Down -> the OCSE entry
        time.sleep(0.5)
        child.send("\r")                 # Enter -> boot OCSE now
        child.expect("environment up: GUI mode", timeout=120)
        ptr2.click(695, 426)             # Install now -> wizard
        child.expect("wizard: enter", timeout=30)
        ptr2.in_wizard = True
        ptr2.page()
        time.sleep(0.6)
        ptr2.click(545, 523)             # welcome Next -> disk page
        ptr2.page()
        time.sleep(0.9)
        ptr2.click(545, 523)             # disk Next -> the gate fires
        child.expect(r"\[wizard\] refuse: existing OpenCubeOS on hda",
                     timeout=60)
        ptr2.page()
        time.sleep(0.8)
        mon2.dump("E-refusal-default", 0.5)
        check("E1 default refuses an existing install (WP-06c gate)",
              True)
        # Back -> disk page, enable Advanced, Next again -> proceeds
        ptr2.click(375, 523)             # Back (error page)
        ptr2.page()
        time.sleep(0.9)
        ptr2.click(134, 463)             # Advanced options ON
        time.sleep(0.9)
        ptr2.click(545, 523)             # Next -> advanced path
        child.expect(r"\[wizard\] advanced: existing system on hda - "
                     r"proceeding \(rmpart available\)", timeout=60)
        ptr2.page()
        time.sleep(0.9)
        mon2.dump("E-advanced-existing", 0.5)
        check("E2 Advanced proceeds past the gate (declared on serial)",
              True)
        ptr2.click(700, 523)             # Cancel
        ptr2.page()
        time.sleep(0.6)
        ptr2.click(545, 523)             # Yes -> exit
        child.expect("wizard: exit", timeout=30)
    except Exception as e:
        print(f"\n[WP06f-E CRASH] {e}")
        fails.append("E-crash")
    finally:
        try:
            mon2.s.close()
        except Exception:
            pass
        child.terminate(force=True)
        time.sleep(1.0)
else:
    print("[WP06f SKIP-E] no w6-installed disk (run ocse_w6_gui first)")
    fails.append("E-skipped")

print("\n[WP06f summary] fails:", fails if fails else "none")
sys.exit(1 if fails else 0)
