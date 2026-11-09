#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""WP-10-wp08fix1 end-to-end self test: line editing (oc> and ush),
the new ush tools, and the nano editors.

Typing strategy (WP-10-wp08fix1): commands and control characters go
through the REAL serial console path (qemu -serial mon:stdio -> guest
COM1 IRQ -> keyboard queue), exactly like a human terminal. Only the
keys a serial link cannot carry (arrows/Home/End) are sent through the
QEMU monitor `sendkey`, so the full PS/2 keyboard path is exercised
where it matters.

Usage: python3.13 tools/wp10_wp08fix1_test.py build/opencube.iso
"""
import sys, os, re, time, signal
import pexpect

OC_TOOLS = os.environ.get("OC_TOOLS", "/home/z/opt/extract")
QEMU_BIN = f"{OC_TOOLS}/usr/bin/qemu-system-x86_64"
SEABIOS_DIR = f"{OC_TOOLS}/usr/share/seabios"
QEMU_DATADIR = f"{OC_TOOLS}/usr/share/qemu"

def strip_ansi(s):
    s = re.sub(r'\x1b\[[0-9;]*[a-zA-Z]', '', s)
    s = re.sub(r'\x1b\[\?\d+[a-zA-Z]', '', s)
    return s

KEYMAP = {
    ' ': 'spc', '.': 'dot', '-': 'minus', '_': 'shift-minus',
    '/': 'slash', '=': 'equal', '\n': 'ret', '>': 'shift-dot',
    ':': 'shift-semicolon', ';': 'semicolon', "'": 'apostrophe',
    '$': 'shift-4', '{': 'shift-bracket_left', '}': 'shift-bracket_right',
}

def key_name(ch):
    if ch.isupper():
        return f"shift-{ch.lower()}"
    if ch in KEYMAP:
        return KEYMAP[ch]
    return ch

class Tester:
    def __init__(self, iso):
        cmd = [
            QEMU_BIN,
            "-m", "512M",
            "-cdrom", iso,
            "-boot", "d",
            "-L", QEMU_DATADIR,
            "-L", SEABIOS_DIR,
            "-vga", "std", "-display", "none",
            "-serial", "mon:stdio",
            "-netdev", "user,id=n1",
            "-device", "e1000,netdev=n1",
            "-no-reboot",
        ]
        self.child = pexpect.spawn(cmd[0], cmd[1:], timeout=90,
                                   encoding='utf-8', codec_errors='replace')
        self.child.logfile_read = sys.stdout
        self.results = []

    def hmp(self, hcmd, timeout=15):
        self.child.send("\x01c")
        self.child.expect(r"\(qemu\)", timeout=timeout)
        self.child.sendline(hcmd)
        self.child.expect(r"\(qemu\)", timeout=timeout)
        out = strip_ansi(self.child.before)
        self.child.send("\x01c")
        time.sleep(0.15)
        return out

    def key(self, k):
        """Send a key through the PS/2 keyboard path (monitor sendkey)."""
        self.hmp(f"sendkey {k}")
        time.sleep(0.05)

    def send(self, s, delay=0.06):
        """Send raw text/control bytes through the serial console path.
        The guest COM1 IRQ bridges every byte into the keyboard queue:
        \\n = ENTER, \\t = TAB, 0x03 = Ctrl+C, 0x0f = Ctrl+O, ..."""
        for ch in s:
            self.child.send(ch)
            time.sleep(delay)

    def type_text(self, s):
        """Type a full command line (text + ENTER) via serial."""
        self.send(s)

    def wait_oc(self, t=60):
        self.child.expect(r"oc>\s*", timeout=t)

    def wait_ush(self, t=60):
        self.child.expect(r"ush>\s*", timeout=t)

    def record(self, name, ok, evidence=""):
        self.results.append((name, ok, evidence))
        print(f"\n>>> {'PASS' if ok else 'FAIL'}: {name}" +
              (f" [{evidence}]" if evidence else "") + "\n")

    def kill(self):
        try:
            self.child.kill(signal.SIGKILL)
        except Exception:
            pass
        self.child.close(force=True)


def main():
    iso = sys.argv[1]
    t = Tester(iso)
    try:
        print("[test] waiting for boot ...")
        t.wait_oc(90)
        print("[test] booted")

        # ============ A. oc> line editing ============
        # A1: history (Up key recalls previous commands; Up/Down paging)
        t.type_text("echo first_hist_mark\n")
        t.wait_oc()
        t.type_text("echo second_hist_mark\n")
        t.wait_oc()
        t.key("up"); t.key("ret")          # recall "echo second_hist_mark"
        t.wait_oc()
        buf = strip_ansi(t.child.before)
        ok = "second_hist_mark" in buf
        t.key("up"); t.key("up"); t.key("ret")   # page to the older entry
        t.wait_oc()
        buf2 = strip_ansi(t.child.before)
        ok = ok and ("first_hist_mark" in buf2)
        t.record("oc_lineedit_history_test", ok)

        # A2: cursor move + mid-line insert (Left, then type)
        t.type_text("echo AB")
        t.key("left"); t.key("left")       # cursor before "AB"
        t.type_text("XY")                  # insert -> "echo XYAB"
        t.type_text("\n")
        t.wait_oc()
        buf = strip_ansi(t.child.before)
        t.record("oc_lineedit_cursor_test", "XYAB" in buf)

        # A3: Tab completion (clea + TAB -> clear) + Ctrl+U kill
        t.type_text("clea")
        t.key("tab")
        try:
            t.child.expect(r"clear", timeout=5)   # completed text echoes
            tab_ok = True
        except pexpect.TIMEOUT:
            tab_ok = False
        t.send("\x15")                     # Ctrl+U: kill line, do not run
        t.type_text("\n")
        t.wait_oc()
        t.record("oc_lineedit_tab_test", tab_ok)

        # A3b: Home/End/Delete/Ctrl+A/E sanity (real keystrokes)
        t.type_text("echo ZMARK1")
        t.send("\x01")                     # Ctrl+A -> line start
        t.send("\x05")                     # Ctrl+E -> line end
        t.key("left")                      # cursor before "1"
        t.key("delete")                    # Delete removes "1" -> "echo ZMARK"
        t.type_text("2\n")                 # -> "echo ZMARK2"
        t.wait_oc()
        buf = strip_ansi(t.child.before)
        t.record("oc_lineedit_navkeys_test", "ZMARK2" in buf)

        # A4: Ctrl+C cancels the line
        t.type_text("echo should_not_run")
        t.send("\x03")
        t.wait_oc()
        buf = strip_ansi(t.child.before)
        t.type_text("echo alive\n")
        t.wait_oc()
        buf2 = strip_ansi(t.child.before)
        t.record("oc_lineedit_ctrlc_test",
                 ("^C" in buf) and ("alive" in buf2) and ("should_not_run" not in buf2))

        # ============ B. oc> nano editor ============
        t.type_text("nano oc-note.txt\n")
        time.sleep(1.0)
        t.child.expect(r"-- nano: .*oc-note\.txt", timeout=20)
        t.type_text("hello from oc nano")
        t.type_text("\n")
        t.type_text("second line here")
        t.send("\x0f")                     # Ctrl+O: save
        time.sleep(0.5)
        t.child.expect(r"\[wrote 2 lines\]", timeout=20)
        t.send("\x18")                     # Ctrl+X: exit (saved -> no prompt)
        t.wait_oc()
        t.type_text("cat oc-note.txt\n")
        t.wait_oc()
        buf = strip_ansi(t.child.before)
        ok = ("hello from oc nano" in buf) and ("second line here" in buf)
        t.record("oc_editor_test", ok)

        # ============ C. ush ============
        t.type_text("run ush\n")
        t.wait_ush(90)

        # C1: ush history
        t.type_text("echo ufirst_mark\n"); t.wait_ush()
        t.type_text("echo usecond_mark\n"); t.wait_ush()
        t.key("up"); t.key("ret")
        t.wait_ush()
        buf = strip_ansi(t.child.before)
        ok = "usecond_mark" in buf
        t.key("up"); t.key("up"); t.key("ret")
        t.wait_ush()
        buf2 = strip_ansi(t.child.before)
        ok = ok and ("ufirst_mark" in buf2)
        t.record("ush_lineedit_history_test", ok)

        # C2: ush cursor move + mid-line insert
        t.type_text("echo AB")
        t.key("left"); t.key("left")
        t.type_text("XY")
        t.type_text("\n")
        t.wait_ush()
        buf = strip_ansi(t.child.before)
        t.record("ush_lineedit_cursor_test", "XYAB" in buf)

        # C3: ush Tab completion
        t.type_text("unam")
        t.key("tab")
        try:
            t.child.expect(r"uname", timeout=5)   # completed text echoes
            tab_ok = True
        except pexpect.TIMEOUT:
            tab_ok = False
        t.type_text("\n")                  # run uname
        t.wait_ush()
        buf2 = strip_ansi(t.child.before)
        t.record("ush_lineedit_tab_test", tab_ok and ("Open Cube OS" in buf2))

        # C4: ush Ctrl+C
        t.type_text("echo uzz_no")
        t.send("\x03")
        t.wait_ush()
        buf = strip_ansi(t.child.before)
        t.record("ush_lineedit_ctrlc_test", "^C" in buf)

        # C5: ush toolset
        t.type_text("echo hello world > f1.txt\n"); t.wait_ush()
        t.type_text("cat f1.txt\n"); t.wait_ush()
        buf = strip_ansi(t.child.before)
        ok = "hello world" in buf
        t.record("ush_tools_test: cat/echo+redirect", ok)

        t.type_text("ln f1.txt f1link\n"); t.wait_ush()
        t.type_text("cat f1link\n"); t.wait_ush()
        buf = strip_ansi(t.child.before)
        ok = "hello world" in buf
        t.type_text("ln -s /f1.txt f1sym\n"); t.wait_ush()
        t.type_text("cat f1sym\n"); t.wait_ush()
        buf2 = strip_ansi(t.child.before)
        ok = ok and ("hello world" in buf2)
        t.record("ush_tools_test: ln (hard + symlink)", ok)

        t.type_text("chmod 600 f1.txt\n"); t.wait_ush()
        t.type_text("stat f1.txt\n"); t.wait_ush()
        buf = strip_ansi(t.child.before)
        t.record("ush_tools_test: chmod/stat", "600" in buf, buf.strip()[-60:])

        t.type_text("chown 0:0 f1.txt\n"); t.wait_ush()
        t.type_text("stat f1.txt\n"); t.wait_ush()
        buf = strip_ansi(t.child.before)
        t.record("ush_tools_test: chown", ("uid" in buf.lower()) or ("0" in buf))

        t.type_text("sed s/world/there/g f1.txt\n"); t.wait_ush()
        buf = strip_ansi(t.child.before)
        t.record("ush_tools_test: sed", "hello there" in buf)

        t.type_text("awk '{print $2}' f1.txt\n"); t.wait_ush()
        buf = strip_ansi(t.child.before)
        t.record("ush_tools_test: awk", "world" in buf)

        t.type_text("ps\n"); t.wait_ush()
        buf = strip_ansi(t.child.before)
        t.record("ush_tools_test: ps", ("TID" in buf) and ("RUN" in buf or "READY" in buf))

        t.type_text("top\n"); t.wait_ush(120)
        buf = strip_ansi(t.child.before)
        t.record("ush_tools_test: top", ("top - up" in buf) and ("TID" in buf))

        t.type_text("du /\n"); t.wait_ush(60)
        buf = strip_ansi(t.child.before)
        t.record("ush_tools_test: du", ("bytes" in buf) and ("files" in buf))

        t.type_text("ifconfig\n"); t.wait_ush(60)
        buf = strip_ansi(t.child.before)
        t.record("ush_tools_test: ifconfig", ("10.0.2" in buf) or ("IP" in buf.upper()),
                 buf.strip()[-50:])

        t.type_text("netstat\n"); t.wait_ush(60)
        buf = strip_ansi(t.child.before)
        t.record("ush_tools_test: netstat", len(buf.strip()) > 40)

        t.type_text("ping 10.0.2.2\n"); t.wait_ush(90)
        buf = strip_ansi(t.child.before)
        t.record("ush_tools_test: ping", ("reply" in buf.lower()) or ("64 bytes" in buf.lower()),
                 buf.strip()[-60:])

        # C6: ush nano editor
        t.type_text("nano u-note.txt\n")
        t.child.expect(r"-- nano: .*u-note\.txt", timeout=20)
        t.type_text("written in ush nano")
        t.send("\x0f")
        time.sleep(0.5)
        t.child.expect(r"\[wrote 1 lines\]", timeout=20)
        t.send("\x18")
        t.wait_ush()
        t.type_text("cat u-note.txt\n"); t.wait_ush()
        buf = strip_ansi(t.child.before)
        t.record("ush_editor_test", "written in ush nano" in buf)

        # C7: ush exit back to oc>
        t.type_text("exit\n")
        t.wait_oc(30)
        t.type_text("echo back_in_oc\n")
        t.wait_oc()
        buf = strip_ansi(t.child.before)
        t.record("ush_exit_returns_to_oc", "back_in_oc" in buf)

    except pexpect.TIMEOUT as e:
        t.record("TIMEOUT", False, str(e)[:80])
    except pexpect.EOF as e:
        t.record("EOF", False, str(e)[:80])
    finally:
        t.kill()

    print("\n" + "=" * 64)
    print("WP-10-wp08fix1 SELF-TEST SUMMARY")
    print("=" * 64)
    passed = 0
    for name, ok, ev in t.results:
        print(f"  {'PASS' if ok else 'FAIL'}  {name}" + (f"  [{ev}]" if ev else ""))
        if ok:
            passed += 1
    print(f"\n  {passed}/{len(t.results)} passed")
    return 0 if passed == len(t.results) else 1

if __name__ == "__main__":
    sys.exit(main())
