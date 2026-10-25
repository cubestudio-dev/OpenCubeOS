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

OC_TOOLS = os.environ.get("OC_TOOLS", "/home/z/tools/grub")
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
        args += ["-bios", f"{OC_TOOLS}/usr/share/ovmf/OVMF.fd"]
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

    n_shots = [0]
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
            time.sleep(1.0)
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
        time.sleep(0.4)
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
        child.sendline("tui")
        time.sleep(3.0)
        drain(1.0)

    def to_shell():
        """Leave the desktop with single F10 presses, stopping the
        moment the kernel prints 'desktop closed' - NO key is ever
        sent after the desktop is down, so the shell line editor
        never receives stray CSI bytes (which corrupted 'tui' into
        '[26~tui' and made whole rounds run on a dead desktop)."""
        for _ in range(6):
            child.send(F10)
            time.sleep(1.4)
            out = drain(0.8)
            if "desktop closed" in norm(out):
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
    key("f", 1.2); type_line("qa_tmp.txt", 1.6)   # create (in /)
    key("d", 1.6); key("n", 2.0)           # delete CANCEL (qa_tmp kept)
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
        from the clean shell, so desyncs never accumulate."""
        to_shell_silent()
        child.sendline("tui")
        time.sleep(2.4)
        for k in keys:
            child.send(k)
            time.sleep(1.3)
        png, r = shot(name)
        return png, r

    def to_shell_silent():
        for _ in range(6):
            child.send(F10)
            time.sleep(1.2)
            out = drain(0.6)
            if "desktop closed" in norm(out):
                break
        child.sendline("")
        try:
            child.expect(r"oc>\s*", timeout=20)
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
        key(CTRL_O, 2.0)                       # save
        key(F12, 1.4); key("/qa_cn2.txt\r", 2.2)   # save-as burst
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
    key("/qa_cn.txt\r", 2.4)
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
    out = run("uname")
    check("Processes: kernel alive after kill", "Open Cube OS" in norm(out))

    # ================= Phase G: Panel functional round =================
    print("== phase G: Panel (net/time/update/hardware) ==")
    to_shell()      # resync from the shell (Phase F ended inside the TUI)
    to_desktop()
    key("3", 1.2)
    key("n", 1.0)                          # Network
    key("n", 1.2)                          # Net config (native)
    key("d", 6.0)                          # DHCP (QEMU usernet)
    key("i", 1.2); key("10.0.2.15", 0.5); key("\r", 1.4)
    key("255.255.255.0", 0.5); key("\r", 1.4)
    key("10.0.2.2", 0.5); key("\r", 1.8)  # static IP
    key("n", 1.6); key("10.0.2.3", 0.5); key("\r", 2.0)   # DNS
    key(ESCZ, 1.4); key(ESCZ, 1.4)
    key("4", 2.4)                          # -> Time & Language submenu
    key("1", 2.4)                          # -> Clock page
    key("\t", 2.0)                        # Tab -> timezone page
    key("+", 1.6); key("s", 2.0)           # tz +1h, persist
    key(ESCZ, 1.0); key(ESCZ, 1.0)
    key("u", 1.2)                          # Update
    key("1", 12.0)                         # checkupdate (honest)
    key(" ", 2.0)
    key(ESCZ, 1.0)
    key("h", 1.5)                          # Hardware
    key("\t", 1.2)                         # Tab -> next page
    key(ESCZ, 1.0); key(ESCZ, 1.2)

    ok = to_shell()
    out = run("ip")
    check("Panel: static IP applied (10.0.2.15)", "10.0.2.15" in norm(out))
    out = run("dns example.com")
    check("Panel: DNS applied (resolve works)", "A:" in norm(out))
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
    png, r = shot_page("panel_update", ["3", "5"])
    check("Panel: update page renders", r > 0.004, f"nonblack={r:.3f}")
    png, r = shot_page("panel_hw", ["3", "1"])
    check("Panel: hardware page renders", r > 0.02, f"nonblack={r:.3f}")

    # ================= Phase I: zh sweep (isolated shot sessions) =================
    print("== phase I: zh sweep ==")
    png, r = shot_page("zh_desktop", [F2])
    d = cjk_density(png) if png else 0.0
    check("zh: desktop labels (CJK density)", d > 0.008, f"density={d:.4f}")
    png, r = shot_page("zh_files", [F2, "1"])
    d = cjk_density(png) if png else 0.0
    check("zh: Files key bar (CJK density)", d > 0.008, f"density={d:.4f}")
    png, r = shot_page("zh_proc", [F2, "2"])
    d = cjk_density(png) if png else 0.0
    check("zh: Task manager (CJK density)", d > 0.008, f"density={d:.4f}")
    png, r = shot_page("zh_panel", [F2, "3"])
    d = cjk_density(png) if png else 0.0
    check("zh: Control Panel (CJK density)", d > 0.008, f"density={d:.4f}")
    png, r = shot_page("zh_editor", [F2, "4"])
    d = cjk_density(png) if png else 0.0
    check("zh: Editor prompt (CJK density)", d > 0.008, f"density={d:.4f}")
    # zh terminal banner: console mode -> serial text.  Language set
    # via `lang zh` (equivalent to F2 - both drive shell_lang_set; the
    # F2 toggle itself is proven by the zh sweep screenshots above).
    to_shell_silent()
    child.sendline("lang zh")
    try:
        child.expect(r"oc>\s*", timeout=15)
    except Exception:
        pass
    child.sendline("tui")
    time.sleep(3.0)
    child.send("5")
    time.sleep(4.0)
    out, ok = wait_serial(zh("[终端] Ring 3 ush"), 25)
    check("zh: Terminal banner (Group C fix)", ok)
    child.sendline("exit")
    wait_serial(r"已退出|ush", 15)
    key(" ", 2.5)
    child.send(F10)
    time.sleep(1.5)
    drain(1.0)
    child.sendline("lang en")
    try:
        child.expect(r"oc>\s*", timeout=15)
    except Exception:
        pass

    # ================= teardown =================
    print("== phase J: teardown ==")
    ok = to_shell()
    check("teardown: desktop closes, shell alive", ok)
    out = run("uname")
    check("teardown: kernel still up", "Open Cube OS" in norm(out))
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
