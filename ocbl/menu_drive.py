#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - 3.0.0-WP05-ocbl
# File: ocbl/menu_drive.py
# Purpose: drive the OCBL menu in QEMU: monitor `sendkey` (the PS/2
#          path BOTH flavors consume - proven by the B3/B5-4 evidence)
#          with a MENU-READY poll on the serial transcript (B5-5
#          live-caught: a fixed sleep sends the keys while OVMF BDS is
#          still probing the fleet disks and the pick silently falls
#          back to the default).  The serial line rides a tcp socket
#          so the transcript streams live for the poll.
#
#          (Second live-caught: injecting CSI bytes through the tcp
#          socket itself does NOT work - the bytes reach the 16550
#          far too late, only to be echoed by the started kernel's
#          shell.  PS/2 sendkey stays THE injection path.)
#
# Usage: menu_drive.py <scenario> <iso> <serial_out> [disk.img ...]
#   scenario: timeout | down1 | down2 | downN | downret (=down1)
#             | shutdown | ocos (the default entry, Enter)
#   Env OCBL_OVMF=1 boots the UEFI flavor of the dual-firmware ISO.
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

    # serial on a TCP server socket: bidirectional (transcript OUT,
    # keys IN) - the file: backend only captures, it cannot inject.
    cmd = [BIN + '/qemu-system-x86_64']
    cmd += ['-L', '/home/z/opt/extract/share/qemu',
            '-L', '/home/z/opt/extract/share/seabios']
    if os.environ.get('OCBL_OVMF'):
        cmd += ['-bios', '/home/z/opt/extract/share/ovmf/OVMF.fd']
    cmd += [
        '-m', '256M', '-boot', 'd', '-no-reboot',
        '-display', 'none',
        '-serial', f'file:{serial_out}',
        '-qmp', f'unix:{sock},server,nowait', '-vga', 'std',
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
                         stderr=None)

    buf = b''

    def pump():
        nonlocal buf
        try:
            buf = open(serial_out, 'rb').read()
        except Exception:
            pass

    def wait_for(needle, timeout):
        end = time.time() + timeout
        while time.time() < end:
            pump()
            if needle in buf:
                return True
            time.sleep(0.3)
        return False

    # QMP socket for send-key (the PS/2 injection path)
    m = None
    end = time.time() + 30
    while time.time() < end and m is None:
        try:
            c = socket.socket(socket.AF_UNIX)
            c.connect(sock)
            m = c                        # only a CONNECTED socket qualifies
        except Exception:
            time.sleep(0.4)
    if m is None:
        print("FATAL: qmp socket never came up")
        p.kill()
        sys.exit(1)

    # QMP handshake: read the greeting, say qmp_capabilities
    m.setblocking(False)
    greet_end = time.time() + 5
    greet = b''
    while time.time() < greet_end:
        try:
            d = m.recv(4096)
            if not d:
                break
            greet += d
            if b'"return"' in greet:
                break
        except BlockingIOError:
            pass
        except Exception:
            break
        time.sleep(0.2)
    try:
        m.sendall(b'{"execute":"qmp_capabilities"}\n')
    except Exception as e:
        sys.stderr.write(f"[dbg] qmp handshake failed: {e}\n")
        sys.stderr.flush()
    time.sleep(0.3)

    def key(k, wait=0.4):
        try:
            q = ('{"execute":"send-key","arguments":{"keys":'
                 '[{"type":"qcode","data":"%s"}]}}\n' % k).encode()
            m.sendall(q)
            m.recv(4096)
        except Exception as e:
            sys.stderr.write(f"[dbg] sendkey {k} failed: {e}\n")
            sys.stderr.flush()
        time.sleep(wait)

    ok = wait_for(b'Up/Down to select', 150)
    tail = 8
    if scenario == 'timeout':
        time.sleep(13)                  # let the 10s timer fire
        tail = 90                       # B5-5: 2 MiB kernel + boot
    elif scenario in ('ocos', 'default'):
        key('ret')                      # boot the default entry now
        tail = 40                        # 2 MiB payloads take a while
    elif scenario in ('downret', 'down1'):
        key('down')                     # move to entry 1
        key('ret')
        tail = 30                        # OCIRE: 60 KiB payload
    elif scenario.startswith('down'):   # down2..downN: B5-6 chain rows
        n = int(scenario[4:])
        for _ in range(n):
            key('down', wait=0.4)
        key('ret')
        tail = 30
    elif scenario == 'shutdown':
        key('down')                     # hd0-only fleet: row 1 = Shutdown
        key('ret')
        tail = 10

    end = time.time() + tail
    while time.time() < end:
        pump()
        time.sleep(0.5)
    
    # hard stop: the handoff'ed kernel would run forever
    try:
        m.sendall(b'{"execute":"quit"}\n')
        time.sleep(0.5)
        m.close()
    except Exception:
        pass
    try:
        p.wait(timeout=8)
    except Exception:
        p.kill()
    pump()
    txt = buf.decode(errors='replace')
    print(txt[-2000:])


if __name__ == '__main__':
    main()
