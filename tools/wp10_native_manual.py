#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""WP-10-tui-native isolated-scenario verifier.

The main E2E (wp10_native_e2e.py) walks every application in one long
session.  QEMU's serial backend intermittently drops RX bytes (worst
after monitor screendumps; see the E2E docstring), and a single lost
keystroke mid-flow desyncs everything AFTER it - which is why a few
checks there flap between runs even though the kernel features work.

This verifier runs the FLAPPING checks as ISOLATED scenarios, each on
a fresh QEMU instance, using the exact key sequences that were proven
reliable with in-kernel RX traces and screen dumps:

  S1  Editor full round: create -> ASCII+CJK IME input -> ^O save ->
      ^X+y exit -> shell cat verifies UTF-8 bytes on disk
  S2  zh terminal banner: lang zh -> tui -> terminal -> the Group C
      bilingual banner must read Chinese on the serial console
  S3  Files delete-cancel: create qa_tmp.txt -> d (confirm dialog) ->
      n (cancel) -> shell ls: qa_tmp.txt must survive (the
      break->continue interaction fix)
  S4  Panel tz persist: clock page -> Tab to the timezone page ->
      '+' then 's' -> /etc/opencube.conf must contain tz.offset

Usage: python3 tools/wp10_native_manual.py <iso>
Exit code: number of failed scenarios."""
import os, re, sys, time
import pexpect

OC_TOOLS = os.environ.get("OC_TOOLS", "/home/z/tools/grub")
QEMU = f"{OC_TOOLS}/usr/bin/qemu-system-x86_64"
F10 = "\x1b[26~"

RESULTS = []
def check(name, ok, ev=""):
    RESULTS.append((name, bool(ok)))
    print(f"  {'PASS' if ok else 'FAIL'}  {name}" + (f"   [{ev}]" if ev else ""))
    return bool(ok)

def spawn_qemu(iso):
    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = (f"{OC_TOOLS}/usr/lib/x86_64-linux-gnu:"
                              f"{OC_TOOLS}/lib/x86_64-linux-gnu")
    args = ["-m", "512M", "-cdrom", iso, "-boot", "d",
            "-L", f"{OC_TOOLS}/usr/share/qemu",
            "-L", f"{OC_TOOLS}/usr/share/seabios",
            "-display", "none", "-serial", "stdio", "-monitor", "none",
            "-vga", "std", "-no-reboot", "-snapshot"]
    child = pexpect.spawn(QEMU, args, timeout=120, encoding="latin-1",
                          codec_errors="replace", env=env)
    child.expect(r"oc>\s*", timeout=200)
    return child

def drain(child, t):
    out = ""
    end = time.time() + t
    while time.time() < end:
        try:
            out += child.read_nonblocking(size=65536, timeout=0.3)
        except Exception:
            pass
    return out

def run(child, cmd, t=25):
    child.sendline(cmd)
    try:
        child.expect(r"oc>\s*", timeout=t)
        return child.before or ""
    except Exception:
        return ""

def leave_desktop(child):
    for _ in range(6):
        child.send(F10)
        time.sleep(1.4)
        out = drain(child, 0.8)
        if "desktop closed" in re.sub(r"\x1b\[[0-9;?]*[a-zA-Z]", "", out):
            break
    child.sendline("")
    try:
        child.expect(r"oc>\s*", timeout=20)
        return True
    except Exception:
        return False

def s1_editor(iso):
    print("== S1: editor full round (isolated) ==")
    child = spawn_qemu(iso)
    child.sendline("tui")
    drain(child, 4.5)
    child.send("4")
    time.sleep(3.2)
    child.send("/qa_cn.txt\r")           # burst: path + Enter
    time.sleep(3.0)
    child.send("native editor ascii line\r")
    time.sleep(2.0)
    # IME: F4 -> zhong -> 1 -> wen -> 1 -> F4
    child.send("\x1b[14~"); time.sleep(1.4)
    child.send("zhong"); time.sleep(1.4)
    child.send("1"); time.sleep(1.2)
    child.send("wen"); time.sleep(1.4)
    child.send("1"); time.sleep(1.2)
    child.send("\x1b[14~"); time.sleep(1.2)
    child.send("\r"); time.sleep(1.2)
    child.send("\x0f")                   # ^O save
    time.sleep(2.2)
    child.send("\x18")                   # ^X exit
    time.sleep(1.6)
    child.send("y")                      # dirty-confirm: save
    time.sleep(2.0)
    child.send("n")                      # (fallback if y was eaten)
    time.sleep(1.5)
    leave_desktop(child)
    out = run(child, "cat /qa_cn.txt")
    o = out
    ascii_ok = "native editor ascii line" in re.sub(r"\x1b\[[0-9;?]*[a-zA-Z]", "", o)
    cjk = re.findall(r"[\xe4-\xe9][\x80-\xbf][\x80-\xbf]", o)
    check("S1 Editor: cat shows ASCII line", ascii_ok)
    check("S1 Editor: cat shows CJK (UTF-8 bytes)", len(cjk) >= 2,
          f"cjk={len(cjk)}")
    # reload round-trip on the same instance
    child.sendline("tui")
    drain(child, 4.5)
    child.send("4")
    time.sleep(3.2)
    child.send("/qa_cn.txt\r")
    time.sleep(3.0)
    child.send("\x18"); time.sleep(1.6)
    child.send("n"); time.sleep(1.5)     # discard (content verified by cat)
    leave_desktop(child)
    out = run(child, "cat /qa_cn.txt")
    cjk2 = re.findall(r"[\xe4-\xe9][\x80-\xbf][\x80-\xbf]", out)
    check("S1 Editor: file intact after reload round-trip",
          "native editor ascii line" in re.sub(r"\x1b\[[0-9;?]*[a-zA-Z]", "", out)
          and len(cjk2) >= 2, f"cjk={len(cjk2)}")
    child.sendline("poweroff"); drain(child, 4)
    child.terminate(force=True)

def s2_zh_banner(iso):
    print("== S2: zh terminal banner (isolated) ==")
    child = spawn_qemu(iso)
    child.sendline("lang zh")
    child.expect(r"oc>\s*", timeout=15)
    child.sendline("tui")
    drain(child, 4.5)
    child.send("5")
    out = drain(child, 8)
    zh_b = "[终端] Ring 3 ush".encode().decode("latin-1")
    ok = zh_b in out
    check("S2 zh: terminal banner is Chinese (Group C)", ok)
    if not ok:
        t = re.sub(r"\x1b\[[0-9;?]*[a-zA-Z]", "", out)
        print("   (banner seen:", repr(t[:120]), ")")
    child.sendline("exit")
    drain(child, 3)
    child.send(" ")
    drain(child, 2)
    child.sendline("poweroff"); drain(child, 4)
    child.terminate(force=True)

def s3_delete_cancel(iso):
    print("== S3: Files delete-cancel (isolated) ==")
    child = spawn_qemu(iso)
    child.sendline("tui")
    drain(child, 4.5)
    child.send("1")
    time.sleep(1.8)
    child.send("n"); time.sleep(1.2)
    child.send("qa_dir\r"); time.sleep(1.6)
    child.send("\r"); time.sleep(1.2)       # enter qa_dir
    child.send("f"); time.sleep(1.2)
    child.send("qa_tmp.txt\r"); time.sleep(1.8)
    child.send("d"); time.sleep(1.8)        # confirm dialog
    child.send("n"); time.sleep(2.0)        # CANCEL
    leave_desktop(child)
    out = run(child, "ls /qa_dir")
    ok = "qa_tmp.txt" in re.sub(r"\x1b\[[0-9;?]*[a-zA-Z]", "", out)
    check("S3 Files: delete-cancel keeps qa_tmp.txt", ok)
    child.sendline("poweroff"); drain(child, 4)
    child.terminate(force=True)

def s4_tz_persist(iso):
    print("== S4: Panel tz persist (isolated) ==")
    child = spawn_qemu(iso)
    ok = False
    for _round in range(3):
        child.sendline("tui")
        drain(child, 4.5)
        child.send("3"); time.sleep(3.2)     # panel (app load ~2.5s)
        child.send("4"); time.sleep(2.4)     # Time & Language submenu
        child.send("1"); time.sleep(2.6)     # Clock page
        child.send("\t"); time.sleep(2.2)   # Tab -> timezone page
        child.send("+"); time.sleep(1.8)
        child.send("s"); time.sleep(2.2)     # persist
        leave_desktop(child)
        out = run(child, "cat /etc/opencube.conf")
        if "tz.offset" in re.sub(r"\x1b\[[0-9;?]*[a-zA-Z]", "", out):
            ok = True
            break
    check("S4 Panel: tz.offset persisted", ok, f"rounds={_round+1}")
    child.sendline("poweroff"); drain(child, 4)
    child.terminate(force=True)

def main():
    iso = sys.argv[1]
    s1_editor(iso)
    s2_zh_banner(iso)
    s3_delete_cancel(iso)
    s4_tz_persist(iso)
    failed = sum(1 for _, ok in RESULTS if not ok)
    print(f"\n== {len(RESULTS) - failed}/{len(RESULTS)} PASS, {failed} FAIL ==")
    for name, ok in RESULTS:
        if not ok:
            print(f"   FAIL: {name}")
    return failed

if __name__ == "__main__":
    sys.exit(min(main(), 250))
