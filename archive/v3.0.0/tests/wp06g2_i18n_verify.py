#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 -*-
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - tests/wp06g2_i18n_verify.py (3.0.0-WP06g2-l1_i18n)
# Framework acceptance battery (i18n plumbing, before the bulk
# translation pass):
#   T1  boot -> `lang` reports English ("Console output language: en")
#   T2  `lang zh` -> the REAL Chinese line (the historical English
#       placeholder is gone): "控制台输出语言已设为 zh" + persistence note
#   T3  `lang` again -> "控制台输出语言：zh"
#   T4  `help -a` -> the CHINESE registry header ("ring-3 程序注册表")
#       and the CHINESE footer ("个已注册程序") from the L0 i18n resource
#   T5  `help -w` -> the Chinese group descriptions render (the
#       wp_* keys decode, e.g. "内核核心" for the core group)
#   T6  serial stream stays strictly valid UTF-8 through the Chinese
#       outputs (terminal-side decode of everything the run echoed)
#   T7  `lang en` back to English, `lang` reports en again
import os
import socket
import sys
import time
import pexpect

BUILD = "/home/z/oc-os/build"
OUT = f"{BUILD}/wp06g2-i18n-verify"
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

print("[i18n-verify] boot:", CMD)
child = pexpect.spawn("/bin/bash", ["-c", CMD], timeout=180)
child.logfile_read = open(f"{OUT}/pty-all.log", "wb")


class Mon:
    def __init__(self, path, wait=30):
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
        raise RuntimeError("monitor socket never appeared")

    def cmd(self, c, timeout=10):
        self.s.sendall((c + "\n").encode())
        time.sleep(0.4)
        try:
            return self.s.recv(65536).decode(errors="replace")
        except socket.timeout:
            return ""


mon = Mon(MON)
fails = 0


def check(name, cond, detail=""):
    global fails
    tag = "PASS" if cond else "FAIL"
    print(f"[{tag}] {name}" + (f"  ({detail})" if detail and not cond else ""))
    if not cond:
        fails += 1


def run(cmd, expect, timeout=60):
    """Send a shell line, wait for the prompt back."""
    child.sendline(cmd)
    try:
        child.expect_exact(expect, timeout=timeout)
        return True
    except pexpect.TIMEOUT:
        return False


# ---- boot to the ush prompt ------------------------------------------
print("[i18n-verify] waiting for the ush prompt...")
ok = child.expect_exact(["ush>", "login:"], timeout=180)
if ok != 0:
    print("[i18n-verify] FATAL: no ush prompt; log tail:")
    os.system(f"tail -30 {OUT}/pty-all.log")
    child.kill(9)
    sys.exit(1)
check("boot to ush prompt", True)

# ---- T1: lang reports English ----------------------------------------
child.sendline("lang")
i = child.expect(["Console output language: en", "ush>"], timeout=60)
# the reply lands before the next prompt; grab the transcript
time.sleep(0.6)
child.expect_exact("ush>", timeout=60)
log = open(f"{OUT}/pty-all.log", "rb").read().decode(errors="replace")
t1 = "Console output language: en" in log
check("T1 lang default English", t1)

# ---- T2: lang zh -> real Chinese -------------------------------------
child.sendline("lang zh")
time.sleep(0.8)
child.expect_exact("ush>", timeout=60)
log = open(f"{OUT}/pty-all.log", "rb").read().decode(errors="replace")
t2 = "控制台输出语言已设为 zh" in log
check("T2 lang zh real Chinese (placeholder gone)", t2,
      log[-400:] if not t2 else "")

# ---- T3: lang reports Chinese now ------------------------------------
child.sendline("lang")
time.sleep(0.8)
child.expect_exact("ush>", timeout=60)
log = open(f"{OUT}/pty-all.log", "rb").read().decode(errors="replace")
t3 = "控制台输出语言：zh" in log
check("T3 lang status Chinese", t3)

# ---- T4: help -a Chinese header; help core-mode Chinese footer --------
child.sendline("help -a")
time.sleep(1.2)
child.expect_exact("ush>", timeout=120)
log = open(f"{OUT}/pty-all.log", "rb").read().decode(errors="replace")
t4a = "ring-3 程序注册表（A-Z" in log
# the FULL A-Z list must be present (the 4096-era truncation is gone):
# the last alphabetical entry (write) and the head must both render
t4a_full = "  write  [block]" in log and "  ab_partition_test" in log
check("T4a help -a Chinese registry header", t4a)
check("T4a2 help -a full A-Z list (no truncation: ab_partition_test .. write)", t4a_full)

child.sendline("help")
time.sleep(1.5)
child.expect_exact("ush>", timeout=120)
log = open(f"{OUT}/pty-all.log", "rb").read().decode(errors="replace")
# the ush built-in help now renders through its own i18n resource:
# the Chinese title, a translated built-in row, and the Chinese
# registry count line are all user-visible surfaces
t4b = "个程序" in log and "切换目录" in log and "内建命令" in log
check("T4b ush help Chinese (title + rows + registry count)", t4b)

# ---- T5: help -w Chinese group descriptions ---------------------------
child.sendline("help -w")
time.sleep(1.2)
child.expect_exact("ush>", timeout=120)
log = open(f"{OUT}/pty-all.log", "rb").read().decode(errors="replace")
t5a = "内核核心：内存、调度器、诊断" in log
t5b = "显示服务：帧缓冲、字体、中文渲染" in log
check("T5a help -w Chinese core-group description", t5a)
check("T5b help -w Chinese display-group description", t5b)

# ---- T6: the whole serial stream is strictly valid UTF-8 --------------
raw = open(f"{OUT}/pty-all.log", "rb").read()
try:
    raw.decode("utf-8", errors="strict")
    t6 = True
    why = ""
except UnicodeDecodeError as e:
    t6 = False
    why = str(e)
check("T6 serial stream strictly valid UTF-8", t6, why)

# ---- T7: back to English ----------------------------------------------
child.sendline("lang en")
time.sleep(0.8)
child.expect_exact("ush>", timeout=60)
child.sendline("lang")
time.sleep(0.8)
child.expect_exact("ush>", timeout=60)
log = open(f"{OUT}/pty-all.log", "rb").read().decode(errors="replace")
# the LAST occurrence must be the English status again
tail = log[-600:]
t7 = "Console output language: en" in tail
check("T7 lang en restores English", t7)

print()
if fails == 0:
    print("wp06g2_i18n_verify: ALL PASS (7/7)")
    child.kill(9)
    sys.exit(0)
print(f"wp06g2_i18n_verify: {fails} FAILURES")
child.kill(9)
sys.exit(1)
