#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - tools/wp06g3_baseline.py (3.0.0-WP06g3-ocse_enc_fix)
# BEFORE-fix evidence capture:
#   OCSE:  boot -> Ctrl+Alt+F2 to CLI -> lang zh -> hwcheck/help
#          serial log + screendump (screen mojibake + serial bytes)
#   OCIRE: boot -> help -> screendump + serial (no Chinese support at all)
import os, socket, sys, time
import pexpect

BUILD = "/home/z/oc-os/build"
EV    = "/home/z/oc-os/evidence/3_0_0_wp06g3_ocse_enc_fix"
QEMU  = "/home/z/opt/extract/usr/bin/qemu-system-x86_64"
QL    = "/home/z/opt/extract/usr/share/qemu"
SEA   = "/home/z/opt/extract/usr/share/seabios"
os.makedirs(EV, exist_ok=True)

DISK = f"{EV}/ocse-test-disk64.raw"
if not os.path.exists(DISK) or os.path.getsize(DISK) != 64 * 1024 * 1024:
    with open(DISK, "wb") as f:
        f.truncate(64 * 1024 * 1024)

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
            while True:
                r = self.s.recv(65536)
                if not r:
                    break
        except socket.timeout:
            pass

def run_ocse():
    mon = f"{EV}/ocse-mon.sock"
    if os.path.exists(mon):
        os.unlink(mon)
    cmd = (f"{QEMU} -L {QL} -L {SEA} -m 256M "
           f"-drive file={DISK},format=raw,if=ide,index=0,media=disk "
           f"-cdrom {BUILD}/ocse.iso -boot d "
           f"-no-reboot -display none -serial stdio "
           f"-monitor unix:{mon},server,nowait -vga std")
    child = pexpect.spawn(cmd, timeout=180, encoding="utf-8",
                          codec_errors="replace")
    log = open(f"{EV}/before_ocse_serial.log", "w")
    child.logfile = log
    try:
        child.expect("environment up: GUI mode", timeout=120)
        m = Mon(mon)
        time.sleep(1)
        m.cmd("sendkey ctrl-alt-f2")
        child.expect("OCSECL", timeout=30)
        time.sleep(0.5)
        m.cmd("sendkey l"); m.cmd("sendkey a"); m.cmd("sendkey n"); m.cmd("sendkey g")
        m.cmd("sendkey spc")
        m.cmd("sendkey z"); m.cmd("sendkey h")
        m.cmd("sendkey ret", 1.0)
        child.expect("lang", timeout=20)
        time.sleep(0.5)
        # hwcheck output (zh)
        for k in "hwcheck":
            m.cmd("sendkey " + k, 0.12)
        m.cmd("sendkey ret", 1.5)
        time.sleep(1)
        m.cmd("screendump " + EV + "/before_ocse_cli_zh.ppm", 1.0)
        # help banner (zh) - also has the em-dash string
        for k in "help":
            m.cmd("sendkey " + k, 0.12)
        m.cmd("sendkey ret", 1.5)
        time.sleep(1)
        m.cmd("screendump " + EV + "/before_ocse_cli_help.ppm", 1.0)
        # GUI panel in zh: ctrl-alt-f1
        m.cmd("sendkey ctrl-alt-f1", 1.0)
        time.sleep(1.5)
        m.cmd("screendump " + EV + "/before_ocse_gui_zh.ppm", 1.0)
        print("[baseline] OCSE captured")
    except Exception as e:
        print(f"[baseline] OCSE FAILED: {e}")
        log.close()
        child.terminate(force=True)
        return 1
    log.close()
    child.terminate(force=True)
    return 0

def run_ocire():
    cmd = (f"{QEMU} -L {QL} -L {SEA} -m 256M "
           f"-cdrom {BUILD}/ocire/ocire.iso -boot d "
           f"-no-reboot -display none -serial stdio -monitor none -vga std")
    child = pexpect.spawn(cmd, timeout=120, encoding="utf-8",
                          codec_errors="replace")
    log = open(f"{EV}/before_ocire_serial.log", "w")
    child.logfile = log
    try:
        child.expect("Type 'help'", timeout=90)
        time.sleep(0.5)
        child.send("help\r")
        child.expect("OCIRE commands", timeout=20)
        time.sleep(1)
        print("[baseline] OCIRE captured (English-only shell)")
    except Exception as e:
        print(f"[baseline] OCIRE FAILED: {e}")
        log.close()
        child.terminate(force=True)
        return 1
    log.close()
    child.terminate(force=True)
    return 0

rc = run_ocse()
rc |= run_ocire()
sys.exit(rc)
