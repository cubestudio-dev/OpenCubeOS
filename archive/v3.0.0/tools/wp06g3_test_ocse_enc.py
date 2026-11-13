#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 */
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - tools/wp06g3_test_ocse_enc.py (3.0.0-WP06g3-ocse_enc_fix)
# Tasks 4.2+4.8 verification on OCSE (OCSECL):
#   1. serial_encoding            -> "serial encoding: utf8" (default)
#   2. lang zh (utf8)             -> serial carries UTF-8 Chinese
#   3. screendump                 -> SCREEN shows proper hanzi (4.8 fix)
#   4. serial_encoding gbk        -> "serial encoding: gbk"
#   5. lang en -> lang zh (gbk)   -> serial carries GBK bytes (bf d8 "控")
import os, socket, sys, time
import pexpect

BUILD = "/home/z/oc-os/build"
EV    = "/home/z/oc-os/evidence/3_0_0_wp06g3_ocse_enc_fix"
QEMU  = "/home/z/tools/usr/bin/qemu-system-x86_64"
QL    = "/home/z/tools/usr/share/qemu"
SEA   = "/home/z/tools/usr/share/seabios"
DISK  = f"{EV}/ocse-test-disk64.raw"

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

def key(mon, s, settle=0.12):
    for ch in s:
        if ch == " ":
            k = "spc"
        elif ch == "_":
            k = "shift-minus"   # QEMU sendkey has no underscore key name
        else:
            k = ch
        mon.cmd("sendkey " + k, settle)

mon_path = f"{EV}/ocse-enc-mon.sock"
if os.path.exists(mon_path):
    os.unlink(mon_path)

cmd = (f"{QEMU} -L {QL} -L {SEA} -m 256M "
       f"-drive file={DISK},format=raw,if=none,id=sata0 "
       f"-device ahci,id=ahci0 -device ide-hd,drive=sata0,bus=ahci0.0 "
       f"-cdrom {BUILD}/ocse.iso -boot d "
       f"-no-reboot -display none -serial stdio "
       f"-monitor unix:{mon_path},server,nowait -vga std")

log = open(f"{EV}/fix42_ocse_serial_encoding.bin", "wb")
child = pexpect.spawn(cmd, timeout=180)
child.logfile = log

def dump(name):
    try:
        m.cmd("screendump " + EV + "/" + name, 1.0)
    except Exception as e:
        print("screendump fail:", e)

try:
    child.expect("environment up", timeout=120)
    print("[1] OCSE boot: OK")
    m = Mon(mon_path)
    time.sleep(1)
    m.cmd("sendkey ctrl-alt-f2")
    child.expect("OCSECL", timeout=30)
    print("[2] OCSECL: OK")

    key(m, "serial_encoding"); m.cmd("sendkey ret", 1.0)
    child.expect("serial encoding: utf8", timeout=20)
    print("[3] default utf8: OK")

    # zh under utf8: serial must carry UTF-8 (e6 8e a7 "控")
    key(m, "lang zh"); m.cmd("sendkey ret", 1.0)
    child.expect("ocsecl>", timeout=10)
    time.sleep(0.5)
    key(m, "serial_encoding"); m.cmd("sendkey ret", 1.0)
    child.expect("ocsecl>", timeout=10)
    m.cmd("screendump " + EV + "/fix48_ocse_cli_zh_utf8.ppm", 1.0)
    print("[4] lang zh (utf8) sent, screen dumped")

    # switch to gbk
    key(m, "serial_encoding gbk"); m.cmd("sendkey ret", 1.0)
    child.expect("serial encoding: gbk", timeout=10)
    child.expect("ocsecl>", timeout=10)          # consume the leftover prompt
    print("[5] switch gbk: OK")

    # zh again under gbk: serial must carry GBK bytes
    key(m, "lang en"); m.cmd("sendkey ret", 1.5)
    child.expect("ocsecl>", timeout=15)          # lang en ack landed
    key(m, "lang zh"); m.cmd("sendkey ret", 1.5)
    child.expect("ocsecl>", timeout=15)          # lang zh ack landed
    time.sleep(0.5)
    m.cmd("screendump " + EV + "/fix48_ocse_cli_zh_gbk.ppm", 1.0)
    print("[6] lang zh (gbk) sent, screen dumped")

    child.terminate(force=True)
except Exception as e:
    print(f"FAILED: {e}")
    child.terminate(force=True)
finally:
    try:
        child.close(force=True)
    except Exception:
        pass
    log.close()

# byte-level checks on the closed log - the lang ack line is
# "语言：..." so the probe character is 语 (UTF-8 e8 af ad / GBK d3 ef)
raw = open(f"{EV}/fix42_ocse_serial_encoding.bin", "rb").read()
i_gbk = raw.find(b"serial encoding: gbk")
seg = raw[i_gbk:] if i_gbk >= 0 else b""
utf8_yu_after = b"\xe8\xaf\xad" in seg
gbk_yu_after  = b"\xd3\xef" in seg
print(f"[7] after gbk switch: utf8-yu={utf8_yu_after} gbk-yu={gbk_yu_after}")
if gbk_yu_after and not utf8_yu_after:
    print("[7] OCSE GBK serial conversion: OK")
else:
    print("[7] OCSE GBK serial conversion: FAIL")
import sys as _sys; _sys.exit(1)  # BUG #65: propagate failure

# before the gbk marker (utf8 phase), zh output must be UTF-8
seg0 = raw[:i_gbk] if i_gbk >= 0 else raw
utf8_yu_before = b"\xe8\xaf\xad" in seg0
print(f"[8] utf8 phase carried utf8-yu={utf8_yu_before}")
if utf8_yu_before:
    print("[8] OCSE utf8 serial path: OK")
else:
    print("[8] OCSE utf8 serial path: FAIL")
import sys as _sys; _sys.exit(1)  # BUG #65: propagate failure
