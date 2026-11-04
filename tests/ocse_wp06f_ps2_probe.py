#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - tests/ocse_wp06f_ps2_probe.py
# WP06f probe: can the QEMU monitor drive the OCSE GUI pointer through
# the PS/2 aux mouse (the WP-06c third pointer source)?  Boots the
# plain machine (no -device usb-mouse, so the monitor's mouse events
# route to the default PS/2 mouse), parks the pointer at (0,0), moves
# it to (400,300), takes two screendumps and reports where the cursor
# sprite landed in each.
import os
import socket
import subprocess
import sys
import time

BUILD = "/home/z/oc-public/build"
MON = f"{BUILD}/wp06f-mon.sock"
OUT = f"{BUILD}/wp06f-ps2-probe"
for p in (MON, OUT + "-a.ppm", OUT + "-b.ppm"):
    if os.path.exists(p):
        os.unlink(p)

CMD = ("qemu-system-x86_64 "
       "-L /home/z/opt/extract/usr/share/qemu "
       "-L /home/z/opt/extract/usr/share/seabios "
       "-m 256M "
       f"-drive file={BUILD}/ocse-test-disk64.raw,format=raw,if=ide,index=0,media=disk "
       f"-cdrom {BUILD}/ocse.iso -boot d "
       "-no-reboot -display none -serial stdio "
       f"-monitor unix:{MON},server,nowait -vga std")


class Mon:
    def __init__(self, path, wait=20):
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
        raise RuntimeError("monitor socket never came up")

    def cmd(self, c, settle=0.35):
        self.s.sendall((c + "\n").encode())
        time.sleep(settle)
        try:
            self.s.recv(65536)
        except socket.timeout:
            pass


def cursor_pos(ppm):
    """Locate the cursor sprite's white outline top-left in a PPM.

    The arrow: white outline pixels + black fill on a light background.
    We look for the distinctive FIRST row of the sprite (a short white
    run with black below) - simpler: report the topmost-leftmost pixel
    that is near-white AND has a near-black pixel within 2px right.
    """
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
        w, h, maxv = tok(), tok(), tok()
        data = f.read(w * h * 3)
    def px(x, y):
        o = (y * w + x) * 3
        return data[o], data[o + 1], data[o + 2]
    for y in range(h):
        for x in range(w - 2):
            r, g, b = px(x, y)
            if r > 220 and g > 220 and b > 220:
                r2, g2, b2 = px(x + 2, y + 2)
                if r2 < 90 and g2 < 90 and b2 < 90:
                    return (x, y)
    return None


qemu = subprocess.Popen(CMD.split(), stdin=subprocess.DEVNULL,
                        stdout=subprocess.DEVNULL,
                        stderr=subprocess.STDOUT)
try:
    mon = Mon(MON)
    time.sleep(12)          # OCBL menu countdown -> OCSE boot -> GUI up
    mon.cmd("mouse_move -2000 -2000", 0.4)
    mon.cmd("mouse_move 400 300", 0.4)
    mon.cmd(f"screendump {OUT}-a.ppm", 0.8)
    mon.cmd("mouse_move -2000 -2000", 0.4)
    mon.cmd("mouse_move 100 100", 0.4)
    mon.cmd(f"screendump {OUT}-b.ppm", 0.8)
finally:
    qemu.terminate()
    try:
        qemu.wait(5)
    except subprocess.TimeoutExpired:
        qemu.kill()

for tag in ("a", "b"):
    p = f"{OUT}-{tag}.ppm"
    if os.path.exists(p):
        print("dump %s: cursor sprite at %s" % (tag, cursor_pos(p)))
    else:
        print("dump %s: MISSING" % tag)
