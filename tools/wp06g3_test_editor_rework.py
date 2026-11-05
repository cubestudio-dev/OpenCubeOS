#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 */
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - tools/wp06g3_test_editor_rework.py (3.0.0-WP06g3-ocse_enc_fix)
# Tasks 4.5+4.6+4.7 verification on the main ISO:
#   [A] zh localization: nano's header legend is Chinese under lang zh
#   [B] IME preedit: nihao pinyin NEVER enters the text line (disaster 6);
#       the preedit strip lives on the status bar; commit lands as 你好
#   [C] render: typing letters scrolls NOTHING (disaster 7 measured live:
#       3 scrolled lines per letter, 8 full-bar prints) - the byte stream
#       per letter must carry no '\n' and no bar reprint after the first
#   [D] vi: same preedit discipline in insert mode
import os, socket, sys, time
import pexpect

BUILD = "/home/z/oc-os/build"
EV    = "/home/z/oc-os/evidence/3_0_0_wp06g3_ocse_enc_fix"
QEMU  = "/home/z/opt/extract/usr/bin/qemu-system-x86_64"
QL    = "/home/z/opt/extract/usr/share/qemu"
SEA  = "/home/z/opt/extract/usr/share/seabios"

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

mon_path = f"{EV}/ed-mon.sock"
if os.path.exists(mon_path):
    os.unlink(mon_path)

cmd = (f"{QEMU} -L {QL} -L {SEA} -m 256M -cdrom {BUILD}/opencube.iso -boot d "
       f"-no-reboot -display none -serial stdio "
       f"-monitor unix:{mon_path},server,nowait -vga std")

log = open(f"{EV}/fix4567_editor_rework.bin", "wb")
child = pexpect.spawn(cmd, timeout=120, encoding="latin-1",
                      codec_errors="replace")
child.logfile = open(f"{EV}/fix4567_editor_rework.log", "w",
                     encoding="latin-1")

def raw():
    return open(f"{EV}/fix4567_editor_rework.bin", "rb").read()

try:
    child.expect("ush>", timeout=90)
    print("[0] boot to ush: OK")

    child.sendline("lang zh")
    child.expect("ush>", timeout=30)
    child.sendline("nano t.txt")
    child.expect(r"1  :", timeout=30)
    print("[A1] nano opened: OK")

    # [A] the zh header legend (shows once, Chinese)
    m = Mon(mon_path)
    time.sleep(0.5)
    m.cmd("screendump " + EV + "/fix45_nano_zh_legend.ppm", 1.0)

    # [C] typing discipline: type 'abc' and measure the byte stream
    logpos0 = len(open(f"{EV}/fix4567_editor_rework.log","rb").read())
    m.cmd("sendkey a", 0.5); m.cmd("sendkey b", 0.5); m.cmd("sendkey c", 0.5)
    time.sleep(1.0)
    seg = open(f"{EV}/fix4567_editor_rework.log","rb").read()[logpos0:]
    nl_count = seg.count(b"\n")
    has_bar_reprint = seg.count(b"\x1b[999;1H")
    print(f"[C] typing 'abc': {nl_count} newline(s), {has_bar_reprint} bar repaint(s) in stream")
    if nl_count == 0 and has_bar_reprint <= 1:
        print("[C] no scrolling, differential bar: OK")
    else:
        print("[C] FAIL (postmortem: 3 newlines per letter, full-bar prints)")

    # [B] IME preedit: F2 on, type nihao - letters must NOT reach the text
    m.cmd("sendkey f2", 1.0)      # F2
    logpos1 = len(open(f"{EV}/fix4567_editor_rework.log","rb").read())
    for ch in "nihao":
        m.cmd("sendkey " + ch, 0.4)
    time.sleep(1.5)
    m.cmd("screendump " + EV + "/fix46_nano_ime_preedit.ppm", 1.0)
    seg = open(f"{EV}/fix4567_editor_rework.log","rb").read()[logpos1:]
    # the text line after nihao: "1  : abc..." - no 'n','i','h','a','o' append?
    # the strip must be on the bar: check the stream carries the strip render
    # (ESC[999;1H once for the bar update carrying [nihao])
    strip_on_bar = b"\x1b[999;1H" in seg
    print(f"[B1] preedit bytes: strip-rerender={strip_on_bar}")
    # commit with '1' (first candidate = 你好? engine picks first)
    logpos2 = len(open(f"{EV}/fix4567_editor_rework.log","rb").read())
    m.cmd("sendkey 1", 1.5)
    time.sleep(1.0)
    m.cmd("screendump " + EV + "/fix46_nano_ime_commit.ppm", 1.0)
    seg = open(f"{EV}/fix4567_editor_rework.log","rb").read()[logpos2:]
    has_ni_hao = b"\xe4\xbd\xa0\xe5\xa5\xbd" in seg   # 你好 UTF-8
    print(f"[B2] commit: utf8-nihao={'yes' if has_ni_hao else 'no'}")
    if has_ni_hao:
        print("[B] preedit + commit: OK")
    else:
        print("[B] FAIL")

    # the final text line must NOT contain raw pinyin letters appended after abc
    child.sendcontrol("x")   # ^X exit (dirty -> prompt)
    child.expect("y/n", timeout=20)
    child.sendline("y")
    child.expect("ush>", timeout=20)
    child.sendline("cat t.txt")
    child.expect("ush>", timeout=20)
    # dump the cat output from the stream
    time.sleep(0.5)

    # [D] vi insert mode preedit
    child.sendline("vi t.txt")
    child.expect(r"1  :", timeout=30)
    m.cmd("sendkey i", 1.0)          # insert mode
    m.cmd("sendkey f2", 1.0)         # IME on
    for ch in "ni":
        m.cmd("sendkey " + ch, 0.4)
    time.sleep(1.0)
    m.cmd("screendump " + EV + "/fix46_vi_ime_preedit.ppm", 1.0)
    child.sendcontrol("c")
    time.sleep(0.5)
    child.sendcontrol("x")
    time.sleep(0.5)
    m.cmd("sendkey shift-1", 0.3)   # '!'
    m.cmd("sendkey w", 0.2)
    m.cmd("sendkey q", 0.2)
    m.cmd("sendkey ret", 1.0)
    child.expect("ush>", timeout=20)
    print("[D] vi preedit captured (screen dump)")
    child.terminate(force=True)
except Exception as e:
    print(f"FAILED: {e}")
    child.terminate(force=True)
finally:
    try:
        child.close(force=True)
    except Exception:
        pass

# ---- byte-level verdicts on the closed log ----
raw = open(f"{EV}/fix4567_editor_rework.log", "rb").read()
i_nano = raw.find(b"nano t.txt")
seg = raw[i_nano:]
# the text line after the whole session must not have pinyin letters
# between 'abc' and the commit: find the redraws of "1  : abc"
import re
lines = re.findall(rb"1  : (abc[^\r\n\x1b]*)", seg)
final_txt = lines[-1] if lines else b""
pinyin_leak = b"nihao" in final_txt or b"niha" in final_txt
print(f"[B3] text line after IME session: {final_txt!r} pinyin_leak={pinyin_leak}")
if not pinyin_leak:
    print("[B3] pinyin never entered the text: OK")
else:
    print("[B3] FAIL")
