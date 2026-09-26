#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""Run QEMU, wait, send screendump, kill QEMU, return.

Usage: qemu_shot.py <iso> <out.ppm> <serial.log> [uefi]
The 5th arg "uefi" switches to UEFI/OVMF mode.
"""
import subprocess, time, os, sys, socket, signal

def main():
    iso = sys.argv[1]
    out_ppm = sys.argv[2]
    serial_log = sys.argv[3]
    use_uefi = (len(sys.argv) > 4 and sys.argv[4] == 'uefi')

    qmon_path = '/tmp/qmon-' + str(os.getpid())
    try: os.unlink(qmon_path)
    except FileNotFoundError: pass

    qemu_args = [
        'qemu-system-x86_64',
        '-L', os.environ.get('OC_TOOLS', '/usr')+'/usr/share/qemu',
        '-L', os.environ.get('SEABIOS', '/usr/share/seabios'),
        '-m', '256M',
        '-cdrom', iso,
        '-no-reboot', '-no-shutdown',
        '-display', 'none',
        '-serial', 'file:'+serial_log,
        '-monitor', 'unix:'+qmon_path+',server,nowait',
        '-vga', 'std',
        '-snapshot',
    ]
    if use_uefi:
        qemu_args += [
            '-drive', 'if=pflash,format=raw,unit=0,file='+os.environ.get('OVMF_CODE', '/usr/share/OVMF/OVMF_CODE_4M.fd')+',readonly=on',
            '-drive', 'if=pflash,format=raw,unit=1,file='+os.environ.get('OVMF_VARS', '/usr/share/OVMF/OVMF_VARS_4M.fd')+',readonly=on',
        ]

    qemu = subprocess.Popen(qemu_args, stdin=subprocess.DEVNULL,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    # Connect to monitor socket
    mon = None
    for _ in range(50):
        try:
            mon = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            mon.connect(qmon_path)
            break
        except Exception:
            time.sleep(0.1)

    if mon is None:
        print("FAILED to connect to monitor")
        qemu.kill()
        return 1

    # Wait for boot to complete
    time.sleep(5 if not use_uefi else 8)

    # Drain any pending monitor output
    mon.setblocking(False)
    try:
        while True:
            data = mon.recv(4096)
            if not data: break
    except BlockingIOError:
        pass

    # Send screendump
    mon.setblocking(True)
    mon.sendall(b'screendump ' + out_ppm.encode() + b'\n')
    time.sleep(0.5)

    # Drain
    mon.setblocking(False)
    try:
        while True:
            data = mon.recv(4096)
            if not data: break
    except BlockingIOError:
        pass

    # Send quit (best effort)
    try:
        mon.sendall(b'quit\n')
    except Exception:
        pass
    mon.close()

    # Give QEMU 2 seconds to exit cleanly, else kill
    try:
        qemu.wait(timeout=2)
    except subprocess.TimeoutExpired:
        qemu.kill()
        try:
            qemu.wait(timeout=2)
        except subprocess.TimeoutExpired:
            pass

    if os.path.exists(out_ppm):
        sz = os.path.getsize(out_ppm)
        with open(out_ppm, 'rb') as f:
            hdr = f.read(20)
        print(f"shot: {out_ppm} ({sz} bytes) hdr={hdr!r}")
        return 0
    else:
        print(f"FAILED: no screenshot at {out_ppm}")
        return 2

if __name__ == '__main__':
    sys.exit(main())
