#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""Run QEMU with VNC display, capture screenshot via monitor."""
import subprocess, time, os, sys, socket

def main():
    iso = sys.argv[1]
    out_png = sys.argv[2]
    use_uefi = (len(sys.argv) > 3 and sys.argv[3] == 'uefi')

    qmon = '/tmp/qmon-' + str(os.getpid())
    try: os.unlink(qmon)
    except FileNotFoundError: pass

    # Live-caught on the 3.0.0-WP-boot-stack release shots: the old
    # defaults ('/usr/share/seabios', '/usr/share/OVMF/...') do not exist
    # on relocated toolchains (OC_TOOLS), QEMU then dies at startup
    # ("could not load PC BIOS"), the monitor socket lingers without a
    # listener and the tool surfaces it as ENOTCONN/ECONNREFUSED far
    # from the real cause. Derive every firmware path from OC_TOOLS the
    # way tools/test_strict.sh and tools/qemu_runner.py do; the plain
    # env overrides (SEABIOS / OVMF_CODE / OVMF_VARS) still win when set.
    oc_tools = os.environ.get('OC_TOOLS', '/usr')
    seabios = os.environ.get('SEABIOS', oc_tools + '/usr/share/seabios')
    ovmf_code = os.environ.get('OVMF_CODE', oc_tools + '/usr/share/OVMF/OVMF_CODE_4M.fd')
    ovmf_vars = os.environ.get('OVMF_VARS', oc_tools + '/usr/share/OVMF/OVMF_VARS_4M.fd')

    args = [
        'qemu-system-x86_64',
        '-L', oc_tools + '/usr/share/qemu',
        '-L', seabios,
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
            '-drive', 'if=pflash,format=raw,unit=0,file=' + ovmf_code + ',readonly=on',
            '-drive', 'if=pflash,format=raw,unit=1,file=' + ovmf_vars + ',readonly=on',
        ]

    qemu = subprocess.Popen(args, stdin=subprocess.DEVNULL,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


    # Wait for boot, then talk to the monitor with FRESH-socket retries
    # (the timeout-based monitor_screendump() pattern: banner swallowed,
    # send, read to the (qemu) prompt; a failed connect loop can no
    # longer leave a dead socket object behind - the original tool's
    # mon-not-None check passed on an unconnected socket and surfaced
    # as ENOTCONN at sendall, masking the dead-QEMU root cause above).
    time.sleep(12 if not use_uefi else 16)
    ppm_path = out_png + '.ppm'

    sent = False
    for _ in range(10):
        try:
            mon = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            mon.settimeout(10)
            mon.connect(qmon)
        except Exception:
            time.sleep(0.5)
            continue
        try:
            try:
                mon.recv(4096)      # drain the banner (may fail; swallowed)
            except Exception:
                pass
            mon.sendall(b'screendump ' + ppm_path.encode() + b'\n')
            sent = True
            buf = b""
            t0 = time.time()
            while time.time() - t0 < 10:
                try:
                    d = mon.recv(4096)
                    if not d:
                        break
                    buf += d
                    if b"(qemu)" in buf:
                        break
                except Exception:
                    break
        except Exception:
            time.sleep(0.5)
            continue
        finally:
            try:
                mon.close()
            except Exception:
                pass
        if sent:
            break
        time.sleep(0.5)
    if not sent:
        print("FAILED to talk to the monitor")
        qemu.kill()
        return 1

    # QEMU >= 9 writes the PPM asynchronously AFTER the HMP prompt
    # returns; wait for the file to appear and settle (same fix as
    # qemu_runner.monitor_screendump).
    deadline = time.time() + 5
    last_size = -1
    while time.time() < deadline:
        try:
            size = os.path.getsize(ppm_path)
        except OSError:
            size = -1
        if size > 0 and size == last_size:
            break
        last_size = size
        time.sleep(0.1)

    # Quit
    try:
        qemu.terminate()
    except: pass
    try:
        qemu.wait(timeout=3)
    except:
        qemu.kill()
        try: qemu.wait(timeout=3)
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
