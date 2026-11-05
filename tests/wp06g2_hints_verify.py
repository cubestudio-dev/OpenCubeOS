#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 -*-
# Copyright 2026 cubestudio-dev <cubestudio@qq.com> -*-
# Open Cube OS - tests/wp06g2_hints_verify.py (3.0.0-WP06g2-l1_i18n)
# The per-program localization battery:
#   H1  under `lang zh`, help -a renders a CHINESE hint for every one
#       of the 274 registry entries (byte-scan for the count of lines
#       whose hint field starts with a 3-byte UTF-8 CJK lead)
#   H2  spot pairs the user-named commands (help ls cat cp mv rm
#       mkdir ping wget nano vi tui) carry a non-empty Chinese hint
#   H3  under `lang en`, help -a renders English hints back
#   H4  the serial stream stays strictly valid UTF-8
import os
import socket
import sys
import time
import pexpect

BUILD = "/home/z/oc-os/build"
OUT = f"{BUILD}/wp06g2-hints-verify"
MON = f"{OUT}/mon.sock"
os.makedirs(OUT, exist_ok=True)
for p in (MON,):
    if os.path.exists(p):
        os.unlink(p)

QEMU = "qemu-system-x86_64"
CMD = (f"{QEMU} -L /home/z/opt/extract/usr/share/qemu "
       f"-L /home/z/opt/extract/usr/share/seabios -m 256M "
       f"-cdrom {BUILD}/opencube.iso -boot d "
       f"-no-reboot -display none "
       f"-serial stdio "
       f"-monitor unix:{MON},server,nowait -vga std")

print("[hints-verify] boot:", CMD)
child = pexpect.spawn("/bin/bash", ["-c", CMD], timeout=180)
child.logfile_read = open(f"{OUT}/pty-all.log", "wb")

fails = 0


def check(name, cond, detail=""):
    global fails
    tag = "PASS" if cond else "FAIL"
    print(f"[{tag}] {name}" + (f"  ({detail})" if detail and not cond else ""))
    if not cond:
        fails += 1


def pump(seconds=0.3):
    try:
        child.expect_exact("@@NEVER@@", timeout=seconds)
    except pexpect.TIMEOUT:
        pass
    except pexpect.EOF:
        pass


def rawlog():
    child.logfile_read.flush()
    return open(f"{OUT}/pty-all.log", "rb").read()


print("[hints-verify] waiting for the ush prompt...")
if child.expect_exact(["ush>", "login:"], timeout=180) != 0:
    print("[hints-verify] FATAL: no ush prompt")
    child.kill(9)
    sys.exit(1)
check("boot to ush prompt", True)

# ---- H1: zh hints for all 274 entries ----
child.sendline("lang zh")
child.expect_exact("ush>", timeout=60)
child.sendline("help -a")
for _ in range(40):                    # the 274-line listing takes a while
    pump(0.3)
    if rawlog().count(b"ush>") >= 4:
        break
child.expect_exact("ush>", timeout=120)
raw = rawlog()
k = raw.find(b"help -a")
seg = raw[k:] if k >= 0 else b""
# every registry line: "  name  [wp]  <hint>" - count lines whose
# hint begins with a CJK lead byte (0xE4..0xE9)
zh_lines = 0
en_lines = 0
for line in seg.split(b"\r\r\n"):
    if b"  [" not in line or b"]" not in line:
        continue
    try:
        after_wp = line.split(b"]", 1)[1]
    except IndexError:
        continue
    hint = after_wp.strip()
    if not hint:
        continue
    # a hint is Chinese when it CONTAINS a CJK sequence anywhere -
    # many zh hints legitimately start with a Latin term (A/B, AC97,
    # e1000e, PCI...)
    if any(0xE4 <= hint[i] <= 0xE9 for i in range(len(hint))):
        zh_lines += 1
    else:
        en_lines += 1
check("H1 zh: all 274 registry hints render in Chinese",
      zh_lines >= 274 and en_lines == 0,
      f"zh={zh_lines} en={en_lines}")

# ---- H2: the user-named commands spot check ----
log = seg.decode(errors="replace")
spots = {"help": "内建命令", "ls": "列出目录内容", "cat": "打印文件内容",
         "cp": "复制文件", "mv": "移动", "rm": "删除文件",
         "mkdir": "创建目录", "ping": "ICMP", "wget": "下载",
         "nano": "编辑器", "vi": "编辑器", "tui": "桌面"}
bad = [f"{n}:{v}" for n, v in spots.items() if v not in log]
check("H2 user-named commands carry their Chinese hints", not bad,
      "missing: " + " ".join(bad))

# ---- H3: back to English ----
child.sendline("lang en")
child.expect_exact("ush>", timeout=60)
child.sendline("help -a")
for _ in range(40):
    pump(0.3)
    if rawlog().count(b"ush>") >= 6:
        break
child.expect_exact("ush>", timeout=120)
raw2 = rawlog()
k2 = raw2.rfind(b"help -a")
seg2 = raw2[k2:] if k2 >= 0 else b""
check("H3 en: hints render in English again",
      b"list directory contents" in seg2 and
      b"the nano-style editor" in seg2)

# ---- H4: strict UTF-8 ----
try:
    raw2.decode("utf-8", errors="strict")
    t = True
    why = ""
except UnicodeDecodeError as e:
    t = False
    why = str(e)
check("H4 serial stream strictly valid UTF-8", t, why)

print()
if fails == 0:
    print("wp06g2_hints_verify: ALL PASS (4/4)")
    child.kill(9)
    sys.exit(0)
print(f"wp06g2_hints_verify: {fails} FAILURES")
child.kill(9)
sys.exit(1)
