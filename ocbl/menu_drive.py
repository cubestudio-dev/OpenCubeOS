#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - 3.0.0-WP05-ocbl
# File: ocbl/menu_drive.py
# Purpose: drive the B3 menu in QEMU through the monitor `sendkey`
#          (real PS/2 path) and capture the serial transcript.
#          Brings-up helper - not part of any acceptance gate.
#
# Usage: menu_drive.py <scenario> <iso> <serial_out> [disk.img ...]
#   scenario: timeout | downret | shutdown
import subprocess
import sys
import time
import os
import socket

BIN = "/home/z/opt/extract/usr/bin"


def main():
    scenario = sys.argv[1]
    iso = sys.argv[2]
    serial_out = sys.argv[3]
    disks = sys.argv[4:]
    sock = '/tmp/ocbl_menu.sock'
    for p in (sock, serial_out):
        if os.path.exists(p):
            os.unlink(p)
    cmd = [BIN + '/qemu-system-x86_64']
    # the toolchain data dirs are always needed (VGA BIOS etc.)
    cmd += ['-L', '/home/z/opt/extract/share/qemu',
            '-L', '/home/z/opt/extract/share/seabios']
    if os.environ.get('OCBL_OVMF'):
        # the UEFI flavor of the SAME dual-firmware ISO (B5-4): OVMF
        # routes ConIn from the PS/2 keyboard, ConOut to the serial
        cmd += ['-bios', '/home/z/opt/extract/share/ovmf/OVMF.fd']
    cmd += [
        '-m', '256M', '-boot', 'd', '-no-reboot',
        '-display', 'none',
        '-serial', f'file:{serial_out}',
        '-monitor', f'unix:{sock},server,nowait', '-vga', 'std',
        '-cdrom', iso,
    ]
    ide_i = 0
    for d in disks:
        if d.endswith('.iso'):
            cmd += ['-drive', f'file={d},if=ide,index=3,media=cdrom,'
                    'format=raw']
        elif ide_i < 2:
            cmd += ['-drive', f'file={d},if=ide,index={ide_i},'
                    'media=disk,format=raw']
            ide_i += 1
        else:
            cmd += ['-drive', f'file={d},if=virtio,format=raw']
    p = subprocess.Popen(cmd, stdout=subprocess.DEVNULL,
                         stderr=subprocess.DEVNULL)
    time.sleep(2.5)                     # scan + menu render
    s = socket.socket(socket.AF_UNIX)
    s.connect(sock)
    time.sleep(0.2)

    def mcmd(c, wait=0.5):
        s.sendall((c + '\n').encode())
        time.sleep(wait)

    def key(k, wait=0.4):
        mcmd('sendkey ' + k, wait)

    if scenario == 'timeout':
        time.sleep(12)                  # let the 10s timer fire
    elif scenario == 'downret':
        key('down')                     # move to entry 1
        time.sleep(0.5)
        key('ret')
    elif scenario == 'shutdown':
        key('down')                     # hd0-only fleet: row 1 = Shutdown
        time.sleep(0.5)
        key('ret')
    time.sleep(4)
    mcmd('quit', 0.3)
    try:
        p.wait(timeout=8)
    except Exception:
        p.kill()
    print(open(serial_out).read()[-1400:])


if __name__ == '__main__':
    main()
