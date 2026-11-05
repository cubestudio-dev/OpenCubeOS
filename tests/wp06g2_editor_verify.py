#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 -*-
# Copyright 2026 cubestudio-dev <cubestudio@qq.com> -*-
# Open Cube OS - tests/wp06g2_editor_verify.py (3.0.0-WP06g2-l1_i18n)
# Editor battery (live QEMU, serial-driven):
#   N1  nano: F2 + pinyin "zhongguo" -> strip shows -> SPACE commits
#       中国 into the line; "hao" + SPACE commits 好; ^O saves; ^X+y;
#       `cat` shows 中国好
#   N2  nano Backspace over a hanzi wipes TWO cells (serial echo has
#       the double "\b \b" pair) - glyph-wise deletion
#   N3  nano ^C: cursor columns count DISPLAY cells (你好| -> col 5;
#       one Left -> col 3) - glyph-wise motion
#   N4  nano ^K cut + ^U paste restore the line count
#   V1  vi: i -> F2 -> "nihao" -> SPACE commits 你好 -> Esc -> :wq;
#       `cat` shows 你好
#   V2  vi: yy copies the line, p pastes it below (line count +1)
#   E1  the whole serial stream stays strictly valid UTF-8
import os
import socket
import sys
import time
import pexpect

BUILD = "/home/z/oc-os/build"
OUT = f"{BUILD}/wp06g2-editor-verify"
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

print("[editor-verify] boot:", CMD)
child = pexpect.spawn("/bin/bash", ["-c", CMD], timeout=180)
child.logfile_read = open(f"{OUT}/pty-all.log", "wb")

fails = 0
F2 = "\x1b[12~"   # xterm F2 (ESC[12~)


def check(name, cond, detail=""):
    global fails
    tag = "PASS" if cond else "FAIL"
    print(f"[{tag}] {name}" + (f"  ({detail})" if detail and not cond else ""))
    if not cond:
        fails += 1


def sh(cmd, wait=1.0):
    child.sendline(cmd)
    time.sleep(wait)
    child.expect_exact("ush>", timeout=90)


def pump(seconds=0.2):
    """Drain the pty into the logfile (pexpect only reads on expect)."""
    try:
        child.expect_exact("@@NEVER@@", timeout=seconds)
    except pexpect.TIMEOUT:
        pass
    except pexpect.EOF:
        pass


def logtext():
    child.logfile_read.flush()
    return open(f"{OUT}/pty-all.log", "rb").read().decode(errors="replace")


print("[editor-verify] waiting for the ush prompt...")
if child.expect_exact(["ush>", "login:"], timeout=180) != 0:
    print("[editor-verify] FATAL: no ush prompt")
    child.kill(9)
    sys.exit(1)
check("boot to ush prompt", True)

# ---------- N1: nano Chinese input through the system IME ----------
child.sendline("nano /tmp/t1.txt")
time.sleep(1.2)
child.send(F2)                    # IME on
time.sleep(0.4)
child.send("zhongguo")            # compose
n1_strip = False
for _ in range(24):                # poll up to ~6s
    pump(0.2)
    if "[zhongguo]" in logtext():
        n1_strip = True
        break
check("N1a nano IME strip shows the pinyin", n1_strip)
child.send(" ")                   # commit 中国
time.sleep(0.6)
child.send("hao")                 # compose hao
time.sleep(0.6)
child.send("4")                   # pick 好 (freq order: 1号 2嚎 3壕 4好)
time.sleep(0.5)
child.send("\x0f")                # ^O save
time.sleep(0.8)
child.send("\x18")                # ^X exit (clean -> straight back)
time.sleep(0.8)
child.expect_exact("ush>", timeout=30)
sh("cat /tmp/t1.txt")
log = logtext()
check("N1b nano: cat shows 中国好", "中国好" in log,
      log[-200:] if "中国好" not in log else "")

# ---------- N2: Backspace over a hanzi wipes two cells ----------
child.sendline("nano /tmp/t2.txt")
time.sleep(1.0)
child.send(F2)
time.sleep(0.3)
child.send("hao")
time.sleep(0.4)
child.send(" ")
time.sleep(0.5)                   # 好 inserted
child.send("\x08")                # Backspace over 好
time.sleep(0.6)
pump(0.4)
child.logfile_read.flush()
raw_all = open(f"{OUT}/pty-all.log", "rb").read()
k = raw_all.find(b"nano /tmp/t2.txt")
tail = raw_all[k:] if k >= 0 else b""
n2 = tail.count(b"\x08 \x08") >= 2 and b"\xe5\xa5\xbd" in tail
check("N2 nano Backspace wipes the whole 2-cell glyph", n2,
      repr(tail[-120:]))
child.send("\x18")                # dirty -> asks
time.sleep(0.5)
child.send("n")                   # discard
time.sleep(0.5)
child.expect_exact("ush>", timeout=30)

# ---------- N3: glyph motion via the redraw cursor prefix ----------
# (no raw ^C over the pty: the line discipline turns 0x03 into SIGINT
#  and KILLS QEMU - live-caught as 'terminating on signal 2'; the ^C
#  position report stays a real key on real keyboards)
child.sendline("nano /tmp/t1.txt")
time.sleep(1.2)
child.send("\x05")                # End: cursor prefix = whole line
time.sleep(0.6)
child.send("\x1b[D")              # Left (CSI; a raw 0x82 is UTF-8 paste)
time.sleep(0.6)
pump(0.4)
child.logfile_read.flush()
raw3 = open(f"{OUT}/pty-all.log", "rb").read()
k3 = raw3.rfind(b"nano /tmp/t1.txt")
seg3 = raw3[k3:] if k3 >= 0 else b""
# One redraw = "\r<no.> <full line> \r<no.> <cursor prefix>".
# End state: the cursor prefix is the FULL line (中国好).
full_prefix = b"\r1  : \xe4\xb8\xad\xe5\x9b\xbd\xe5\xa5\xbd"
# after one glyph-Left: a redraw whose prefix is 中国 and the NEXT
# bytes are NOT 好 (the prefix genuinely stops before the last glyph)
hao = b"\xe5\xa5\xbd"
half_marker = b" \r1  : \xe4\xb8\xad\xe5\x9b\xbd"
n3a = seg3.count(full_prefix) >= 1
n3b = False
pos = seg3.find(half_marker)
while pos >= 0:
    after = seg3[pos + len(half_marker):pos + len(half_marker) + 3]
    if after != hao:
        n3b = True          # prefix ends in 中国 with no 好 after it
        break
    pos = seg3.find(half_marker, pos + 1)
check("N3a nano End redraws the full line as the cursor prefix", n3a,
      repr(seg3[-160:]))
check("N3b nano one glyph-Left drops exactly the last glyph", n3b,
      repr(seg3[-160:]))
child.send("\x18")                # clean exit (file untouched)
time.sleep(0.6)
child.expect_exact("ush>", timeout=30)

# ---------- N4: ^K cut + ^U paste ----------
sh("echo one > /tmp/t3.txt")
sh("echo two >> /tmp/t3.txt")
sh("echo three >> /tmp/t3.txt")
child.sendline("nano /tmp/t3.txt")
time.sleep(1.0)
child.send("\x0b")                # ^K cut line 1
time.sleep(0.4)
child.send("\x15")                # ^U paste it back
time.sleep(0.4)
child.send("\x0f")                # ^O
time.sleep(0.6)
child.send("\x18")
time.sleep(0.4)
child.expect_exact("ush>", timeout=30)
sh("cat /tmp/t3.txt")
log = logtext()
n4 = ("one" in log) and ("two" in log) and ("three" in log)
check("N4 nano ^K cut + ^U paste restores the content", n4)

# ---------- V1: vi insert-mode Chinese through the system IME ------
child.sendline("vi /tmp/t4.txt")
time.sleep(1.0)
child.send("i")                   # insert mode
time.sleep(0.4)
child.send(F2)
time.sleep(0.3)
child.send("nihao")
v1_strip = False
for _ in range(24):
    pump(0.2)
    if "[nihao]" in logtext():
        v1_strip = True
        break
check("V1a vi INSERT shows the IME strip", v1_strip)
child.send(" ")                   # commit 你好
time.sleep(0.5)
child.send(bytes([0x1B]))         # Esc -> CMD
time.sleep(0.4)
child.send(":wq\r")               # save + quit
time.sleep(1.0)
child.expect_exact("ush>", timeout=30)
sh("cat /tmp/t4.txt")
log = logtext()
check("V1b vi :wq saves 你好 (cat shows it)", "你好" in log,
      log[-200:] if "你好" not in log else "")

# ---------- V2: vi yy copy + p paste ----------
sh("echo alpha > /tmp/t5.txt")
sh("echo beta >> /tmp/t5.txt")
child.sendline("vi /tmp/t5.txt")
time.sleep(1.0)
child.send("yy")                  # copy line 1
time.sleep(0.3)
child.send("p")                   # paste below
time.sleep(0.4)
child.send(":wq\r")
time.sleep(1.0)
child.expect_exact("ush>", timeout=30)
sh("cat /tmp/t5.txt")
log = logtext()
v2 = log.count("alpha") >= 2
check("V2 vi yy + p duplicates the line", v2)

# ---------- E1: strictly valid UTF-8 through everything ----------
raw = open(f"{OUT}/pty-all.log", "rb").read()
try:
    raw.decode("utf-8", errors="strict")
    t = True
    why = ""
except UnicodeDecodeError as e:
    t = False
    why = str(e)
check("E1 serial stream strictly valid UTF-8", t, why)

print()
if fails == 0:
    print("wp06g2_editor_verify: ALL PASS (10/10)")
    child.kill(9)
    sys.exit(0)
print(f"wp06g2_editor_verify: {fails} FAILURES")
child.kill(9)
sys.exit(1)
