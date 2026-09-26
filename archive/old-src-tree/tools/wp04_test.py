#!/usr/bin/env python3
import subprocess, time, os, sys, socket, re

def drain(sock, timeout=0.5):
    sock.setblocking(False)
    data = b''
    end = time.time() + timeout
    while time.time() < end:
        try:
            d = sock.recv(4096)
            if d:
                data += d
                end = time.time() + timeout
            else:
                break
        except (BlockingIOError, OSError):
            time.sleep(0.05)
    return data

def main():
    iso = sys.argv[1]
    out_png = sys.argv[2]
    use_uefi = len(sys.argv) > 3 and sys.argv[3] == 'uefi'

    qmon = f'/tmp/qmon-wp04-{os.getpid()}'
    ser_sock = f'/tmp/serial-wp04-{os.getpid()}.sock'
    for p in [qmon, ser_sock]:
        try: os.unlink(p)
        except: pass

    args = [
        'qemu-system-x86_64',
        '-L', os.environ.get('OC_TOOLS', '/usr')+'/usr/share/qemu',
        '-L', os.environ.get('SEABIOS', '/usr/share/seabios'),
        '-m', '256M', '-cdrom', iso, '-boot', 'd',
        '-display', 'vnc=127.0.0.1:0',
        '-serial', 'unix:'+ser_sock+',server,nowait',
        '-monitor', 'unix:'+qmon+',server,nowait',
        '-vga', 'std', '-snapshot', '-no-reboot',
    ]
    if use_uefi:
        args += ['-drive', 'if=pflash,format=raw,file=/tmp/OVMF_WORK_WP04.fd']

    qemu = subprocess.Popen(args, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    ser = None
    for _ in range(100):
        try:
            ser = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            ser.connect(ser_sock)
            break
        except: time.sleep(0.1)
    if ser is None:
        print("FAILED to connect to serial"); qemu.kill(); return 1
    mon = None
    for _ in range(100):
        try:
            mon = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            mon.connect(qmon)
            break
        except: time.sleep(0.1)

    time.sleep(10 if not use_uefi else 15)
    all_serial = drain(ser, 1.0)

    cmds = [
        (b'sched\n', 3),
        (b'ps\n', 3),
        (b'spawn\n', 4),
        (b'ps\n', 3),
        (b'sched\n', 3),
        (b'pftest\n', 5),
        (b'cr3test\n', 5),
        (b'heaptest\n', 5),
        (b'syncstat\n', 3),
        (b'run hello\n', 5),
        (b'ps\n', 3),
    ]
    for cmd, wait in cmds:
        ser.sendall(cmd)
        time.sleep(wait)
        all_serial += drain(ser, 1.0)

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

    if os.path.exists(out_png + '.ppm'):
        from PIL import Image
        im = Image.open(out_png + '.ppm')
        im.save(out_png)
        os.unlink(out_png + '.ppm')
        print(f"shot: {out_png} ({os.path.getsize(out_png)} bytes, {im.size[0]}x{im.size[1]})")

    print("\n=== Serial output (last 80 lines) ===")
    text = all_serial.decode('latin-1', errors='replace')
    text = re.sub(r'\x1b\[[0-9;]*[a-zA-Z]', '', text)
    text = text.replace('\r', '')
    lines = [l for l in text.split('\n') if l.strip()]
    for line in lines[-80:]:
        print(line)
    return 0

if __name__ == '__main__':
    sys.exit(main())
