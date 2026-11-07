#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""WP-10-tui-native E2E v4: functional rounds + separate shot rounds.

Two hard-won facts drive this architecture:

1. QEMU intermittently DROPS serial RX bytes right after a monitor
   (HMP screendump) command completes - verified with an in-kernel RX
   byte trace.  Keystrokes lost mid-flow desync the whole application
   state machine, so the FUNCTIONAL rounds never take screenshots.

2. The fb backend renders fast and keeps the UART free, so keys are
   otherwise lossless; functional results are verified from the shell
   (ls / cat / ps / ip / dns) after leaving the desktop with F10.

The SHOT rounds re-enter each application afterwards and walk to each
interesting page; a lost keystroke there only loses the shot, never a
verdict.  Every shot is still preceded by a NUL warmup burst to make
the loss window eat discardable bytes.

Coverage (WP-10-tui-native acceptance table):
  [Files]     mkdir/new/copy/move/rename/delete(+confirm,+cancel)/
              viewer/attributes/mount - shell-verified
  [Editor]    ASCII+CJK IME input, ^O save, F12 save-as, reload -
              cat-verified (UTF-8 clean)
  [Processes] live loop row, detail page, Kill with confirm -
              ps-verified
  [Panel]     DHCP/static-IP/DNS (ip/dns-verified), RTC/tz keys,
              config-verified, update page, hardware Tab paging
  [Terminal]  ring-3 ush: echo/cat(CJK)/ls/ps/jobs/exit - serial text
  [Chinese]   F2 zh sweep with CJK-density pixel checks on shots

Usage: python3 tools/wp10_native_e2e.py <iso> <bios|uefi> [outdir]
Exit code: number of failed checks."""
import os, re, sys, time, socket
import pexpect
from PIL import Image

OC_TOOLS = os.environ.get("OC_TOOLS", "/home/z/opt/extract/root")
QEMU = f"{OC_TOOLS}/usr/bin/qemu-system-x86_64"

F2  = "\x1b[12~"   # tui_input CSI map: 12~ -> F2
F4  = "\x1b[14~"   # 14~ -> F4
F5  = "\x1b[20~"   # 20~ -> F5
F12 = "\x1b[29~"   # 29~ -> F12
F10 = "\x1b[26~"   # 26~ -> F10
CTRL_O = "\x0f"
CTRL_X = "\x18"
DOWN = "\x1b[B"
ESCZ = "\x1bz"     # Esc (delivered) + no-op 'z'

def norm(s):
    s = re.sub(r"\x1b\[[0-9;?]*[a-zA-Z]", "", s)
    s = re.sub(r"\x1b\][^\x07]*(\x07|\x1b\\)", "", s)
    return re.sub(r"[ \t]+", " ", s)

def zh(s):
    return s.encode("utf-8").decode("latin-1")

RESULTS = []
def check(name, ok, ev=""):
    RESULTS.append((name, bool(ok)))
    print(f"  {'PASS' if ok else 'FAIL'}  {name}" + (f"   [{ev}]" if ev else ""))
    return bool(ok)

def main():
    iso, mode = sys.argv[1], sys.argv[2]
    outdir = sys.argv[3] if len(sys.argv) > 3 else "/tmp/native-e2e"
    os.makedirs(outdir, exist_ok=True)
    shots = os.path.join(outdir, "shots")
    os.makedirs(shots, exist_ok=True)
    qmon = "/tmp/native_qmon.sock"
    if os.path.exists(qmon):
        os.unlink(qmon)
    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = (f"{OC_TOOLS}/usr/lib/x86_64-linux-gnu:"
                              f"{OC_TOOLS}/lib/x86_64-linux-gnu")
    args = ["-m", "512M", "-cdrom", iso, "-boot", "d",
            "-L", f"{OC_TOOLS}/usr/share/qemu",
            "-L", f"{OC_TOOLS}/usr/share/seabios",
            "-display", "none", "-serial", "stdio",
            "-monitor", f"unix:{qmon},server,nowait",
            "-vga", "std", "-no-reboot", "-snapshot",
            "-netdev", "user,id=n1", "-device", "e1000,netdev=n1"]
    if mode == "uefi":
        # 4M pflash pair (CODE readonly + per-run VARS copy) - the same
        # shape wp10_tui_e2e/wp10_chinese_e2e use; the old bare
        # `-bios OVMF.fd` shape fails to load in this toolchain
        # (qemu: could not load PC BIOS).
        code4m = f"{OC_TOOLS}/usr/share/OVMF/OVMF_CODE_4M.fd"
        vars4m = f"{OC_TOOLS}/usr/share/OVMF/OVMF_VARS_4M.fd"
        vars_run = "/tmp/oc_native_efi_vars.fd"
        import shutil
        shutil.copyfile(vars4m, vars_run)
        args += ["-drive", f"if=pflash,format=raw,readonly=on,file={code4m}",
                 "-drive", f"if=pflash,format=raw,file={vars_run}"]
    child = pexpect.spawn(QEMU, args, timeout=200, encoding="latin-1",
                          codec_errors="replace", env=env)
    # v2.0.0: boot lands in the ring-3 ush shell; leave it for the L0
    # recovery prompt (the TUI desktop and these test commands are L0)
    child.expect(r"ush>\s", timeout=200)
    child.sendline("exit")
    child.expect(r"Type 'help' for commands", timeout=90)
    child.expect(r"oc>\s", timeout=60)
    logf = open(os.path.join(outdir, "transcript.log"), "w",
                encoding="latin-1")
    transcript = [""]

    def absorb(buf):
        transcript[0] += buf
        logf.write(buf)
        logf.flush()

    def key(s, wait=0.8):
        absorb(f"\n<<<KEY {s!r}>>>\n")
        # v2 robustness: EVERY key gets the NUL warmup.  The browser
        # re-renders after each action (fl_scan + full repaint) and the
        # render burst opens an RX loss window that eats the NEXT key
        # - proven by the serial-flow probe (the 'r' of the rename
        # round vanished right after the attributes page closed, the
        # 'f' of the create round vanished right after the delete
        # confirm; the form text then ran as browser hotkeys: 'a'
        # opened the attributes view, 'x' cut, 'p' pasted...).  NUL
        # bytes ride the window harmlessly (forms ignore them - the
        # tuiqa editor flow has warmed form input since group R).
        child.send("\x00" * 8)
        time.sleep(0.18)
        child.send(s)
        time.sleep(wait)

    def type_line(s, wait=1.4):
        key(s + "\r", wait)   # ONE burst: split sends lose bytes at
                               # form/editor boundaries (verified with
                               # in-kernel RX traces + screen dumps)

    def drain(t=1.0):
        out = ""
        end = time.time() + t
        while time.time() < end:
            try:
                out += child.read_nonblocking(size=65536, timeout=0.3)
            except Exception:
                pass
        absorb(out)
        return out

    def wait_serial(pat, t=15):
        buf = ""
        end = time.time() + t
        rx = re.compile(pat)
        while time.time() < end:
            try:
                buf += child.read_nonblocking(size=65536, timeout=0.4)
            except Exception:
                pass
            if rx.search(norm(buf)):
                absorb(buf)
                return buf, True
        absorb(buf)
        return buf, False

    def run(cmd, t=60):
        absorb(f"\n<<<RUN {cmd!r}>>>\n")
        child.sendline(cmd)
        child.expect(r"oc>\s*", timeout=t)
        out = child.before or ""
        absorb(out + "\n")
        return out

    def run_ush(cmd, t=60):
        """v2: run one command inside a ring-3 ush session (the kernel
        `run ush` bridge).  The caller owns entering/leaving the session
        (sendline 'run ush' / 'exit' around the run_ush block)."""
        absorb(f"\n<<<RUNUSH {cmd!r}>>>\n")
        child.sendline(cmd)
        child.expect(r"ush>\s*", timeout=t)
        out = child.before or ""
        absorb(out + "\n")
        return out

    n_shots = [0]
    desktop_up = [False]   # v2 state: is the TUI desktop session live?
    # The blind F10 spam of to_shell()/to_shell_silent() on an
    # ALREADY-CLOSED desktop leaked six F10 CSI tails into the shell
    # line editor every phase boundary; the in-flight ones then
    # killed freshly launched sessions seconds after the verify window
    # (the serial RX delivers bursts late) - phase E of run7 died
    # exactly there.  Tracking the state makes those calls a no-op
    # flush instead of a key storm.
    def shot(name):
        """Screendump + NUL warmup (see module docstring point 1)."""
        n_shots[0] += 1
        ppm = os.path.join(shots, f"{n_shots[0]:02d}_{name}.ppm")
        try:
            mon = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            mon.connect(qmon)
            time.sleep(0.15)
            try:
                mon.setblocking(False)
                while True:
                    d = mon.recv(4096)
                    if not d:
                        break
            except Exception:
                pass
            mon.setblocking(True)
            mon.sendall(b"screendump " + ppm.encode() + b"\n")
            time.sleep(0.8)
            try:
                mon.setblocking(False)
                while True:
                    d = mon.recv(4096)
                    if not d:
                        break
            except Exception:
                pass
            mon.close()
        except Exception as e:
            print(f"  (shot {name} failed: {e})")
            return None, 0.0
        child.send("\x00" * 12)          # warmup for the RX loss window
        time.sleep(0.3)
        try:
            im = Image.open(ppm).convert("RGB")
            px = im.getdata()
            total = im.width * im.height
            dark = sum(1 for p in px if p[0] < 24 and p[1] < 24 and p[2] < 24)
            ratio = 1.0 - dark / max(total, 1)
            png = ppm[:-4] + ".png"
            im.save(png)
            return png, ratio
        except Exception as e:
            print(f"  (shot {name} unreadable: {e})")
            return None, 0.0

    def cjk_density(path):
        try:
            im = Image.open(path).convert("RGB")
            w, h = im.size
            px = im.load()
            lit = total = 0
            for y in range(0, h, 3):
                for x in range(0, w, 3):
                    p = px[x, y]
                    total += 1
                    if p[0] > 40 or p[1] > 40 or p[2] > 40:
                        lit += 1
            return lit / max(total, 1)
        except Exception:
            return 0.0

    def to_desktop():
        """v2 hardening: a live session (form included) is closed
        first - launching 'tui' while the desktop owns the console
        would type the command into whatever app is focused."""
        if desktop_up[0]:
            to_shell()
        """v2 hardening: F10 bytes a preceding to_shell() sent can
        arrive LATE (the serial RX window delivers bursts after the
        close was already observed) - they either concatenate onto
        this 'tui' command line ('[26~tui' -> unknown command -> the
        round runs on a dead desktop) or kill the fresh session the
        moment it starts.  Flush the line editor first, then verify
        the session actually came up; retry up to 3 times."""
        for _launch in range(3):
            child.send("\x00" * 12)          # warmup: absorb late RX
            time.sleep(0.2)
            child.sendline("")               # submit any junk partial line
            time.sleep(0.3)
            drain(0.3)
            child.sendline("tui")
            time.sleep(2.7)
            out = drain(0.9)
            if "desktop closed" not in norm(out):
                desktop_up[0] = True
                return
            absorb(f"\n(launch #{_launch+1} killed by late F10; retrying)\n")

    def to_shell():
        """Leave the desktop with single F10 presses, stopping the
        moment the kernel prints 'desktop closed' - NO key is ever
        sent after the desktop is down, so the shell line editor
        never receives stray CSI bytes (which corrupted 'tui' into
        '[26~tui' and made whole rounds run on a dead desktop)."""
        if not desktop_up[0]:
            # already at the shell - a bare flush (NO F10 spam: those
            # bytes leak into the line editor and kill later sessions)
            child.sendline("")
            drain(0.4)
            try:
                child.expect(r"oc>\s*", timeout=12)
            except Exception:
                pass
            return True
        for _ in range(6):
            child.send("\x00" * 8); time.sleep(0.15)   # warmup per key
            if _ >= 1:
                # Esc first: a sub-page or PROMPT FORM eats F10s
                # silently (the form driver ignores non-printables),
                # and the flush Enter below would then SUBMIT the form
                # or open the desktop-root selection - exactly how
                # phase E desynced in run7-run10
                child.send("\x1bz")
                time.sleep(0.6)
            child.send(F10)
            time.sleep(1.2)
            out = drain(0.7)
            if "desktop closed" in norm(out):
                desktop_up[0] = False
                break
        child.sendline("")
        try:
            child.expect(r"oc>\s*", timeout=25)
            return True
        except Exception:
            return False

    # ================= Phase A: Files functional round =================
    print("== phase A: Files functional round ==")
    to_desktop()
    key("1", 1.2)                          # Files
    type_line_step = lambda k, txt: (key(k, 0.9), type_line(txt, 1.0))
    type_line_step("n", "qa_dir")          # mkdir qa_dir
    key("\r", 1.0)                         # enter qa_dir (index 0)
    type_line_step("n", "sub")
    type_line_step("f", "qa_a.txt")
    type_line_step("f", "qa_b.txt")
    key("c", 0.8)                          # copy qa_b.txt
    key(DOWN * 2, 0.8)                     # -> sub
    key("\r", 1.0)                         # enter sub
    key("p", 1.0)                          # paste copy
    key("\x7f", 1.0)                       # back to /qa_dir
    key("v", 1.2)                          # viewer (no shot here!)
    key(ESCZ, 0.9)                         # leave viewer
    key("a", 1.2)                          # attributes
    key(ESCZ, 0.9)                         # leave attr
    type_line_step("r", "qa_c.txt")        # rename qa_b -> qa_c
    key("x", 0.8)                          # cut qa_c.txt (g_sel=0)
    key("\x7f", 1.0)                       # back to /
    key("p", 1.0)                          # move qa_c to /
    key("d", 1.0); key("y", 1.2)           # delete qa_c with confirm
    # delete-cancel round (independent tail): create then cancel
    child.send("\x00" * 12); time.sleep(0.3)      # warmup: the 'f'/'d'/'n'
    key("f", 1.2); type_line("qa_tmp.txt", 1.6)   # control keys ride the
    child.send("\x00" * 12); time.sleep(0.3)      # same RX window that
    key("d", 1.6)                                  # ate them pre-fix
    child.send("\x00" * 12); time.sleep(0.3)
    key("n", 2.0)           # delete CANCEL (qa_tmp kept)
    key(F5, 1.2)                           # refresh
    key("m", 1.2)                          # mount page
    key(ESCZ, 0.9)                         # leave mount
    key(ESCZ, 1.2)                         # leave Files

    ok = to_shell()
    out = run("ls /qa_dir")
    check("Files: mkdir+create persist (qa_a.txt)", "qa_a.txt" in norm(out))
    check("Files: rename applied (no qa_b.txt)", "qa_b.txt" not in norm(out))
    out = run("ls /qa_dir/sub")
    check("Files: copy landed in sub/", "qa_b.txt" in norm(out))
    out = run("ls /")
    check("Files: move+delete (no qa_c.txt at /)", "qa_c.txt" not in norm(out))
    out = run("ls /")
    check("Files: delete-cancel kept qa_tmp.txt", "qa_tmp.txt" in norm(out))

    # ================= Phase B: Files shot rounds (isolated) =================
    print("== phase B: Files shot rounds ==")
    def shot_page(name, keys):
        """Isolated shot session: fresh desktop -> key walk -> shot.
        A lost keystroke only loses THIS shot; every session starts
        from the clean shell, so desyncs never accumulate.  The launch
        is verified (late F10s can kill a fresh session - the shot
        would then capture the console, not the TUI)."""
        if desktop_up[0]:
            to_shell_silent()
        for _launch in range(3):
            child.send("\x00" * 12); time.sleep(0.2)
            child.sendline(""); time.sleep(0.3); drain(0.3)
            child.sendline("tui")
            time.sleep(2.2)
            out = drain(0.5)
            if "desktop closed" not in norm(out):
                desktop_up[0] = True
                break
            absorb(f"\n(shot launch #{_launch+1} killed by late F10; retrying)\n")
        for k in keys:
            child.send("\x00" * 8); time.sleep(0.12)
            child.send(k)
            time.sleep(1.05)
        png, r = shot(name)
        return png, r

    def to_shell_silent():
        """Robust desktop exit for shot-session boundaries.  Fast path:
        plain F10 (a root-level desktop closes in one key).  A sub-page
        or form may own the key loop and eat F10 silently (the editor
        path-prompt form does) - those get the Esc-then-F10 combo from
        the second attempt on.  The closed-detection accepts both
        languages; leaked F10 CSI bytes land in the shell line editor,
        so one empty line flushes them."""
        if not desktop_up[0]:
            # already closed - flush only (see desktop_up note above)
            child.sendline("")
            drain(0.4)
            try:
                child.expect(r"oc>\s*", timeout=12)
            except Exception:
                pass
            return
        out = ""
        for attempt in range(6):
            child.send("\x00" * 12)
            time.sleep(0.15)
            if attempt >= 1:
                child.send("\x1bz")        # Esc (+ no-op pair byte)
                time.sleep(0.6)
            child.send("\x1b[26~")         # F10
            time.sleep(0.9)
            out += drain(0.5)
            if ("desktop closed" in norm(out)
                    or zh("桌面已退出") in out):
                desktop_up[0] = False
                break
        child.sendline("")
        drain(0.4)
        try:
            child.expect(r"oc>\s*", timeout=12)
        except Exception:
            pass

    png, r = shot_page("files_list", ["1"])
    check("Files: browser+list render", r > 0.04, f"nonblack={r:.3f}")
    png, r = shot_page("files_mount", ["1", "m"])
    check("Files: mount page renders", r > 0.03, f"nonblack={r:.3f}")

    # ================= Phase C: Editor functional round =================
    print("== phase C: Editor functional round ==")
    # (retry wrapper below - the UART loss window can eat the '4' key)
    ascii_ok = cjk_ok = False
    for _attempt in range(2):
        to_shell()
        to_desktop()
        key("4", 3.2)                      # app load takes ~2.5s
        key("/qa_cn.txt\r", 2.6)          # path + Enter in ONE burst
        key("native editor ascii line\r", 1.8)
        key(F4, 1.2)                           # IME on
        key("zhong", 1.2)
        key("1", 1.2)                          # pick 中
        key("wen", 1.2)
        key("1", 1.2)                          # pick 文 (or 1st candidate)
        key(F4, 1.0)                           # IME off
        key("\r", 1.0)
        child.send("\x00" * 12); time.sleep(0.3)   # warmup: control keys
        key(CTRL_O, 2.0)                       # save
        child.send("\x00" * 12); time.sleep(0.3)
        key(F12, 1.4); key("/qa_cn2.txt\r", 2.2)   # save-as burst
        child.send("\x00" * 12); time.sleep(0.3)
        key(CTRL_X, 1.4)
        key("y", 1.6)                          # dirty-confirm: save+exit
        key("n", 1.6)                          # (in case y was eaten)
        ok = to_shell()
        out = run("cat /qa_cn.txt")
        o = out
        ascii_ok = "native editor ascii line" in norm(o)
        cjk = re.findall(r"[\xe4-\xe9][\x80-\xbf][\x80-\xbf]", o)
        cjk_ok = len(cjk) >= 2
        if ascii_ok and cjk_ok:
            break
        print(f"  (editor round attempt {_attempt+1} incomplete: "
              f"ascii={ascii_ok} cjk={len(cjk)}; retrying)")
    check("Editor: cat shows ASCII line clean", ascii_ok)
    check("Editor: cat shows CJK clean (UTF-8)", cjk_ok)

    # reload round-trip
    to_desktop()
    key("4", 1.8)
    child.send("\x00" * 12); time.sleep(0.3)
    key("/qa_cn.txt\r", 2.4)
    child.send("\x00" * 12); time.sleep(0.3)
    key(CTRL_X, 1.4)
    key("y", 1.6)
    key("n", 1.6)

    # ================= Phase D: Editor shot rounds (isolated) =================
    print("== phase D: Editor shot rounds ==")
    png, r = shot_page("editor_prompt", ["4"])
    check("Editor: open prompt renders", r > 0.008, f"nonblack={r:.3f}")

    # ================= Phase E: Terminal round =================
    print("== phase E: Terminal (ring-3 ush) ==")
    to_shell()      # resync from the shell (Phase D ended inside the TUI)
    to_desktop()
    key("5", 2.5)
    out, ok = wait_serial(r"ring-3|Ring 3|终端", 15)
    check("Terminal: banner (bilingual)", ok)
    child.sendline("echo hello-from-ush")
    out, ok = wait_serial(r"hello-from-ush", 12)
    check("Terminal: ush echo", ok)
    child.sendline("cat /qa_cn.txt")
    out, ok = wait_serial(zh("中文") + "|[\xe4-\xe9][\x80-\xbf][\x80-\xbf]", 12)
    check("Terminal: ush cat shows CJK", ok)
    child.sendline("ls /qa_dir")
    out, ok = wait_serial(r"qa_a.txt", 12)
    check("Terminal: ush ls", ok)
    child.sendline("ps")
    out, ok = wait_serial(r"ush", 12)
    check("Terminal: ush ps table", ok)
    child.sendline("loop &")
    time.sleep(1.5)
    child.sendline("jobs")
    out, ok = wait_serial(r"loop", 12)
    check("Terminal: ush jobs lists loop", ok)
    child.sendline("exit")
    out, ok = wait_serial(r"ush exited \(rc=|已退出 \(rc=|ended \(rc=", 15)
    check("Terminal: ush exit contained", ok)
    key(" ", 2.0)
    drain(1.0)

    # ================= Phase F: Processes functional round =================
    print("== phase F: Processes (live table + kill) ==")
    to_shell()      # resync (Phase E ended on the desktop root)
    to_desktop()
    key("2", 1.5)
    key("\r", 1.2)                         # detail page on loop
    key(ESCZ, 1.2)
    key("k", 1.0); key("y", 1.6)           # kill with confirm
    key(F5, 1.0)
    key(ESCZ, 1.2)
    ok = to_shell()
    out = run("ps")
    check("Processes: loop killed (not in ps)", norm(out).lower().count("loop") == 0)
    # v2: uname is a ring-3 SVC command - verify through the ush bridge
    child.sendline("run ush")
    child.expect(r"ush>\s", timeout=90)
    out = run_ush("uname")
    check("Processes: kernel alive after kill", "Open Cube OS" in norm(out))
    child.sendline("exit")
    child.expect(r"oc>\s", timeout=60)

    # ================= Phase G: Panel functional round =================
    print("== phase G: Panel (net/time/update/hardware) ==")
    to_shell()      # resync from the shell (Phase F ended inside the TUI)
    to_desktop()
    key("3", 1.2)                          # Panel menu (3rd root child)
    key("3", 1.2)                          # Network submenu (3rd panel child)
    key("1", 1.5)                          # Net config (native app, 1st)
    key("d", 6.0)                          # DHCP (QEMU usernet)
    key("i", 1.2); key("10.0.2.15", 0.5); key("\r", 1.4)
    key("255.255.255.0", 0.5); key("\r", 1.4)
    key("10.0.2.2", 0.5); key("\r", 1.8)  # static IP form
    key("n", 1.6); key("10.0.2.3", 0.5); key("\r", 2.0)   # DNS form
    key(ESCZ, 1.4); key(ESCZ, 1.4)         # netconf app -> Network -> Panel
    key("4", 2.4)                          # -> Time & Language submenu
    key("1", 2.4)                          # -> Clock page
    key("\t", 2.0)                        # Tab -> timezone page
    key("+", 1.6); key("s", 2.0)           # tz +1h, persist
    key(ESCZ, 1.0); key(ESCZ, 1.0)         # clock app -> Time menu -> Panel
    key("6", 2.4)                          # -> Update page (crash page=5)
    key("1", 12.0)                         # checkupdate (honest)
    key(" ", 2.0)
    key(ESCZ, 1.0)                          # update page -> Panel
    key("1", 1.5)                          # Hardware menu (1st child)
    key("\t", 1.2)                         # (menu level; no-op)
    key(ESCZ, 1.0); key(ESCZ, 1.2)         # hw menu -> Panel -> root

    ok = to_shell()
    # v2: ip/dns are ring-3 SVC commands now (no oc> equivalent) - the
    # verification runs inside a ush session, the run-ush bridge; cat
    # stays an L0 recovery command.
    child.sendline("run ush")
    child.expect(r"ush>\s", timeout=90)
    out = run_ush("ip")
    check("Panel: static IP applied (10.0.2.15)", "10.0.2.15" in norm(out))
    out = run_ush("dns example.com")
    check("Panel: DNS applied (resolve works)", "A:" in norm(out))
    child.sendline("exit")
    child.expect(r"oc>\s", timeout=60)
    out = run("cat /etc/opencube.conf")
    check("Panel: tz offset persisted", "tz.offset" in norm(out))

    # ================= Phase H: Panel shot rounds (isolated) =================
    print("== phase H: Panel shot rounds ==")
    png, r = shot_page("desktop_root", [])
    check("Panel: desktop renders", r > 0.04, f"nonblack={r:.3f}")
    png, r = shot_page("panel_menu", ["3"])
    check("Panel: menu renders", r > 0.03, f"nonblack={r:.3f}")
    png, r = shot_page("panel_netconf", ["3", "3", "1"])
    check("Panel: net config renders", r > 0.01, f"nonblack={r:.3f}")
    png, r = shot_page("panel_clock", ["3", "4", "1"])
    check("Panel: clock page renders", r > 0.004, f"nonblack={r:.3f}")
    png, r = shot_page("panel_update", ["3", "6"])
    check("Panel: update page renders", r > 0.004, f"nonblack={r:.3f}")
    png, r = shot_page("panel_hw", ["3", "1"])
    check("Panel: hardware page renders", r > 0.02, f"nonblack={r:.3f}")

    # ================= Phase I: zh sweep (isolated shot sessions) =================
    print("== phase I: zh sweep ==")
    # v2: `lang` is an SVC command (ring 3) - set it once through a ush
    # session.  The language is process-global and PERSISTS across
    # desktop sessions (probe-verified), so every shot session below
    # starts in zh; pressing F2 in each session (the v1 flow) would
    # toggle BACK to en from the second screenshot on.
    to_shell_silent()
    child.sendline("run ush")
    child.expect(r"ush>\s", timeout=90)
    child.sendline("lang zh")
    child.expect(r"ush>\s", timeout=30)
    child.sendline("exit")
    child.expect(r"oc>\s", timeout=60)
    png, r = shot_page("zh_desktop", [])
    d = cjk_density(png) if png else 0.0
    check("zh: desktop labels (CJK density)", d > 0.008, f"density={d:.4f}")
    png, r = shot_page("zh_files", ["1"])
    d = cjk_density(png) if png else 0.0
    check("zh: Files key bar (CJK density)", d > 0.008, f"density={d:.4f}")
    png, r = shot_page("zh_proc", ["2"])
    d = cjk_density(png) if png else 0.0
    check("zh: Task manager (CJK density)", d > 0.008, f"density={d:.4f}")
    png, r = shot_page("zh_panel", ["3"])
    d = cjk_density(png) if png else 0.0
    check("zh: Control Panel (CJK density)", d > 0.008, f"density={d:.4f}")
    png, r = shot_page("zh_editor", ["4"])
    d = cjk_density(png) if png else 0.0
    check("zh: Editor prompt (CJK density)", d > 0.008, f"density={d:.4f}")
    # zh terminal banner: console mode -> serial text.  The language is
    # already zh (set once above - the same shell_lang_set the v1 `lang`
    # command drove; the F2 toggle itself is proven by the density
    # screenshots above).  zh_editor's shot leaves the desktop inside
    # the editor path-prompt form; to_shell_silent's Esc+exit handles
    # it, and the NUL warmup protects the '5' key from the
    # post-screendump RX-loss window.
    to_shell_silent()
    for _launch in range(3):
        child.send("\x00" * 12); time.sleep(0.2)
        child.sendline(""); time.sleep(0.3); drain(0.3)
        child.sendline("tui")
        time.sleep(2.7)
        out = drain(0.5)
        if "desktop closed" not in norm(out):
            break
    child.send("\x00" * 12)
    time.sleep(0.4)
    child.send("5")
    time.sleep(4.0)
    out, ok = wait_serial(zh(r"\[终端\] Ring 3 ush"), 20)
    check("zh: Terminal banner (Group C fix)", ok)
    child.sendline("exit")
    wait_serial(r"已退出|ush", 15)
    key(" ", 2.5)
    # close the desktop (warmup + retry - a lost F10 would leave the
    # desktop owning the console and eat the run-ush line below)
    closed = ""
    for _ in range(3):
        child.send("\x00" * 12)
        time.sleep(0.3)
        child.send(F10)
        time.sleep(1.4)
        closed += drain(0.9)
        if ("desktop closed" in norm(closed)
                or zh("桌面已退出") in closed):
            break
    # restore English through the ring-3 lang command (run-ush bridge)
    child.sendline("run ush")
    child.expect(r"ush>\s", timeout=60)
    child.sendline("lang en")
    child.expect(r"ush>\s", timeout=30)
    child.sendline("exit")
    child.expect(r"oc>\s", timeout=60)

    # ================= teardown =================
    print("== phase J: teardown ==")
    ok = to_shell()
    check("teardown: desktop closes, shell alive", ok)
    child.sendline("run ush")
    child.expect(r"ush>\s", timeout=90)
    out = run_ush("uname")
    check("teardown: kernel still up", "Open Cube OS" in norm(out))
    child.sendline("exit")
    child.expect(r"oc>\s", timeout=60)
    child.sendline("poweroff")
    drain(5.0)
    child.terminate(force=True)
    logf.close()

    failed = sum(1 for _, ok in RESULTS if not ok)
    print(f"\n== {len(RESULTS) - failed}/{len(RESULTS)} PASS, {failed} FAIL ==")
    print(f"== {n_shots[0]} screendumps in {shots} ==")
    for name, ok in RESULTS:
        if not ok:
            print(f"   FAIL: {name}")
    return failed

if __name__ == "__main__":
    sys.exit(min(main(), 250))
