#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - 3.0.0-WP05-ocbl
# File: ocbl/qemu_snap.py
# Purpose: boot a QEMU image, screendump the VGA text screen after a
#          settle delay, and render the 80x25 text grid as ASCII (glyph
#          cells sampled at the 9x16 cell grid of the standard font).
#          Brings-up helper - not part of any acceptance gate.
# Usage: qemu_snap.py <iso> <out.txt> [seconds]
import socket
import subprocess
import sys
import time
import os

def main():
    iso = sys.argv[1]
    out = sys.argv[2]
    settle = float(sys.argv[3]) if len(sys.argv) > 3 else 6.0
    sock = '/tmp/ocbl_snap.sock'
    ppm = '/tmp/ocbl_snap.ppm'
    if os.path.exists(sock):
        os.unlink(sock)
    if os.path.exists(ppm):
        os.unlink(ppm)
    cmd = [
        'qemu-system-x86_64',
        '-L', '/home/z/opt/extract/share/qemu',
        '-L', '/home/z/opt/extract/share/seabios',
        '-m', '256M', '-cdrom', iso, '-boot', 'd', '-no-reboot',
        '-display', 'none', '-serial', 'file:/tmp/ocbl_snap_serial.log',
        '-monitor', f'unix:{sock},server,nowait', '-vga', 'std',
    ]
    p = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(settle)
    s = socket.socket(socket.AF_UNIX)
    s.connect(sock)
    time.sleep(0.3)
    try:
        s.recv(4096)
    except Exception:
        pass

    def mcmd(c, wait=0.6):
        s.sendall((c + '\n').encode())
        time.sleep(wait)
        out = b''
        s.settimeout(0.4)
        try:
            while True:
                b = s.recv(65536)
                if not b:
                    break
                out += b
        except Exception:
            pass
        return out.decode(errors='replace')

    mcmd('screendump ' + ppm, 1.0)
    mcmd('quit', 0.5)
    p.wait(timeout=10)

    from PIL import Image
    img = Image.open(ppm).convert('RGB')
    w, h = img.size
    px = img.load()
    rows = []
    # VGA text grid: 720x400, 80x25 cells => 9x16 per cell
    for cy in range(25):
        row = ''
        for cx in range(80):
            hits = 0
            for gy in (2, 6, 10, 13):
                for gx in (1, 4, 7):
                    x = cx * 9 + gx
                    y = cy * 16 + gy
                    if 0 <= x < w and 0 <= y < h:
                        r, g, b = px[x, y]
                        if r + g + b < 260:
                            hits += 1
            row += '#' if hits >= 3 else '.'
        rows.append(row)
    with open(out, 'w') as f:
        f.write('VGA text grid (80x25, # = ink):\n')
        for i, r in enumerate(rows):
            f.write('%2d |%s|\n' % (i, r))
    print('screen grid written to', out)
    print(open('/tmp/ocbl_snap_serial.log').read())

if __name__ == '__main__':
    main()
