#!/usr/bin/env python3
"""Run QEMU with VNC display, capture screenshot via monitor."""
import subprocess, time, os, sys, socket

def main():
    iso = sys.argv[1]
    out_png = sys.argv[2]
    use_uefi = (len(sys.argv) > 3 and sys.argv[3] == 'uefi')

    qmon = '/tmp/qmon-' + str(os.getpid())
    try: os.unlink(qmon)
    except FileNotFoundError: pass

    args = [
        'qemu-system-x86_64',
        '-L', os.environ.get('OC_TOOLS', '/usr')+'/usr/share/qemu',
        '-L', os.environ.get('SEABIOS', '/usr/share/seabios'),
        '-m', '256M',
        '-cdrom', iso,
        '-no-reboot', '-no-shutdown',
        '-display', 'vnc=127.0.0.1:0',
        '-serial', 'file:/tmp/serial.log',
        '-monitor', 'unix:'+qmon+',server,nowait',
        '-vga', 'std',
        '-device', 'isa-debug-exit,iobase=0xf4,iosize=0x04',
        '-snapshot',
    ]
    if use_uefi:
        args += [
            '-drive', 'if=pflash,format=raw,unit=0,file='+os.environ.get('OVMF_CODE', '/usr/share/OVMF/OVMF_CODE_4M.fd')+',readonly=on',
            '-drive', 'if=pflash,format=raw,unit=1,file='+os.environ.get('OVMF_VARS', '/usr/share/OVMF/OVMF_VARS_4M.fd')+',readonly=on',
        ]

    qemu = subprocess.Popen(args, stdin=subprocess.DEVNULL,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    # Connect to monitor
    mon = None
    for _ in range(50):
        try:
            mon = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            mon.connect(qmon)
            break
        except Exception:
            time.sleep(0.1)
    if mon is None:
        print("FAILED to connect to monitor")
        qemu.kill()
        return 1

    # Wait for boot
    time.sleep(12 if not use_uefi else 16)

    # Drain
    mon.setblocking(False)
    try:
        while True:
            try:
                d = mon.recv(4096)
                if not d: break
            except (BlockingIOError, OSError):
                break
    except: pass

    # screendump to PPM
    mon.setblocking(True)
    ppm_path = out_png + '.ppm'
    mon.sendall(b'screendump ' + ppm_path.encode() + b'\n')
    time.sleep(0.5)

    # Drain
    mon.setblocking(False)
    try:
        while True:
            try:
                d = mon.recv(4096)
                if not d: break
            except (BlockingIOError, OSError):
                break
    except: pass

    # Quit
    try:
        mon.sendall(b'quit\n')
    except: pass
    mon.close()

    try: qemu.wait(timeout=2)
    except:
        qemu.kill()
        try: qemu.wait(timeout=2)
        except: pass

    if os.path.exists(ppm_path):
        # Convert PPM to PNG using PIL if available, else just leave PPM
        try:
            from PIL import Image
            im = Image.open(ppm_path)
            im.save(out_png)
            os.unlink(ppm_path)
            print(f"shot: {out_png} ({os.path.getsize(out_png)} bytes, {im.size[0]}x{im.size[1]})")
        except ImportError:
            print(f"shot: {ppm_path} (PPM, {os.path.getsize(ppm_path)} bytes)")
        return 0
    else:
        print(f"FAILED: no screenshot")
        return 2

if __name__ == '__main__':
    sys.exit(main())
