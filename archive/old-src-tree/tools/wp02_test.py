#!/usr/bin/env python3
import os
"""Run WP-02 kernel in QEMU, send serial commands, capture screenshot + serial output."""
import subprocess, time, os, sys, socket, re

def drain(sock, timeout=0.5):
    """Drain all available data from a non-blocking socket."""
    sock.setblocking(False)
    data = b''
    end = time.time() + timeout
    while time.time() < end:
        try:
            d = sock.recv(4096)
            if d:
                data += d
                end = time.time() + timeout  # reset timeout on data
            else:
                break
        except (BlockingIOError, OSError):
            time.sleep(0.05)
    return data

def main():
    iso = sys.argv[1]
    out_png = sys.argv[2]
    use_uefi = len(sys.argv) > 3 and sys.argv[3] == 'uefi'

    qmon = f'/tmp/qmon-wp02-{os.getpid()}'
    ser_sock_path = f'/tmp/serial-wp02-{os.getpid()}.sock'
    for p in [qmon, ser_sock_path]:
        try: os.unlink(p)
        except: pass

    args = [
        'qemu-system-x86_64',
        '-L', os.environ.get('OC_TOOLS', '/usr')+'/usr/share/qemu',
        '-L', os.environ.get('SEABIOS', '/usr/share/seabios'),
        '-m', '256M',
        '-cdrom', iso,
        '-boot', 'd',
        '-display', 'vnc=127.0.0.1:0',
        '-serial', 'unix:' + ser_sock_path + ',server,nowait',
        '-monitor', 'unix:' + qmon + ',server,nowait',
        '-vga', 'std',
        '-snapshot',
        '-no-reboot',
    ]
    if use_uefi:
        args += ['-drive', 'if=pflash,format=raw,file=/tmp/OVMF_WORK_WP02.fd']

    qemu = subprocess.Popen(args, stdin=subprocess.DEVNULL,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    # Connect to serial socket
    ser = None
    for _ in range(100):
        try:
            ser = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            ser.connect(ser_sock_path)
            break
        except: time.sleep(0.1)
    if ser is None:
        print("FAILED to connect to serial socket")
        qemu.kill()
        return 1

    # Connect to monitor
    mon = None
    for _ in range(100):
        try:
            mon = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            mon.connect(qmon)
            break
        except: time.sleep(0.1)

    # Wait for kernel to fully boot (GRUB timeout 2s + kernel boot ~3s)
    print("[test] waiting for kernel to boot...")
    time.sleep(10 if not use_uefi else 15)

    # Drain boot log
    boot_log = drain(ser, 1.0)
    print(f"[test] boot log: {len(boot_log)} bytes")

    # Send 'help' command
    print("[test] sending 'help'")
    ser.sendall(b'help\n')
    time.sleep(3)
    help_out = drain(ser, 1.0)

    # Send 'stats' command
    print("[test] sending 'stats'")
    ser.sendall(b'stats\n')
    time.sleep(3)
    stats_out = drain(ser, 1.0)

    # Send 'exc' command (exception self-test)
    print("[test] sending 'exc'")
    ser.sendall(b'exc\n')
    time.sleep(4)
    exc_out = drain(ser, 1.0)

    # Send 'stats' again (should show tick count increased)
    print("[test] sending 'stats' again")
    ser.sendall(b'stats\n')
    time.sleep(3)
    stats2_out = drain(ser, 1.0)

    all_serial = boot_log + help_out + stats_out + exc_out + stats2_out

    # Screenshot
    if mon:
        mon.setblocking(True)
        ppm = out_png + '.ppm'
        mon.sendall(b'screendump ' + ppm.encode() + b'\n')
        time.sleep(0.5)
        drain(mon, 0.3)
        try: mon.sendall(b'quit\n')
        except: pass
        mon.close()

    try: qemu.wait(timeout=2)
    except:
        qemu.kill()
        try: qemu.wait(timeout=2)
        except: pass
    ser.close()

    # Convert PPM to PNG
    if os.path.exists(out_png + '.ppm'):
        from PIL import Image
        im = Image.open(out_png + '.ppm')
        im.save(out_png)
        os.unlink(out_png + '.ppm')
        print(f"shot: {out_png} ({os.path.getsize(out_png)} bytes, {im.size[0]}x{im.size[1]})")

    # Print serial output
    print("\n=== Full serial output ===")
    text = all_serial.decode('latin-1', errors='replace')
    text = re.sub(r'\x1b\[[0-9;]*[a-zA-Z]', '', text)
    text = text.replace('\r', '')
    lines = [l for l in text.split('\n') if l.strip()]
    for line in lines[-80:]:
        print(line)

    return 0

if __name__ == '__main__':
    sys.exit(main())
