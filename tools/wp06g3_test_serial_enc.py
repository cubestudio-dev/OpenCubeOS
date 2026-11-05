#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 */
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - tools/wp06g3_test_serial_enc.py (3.0.0-WP06g3-ocse_enc_fix)
# Task 4.2 verification: serial_encoding utf8|gbk on the main ISO.
#   1. serial_encoding            -> "serial encoding: utf8" (default)
#   2. serial_encoding gbk        -> "serial encoding: gbk"
#   3. lang zh under gbk          -> the zh ack line arrives as GBK bytes
#      (UTF-8 e6 8e a7 "控" becomes GBK bf d8); captured RAW (latin-1)
#   4. lang zh under utf8         -> legal UTF-8 again after switching back
import sys, time
import pexpect

QEMU = "/home/z/opt/extract/usr/bin/qemu-system-x86_64"
QL   = "/home/z/opt/extract/usr/share/qemu"
SEA  = "/home/z/opt/extract/usr/share/seabios"
ISO  = "/home/z/oc-os/build/opencube.iso"
OUT  = "/home/z/oc-os/evidence/3_0_0_wp06g3_ocse_enc_fix"

cmd = (f"{QEMU} -L {QL} -L {SEA} -m 256M -cdrom {ISO} -boot d "
       f"-no-reboot -display none -serial stdio -vga std")

child = pexpect.spawn(cmd, timeout=120, encoding="latin-1", codec_errors="replace")
child.logfile = open(f"{OUT}/fix42_main_serial_encoding.bin", "w", encoding="latin-1")

def raw_tail(n=400):
    return open(f"{OUT}/fix42_main_serial_encoding.bin", "rb").read()[-n:]

try:
    child.expect("ush>")
    print("[1] boot to ush: OK")

    child.sendline("serial_encoding")
    child.expect("serial encoding: utf8")
    print("[2] default encoding is utf8: OK")

    child.sendline("serial_encoding gbk")
    child.expect("serial encoding: gbk")
    print("[3] switch to gbk: OK")

    # lang zh ack line: zh mode ON -> the ack text is Chinese; the
    # serial bytes must now be GBK ("控" = bf d8), NOT UTF-8 (e6 8e a7)
    child.sendline("lang zh")
    child.expect("ush>")
    time.sleep(0.5)
    child.sendline("serial_encoding")
    child.expect("ush>")
    # dump what the raw stream carried between the two prompts
    tail = raw_tail()
    has_utf8_kong = b"\xe6\x8e\xa7" in tail      # UTF-8 "控"
    has_gbk_kong  = b"\xbf\xd8" in tail          # GBK  "控"
    print(f"[4] after lang zh under gbk: utf8-kong={has_utf8_kong} gbk-kong={has_gbk_kong}")
    if has_gbk_kong and not has_utf8_kong:
        print("[4] GBK conversion on serial: OK")
    else:
        print("[4] GBK conversion on serial: FAIL")

    # back to utf8 + zh: the Chinese must arrive as legal UTF-8 again
    child.sendline("serial_encoding utf8")
    child.expect("serial encoding: utf8")
    child.sendline("lang en")
    child.expect("ush>")
    child.sendline("lang zh")
    child.expect("ush>")

    child.sendline("shutdown")
    child.expect(pexpect.EOF, timeout=30)
except pexpect.TIMEOUT:
    print("TIMEOUT", flush=True)
    print(child.before, flush=True)
finally:
    try:
        child.close(force=True)
    except Exception:
        pass

# [5] runs AFTER the logfile is closed (pexpect buffered writes land
# only on close) - the full-session bytes are final now.
tail = raw_tail(600)
has_utf8_kong = b"\xe6\x8e\xa7" in tail
print(f"[5] back to utf8 (full-session tail): utf8-kong={has_utf8_kong}")
if has_utf8_kong:
    print("[5] utf8 restored: OK")
else:
    print("[5] utf8 restored: FAIL")
