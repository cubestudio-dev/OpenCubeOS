#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""WP-10-tui-qa deep-drive E2E: the five desktop applications.

The WP-10-tui E2E proves the desktop renders and re-enters; this driver
actually OPERATES every application through the serial TUI backend:

  [Files]      mkdir + new-file through the real VFS, verified from
               the shell afterwards (ls /)
  [Processes]  task-manager table renders with live rows
  [Editor]     the nano round-trip: prompt form -> real nano -> type a
               line -> ^O save -> ^X exit -> content verified by cat
  [Terminal]   ring-3 ush: echo + ps + exit, kernel stays up
  [Panel]      every control-panel branch page opened for real
               (storage mounts/fsck, network dhcp/route/tcpstats,
               time&language, update, security, log, hardware overview)
  [Language]   F2 live toggle en->zh->en on the running desktop
               (Chinese labels + [zh] marker verified on serial)
  [Containment] desktop closes cleanly, shell alive, artifacts in VFS

Serial key protocol (tui_input.c csi_feed): a lone ESC byte is only
delivered when the NEXT byte arrives (pair-rule or stash-rule), so ESC
bytes are always sent TOGETHER with the next navigation key:
N ESC bytes followed by key K arrive as N Esc presses then K.  The
desktop-root registry sorts children by priority, so the quick-open
digits are files=1 procs=2 panel=3 editor=4 terminal=5 (root status
bar documents "1-9:Quick").

Usage: python3 tools/wp10_tuiqa_apps_e2e.py <iso> <bios|uefi> [outlog]
Exit code: number of failed checks."""
import os, re, sys, time
import pexpect

OC_TOOLS = os.environ.get("OC_TOOLS", "/home/z/opt/extract")
QEMU = f"{OC_TOOLS}/usr/bin/qemu-system-x86_64"

F2 = "\x1b[12~"            # CSI 12~ -> TUI_KEY_F2
CTRL_O = "\x0f"            # nano save
CTRL_X = "\x18"            # nano exit

def strip_ansi(s):
    s = re.sub(r"\x1b\[[0-9;?]*[a-zA-Z]", "", s)
    s = re.sub(r"\x1b\][^\x07]*(\x07|\x1b\\)", "", s)
    s = re.sub(r"\x1b[()][A-B0-9]", "", s)
    s = re.sub(r"\x1b[=>]", "", s)
    return s

def norm(s):
    return re.sub(r"[ \t]+", " ", strip_ansi(s))

def zh(s):
    """UTF-8 Chinese label as the latin-1 mojibake the serial stream
    delivers (spawn decoding is latin-1)."""
    return s.encode("utf-8").decode("latin-1")

RESULTS = []
def check(name, ok, ev=""):
    RESULTS.append((name, bool(ok)))
    print(f"  {'PASS' if ok else 'FAIL'}  {name}" + (f"   [{ev}]" if ev else ""))
    return bool(ok)

def main():
    iso, mode = sys.argv[1], sys.argv[2]
    outlog = sys.argv[3] if len(sys.argv) > 3 else "/tmp/tuiqa-apps-e2e.log"
    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = (f"{OC_TOOLS}/usr/lib/x86_64-linux-gnu:"
                              f"{OC_TOOLS}/lib/x86_64-linux-gnu")
    args = ["-m", "512M", "-cdrom", iso, "-boot", "d",
            "-L", f"{OC_TOOLS}/usr/share/qemu",
            "-L", f"{OC_TOOLS}/usr/share/seabios",
            "-display", "none", "-serial", "stdio", "-monitor", "none",
            "-vga", "std", "-no-reboot", "-snapshot"]
    if mode == "uefi":
        # 4M pflash pair (CODE readonly + per-run VARS copy) - the same
        # shape wp10_tui_e2e/wp10_chinese_e2e use; the old bare
        # `-bios OVMF.fd` shape fails to load in this toolchain
        # (qemu: could not load PC BIOS).
        code4m = f"{OC_TOOLS}/usr/share/OVMF/OVMF_CODE_4M.fd"
        vars4m = f"{OC_TOOLS}/usr/share/OVMF/OVMF_VARS_4M.fd"
        vars_run = "/tmp/oc_tuiqa_efi_vars.fd"
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
    transcript = ""

    def drain(secs):
        nonlocal transcript
        out = ""
        end = time.time() + secs
        while time.time() < end:
            try:
                out += child.read_nonblocking(size=65536, timeout=0.3)
            except Exception:
                pass
        transcript += out
        try:    # incremental flush: a later crash must not lose evidence
            with open(outlog, "w") as f:
                f.write(transcript)
        except Exception:
            pass
        return out

    def run(cmd, t=60):
        nonlocal transcript
        transcript += "\n<<<HOST-RUN %r>>>\n" % (cmd,)
        child.sendline(cmd)
        child.expect(r"oc>\s*", timeout=t)
        out = child.before or ""
        transcript += out + "\n"
        return out

    def send(s, secs=2.0):
        nonlocal transcript
        transcript += "\n<<<HOST-SEND %r>>>\n" % (s,)
        child.send(s)
        return drain(secs)

    def wait_text(pat, t=20, prior=""):
        """v2 serial-backend patience: a full-screen TUI render streams as
        per-cell CSI writes, so an app's key bar / toast can arrive well
        after the key that opened it, and a phrase can be SPLIT across
        the drain/poll boundary (each cell is its own escape run).
        Poll the stream for the literal text; `prior` carries the tail
        of already-drained text so boundary-split phrases still match."""
        nonlocal transcript
        carry = norm(prior)[-40:] if prior else ""
        buf = ""
        end = time.time() + t
        while time.time() < end:
            try:
                buf += child.read_nonblocking(size=65536, timeout=0.4)
            except Exception:
                pass
            if pat in (carry + norm(buf)):
                transcript += buf
                return True
        transcript += buf
        return False

    # ---- phase 0: serial TUI backend ----
    run("echo tui.backend=serial >> /etc/opencube.conf")

    # ---- phase 1: desktop root ----
    child.sendline("tui")
    out = drain(4.0)
    o = norm(out)
    check("desktop root renders (title/items)",
          "Open Cube OS Desktop" in o and "[en]" in o
          and "Control Panel" in o and "Files" in o
          and "Processes" in o and "Editor" in o
          and "Terminal" in o and "Command Bridge" in o)
    check("root status bar: resident shortcuts (1-9/F2/F10)",
          "1-9:Quick" in o and "F2:Lang" in o and "F10:Exit" in o)

    # ---- Files (digit 1) ----
    out = send("1", 3.0)
    check("Files: browser opens (key bar)",
          "MkDir" in norm(out) or wait_text("MkDir", 14, out))
    send("n", 1.2)
    out = send("qa_dir\r", 2.5)
    check("Files: mkdir qa_dir rc=0", "mkdir qa_dir rc=0" in norm(out))
    send("f", 1.2)
    out = send("qa_file\r", 2.5)
    check("Files: create qa_file rc=0", "create qa_file rc=0" in norm(out))

    # ---- Processes (Esc + digit 2) ----
    out = send("\x1b2", 3.0)
    o = norm(out)
    check("Files: Esc exits, Processes opens from root",
          "Task manager" in o and "Esc:Exit" in o)
    check("Processes: live rows (kernel tasks)",
          "kmain" in o or "usb-poll" in o or "no user processes" in o
          or "PID" in o)

    # ---- Editor (Esc + digit 4): native TUI editor round-trip ----
    # v2 desktop: the editor is the NATIVE WP-10-tui-native editor (no
    # nano bridge): path-prompt form -> type -> ^O save -> ^X exit
    # (the exit toast 'editor closed: <path>' survives dt_activate now;
    # the save itself is proven by the VFS content check below).
    out = send("\x1b4", 2.5)
    check("Editor: path prompt form",
          "File to edit" in norm(out) or wait_text("File to edit", 14, out))
    child.send("\x00" * 12); time.sleep(0.4)   # warmup: the editor's
    send("/qa_note.txt\r", 3.0)                # per-char full re-render
    send("hello from nano desktop editor", 1.5) # floods the UART - the
    child.send("\x00" * 12); time.sleep(0.4)   # RX loss window eats
    send(CTRL_O, 2.0)                            # un-warmed control keys
    child.send("\x00" * 12); time.sleep(0.4)
    out = send(CTRL_X, 2.5)               # keep the drain: the exit toast
    check("Editor: save + exit round-trip",   # streams inside this window
          "editor closed" in norm(out) or wait_text("editor closed", 14, out))
    out = send(" ", 3.5)                   # consumed by the nav loop's wait
    check("Editor: back to desktop", "Open Cube OS Desktop" in norm(out))

    # ---- Terminal (digit 5): ring-3 ush ----
    out = send("5", 3.5)
    o = norm(out)
    check("Terminal: ring-3 ush sandbox banner",
          "[terminal] ring-3" in o)
    transcript += "\n<<<HOST-SENDLINE 'echo hello-from-ush'>>>\n"; child.sendline("echo hello-from-ush")
    out = drain(2.5)
    check("Terminal: ush echo output", "hello-from-ush" in norm(out))
    transcript += "\n<<<HOST-SENDLINE 'ps'>>>\n"; child.sendline("ps")
    out = drain(2.5)
    o = norm(out)
    check("Terminal: ush ps table", "ush>" in o and ("pid" in o.lower()
          or "kmain" in o or "name" in o.lower()))
    transcript += "\n<<<HOST-SENDLINE 'exit'>>>\n"; child.sendline("exit")
    out = drain(2.5)
    o = norm(out)
    check("Terminal: ush exits contained",
          "ush exited (rc=" in o or "ended (rc=" in o)
    out = send(" ", 3.0)
    check("Terminal: back to desktop", "Open Cube OS Desktop" in norm(out))

    # ---- Control Panel (digit 3): every branch ----
    out = send("3", 2.5)
    o = norm(out)
    check("Panel: menu level entered",
          "Control Panel" in o and "Hardware" in o and "Storage" in o
          and "Network" in o and "Update" in o)
    # Storage branch (panel child 2): mounts (2), fsck (3)
    send("2", 1.5)
    out = send("2", 3.0)
    o = norm(out)
    check("Panel/Storage: mounts output", "[desktop] mounts" in o
          and ("ramfs" in o.lower() or "/" in o))
    send(" ", 2.0)
    out = send("3", 4.5)
    o = norm(out)
    check("Panel/Storage: fsck honest run", "[desktop] fsck" in o)
    send(" ", 2.0)
    # pop to panel + Network menu (Esc + digit 3).  v2 digit map (the
    # quick-open digits follow the priority-sorted children):
    #   1 Net config (native)  2 dhcp  3 ip  4 route  5 dns  6 firewall
    #   7 tcpstats  8 Wireless - the test predates the native Net config
    #   entry (priority 5, first child) and the crash-test page (panel
    #   priority 45), which shifted every digit after Network.
    send("\x1b3", 1.5)
    out = send("2", 3.5)               # dhcp bridge entry
    o = norm(out)
    check("Panel/Network: dhcp honest (no lease)",
          "[desktop] dhcp" in o and ("no lease" in o.lower()
          or "fail" in o.lower() or "discover" in o.lower() or "rc=" in o))
    send(" ", 2.0)
    out = send("4", 2.5)               # route bridge entry
    o = norm(out)
    check("Panel/Network: route table", "[desktop] route" in o)
    send(" ", 2.0)
    out = send("7", 2.5)               # tcpstats bridge entry
    o = norm(out)
    check("Panel/Network: tcpstats", "[desktop] tcpstats" in o)
    send(" ", 2.0)
    # pop to panel + Time & Language menu (Esc + digit 4)
    send("\x1b4", 1.5)
    out = send("2", 2.5)
    o = norm(out)
    check("Panel/Time: language page", "Language" in o
          and "English" in o and "Chinese" in o)
    # close lang page, pop to panel, open Update (Esc Esc digit 6:
    # crash-test sits at panel digit 5, Update at 6)
    out = send("\x1b\x1b6", 3.0)
    o = norm(out)
    check("Panel/Update: page renders", "Update" in o
          and "checkupdate" in o and "abupdate" in o)
    # close update page, open Security (Esc + digit 7)
    out = send("\x1b7", 2.5)
    o = norm(out)
    check("Panel/Security: page renders", "security" in o.lower())
    # close security page, open Log (Esc + digit 8)
    out = send("\x1b8", 2.5)
    o = norm(out)
    check("Panel/Log: page renders", "logging" in o.lower())
    # close log page, open Hardware menu (Esc + digit 1)
    send("\x1b1", 1.5)
    out = send("1", 3.5)
    o = norm(out)
    check("Panel/Hardware: overview app (en pages)",
          "Save config" in o and "Tab next page" in o)
    out = send("\r", 3.5)          # Enter: open the CPU page for real
    o = norm(out)
    check("Panel/Hardware: CPU page content",
          "CPU vendor" in o or "GenuineIntel" in o or "vendor" in o.lower())
    # quit hw app, pop hw->panel->root, F2 toggle zh (3x Esc + F2)
    out = send("\x1b\x1b\x1b" + F2, 3.5)
    o = norm(out)
    check("Language F2: desktop switches to zh",
          "[zh]" in o and zh("控制面板") in o and zh("硬件") in o)
    out = send(F2, 3.0)
    o = norm(out)
    check("Language F2: desktop back to en",
          "[en]" in o and "Control Panel" in o)

    # ---- containment: clean exit (pop the panel level, then exit) ----
    out = send("\x1b\x1b\x1b", 3.0)
    o = norm(out)
    check("Desktop closes, shell survives",
          "desktop closed, shell ok" in o)
    try:
        child.expect(r"oc>\s*", timeout=15)
    except Exception:
        pass

    # ---- phase 2: VFS artifacts from inside the apps ----
    out = run("ls /")
    check("VFS: qa_dir + qa_file exist (Files app wrote them)",
          "qa_dir" in out and "qa_file" in out)
    out = run("cat /qa_note.txt")
    check("VFS: nano note content matches",
          "hello from nano desktop editor" in out)
    run("echo apps-e2e-final-alive")

    child.terminate(force=True)
    with open(outlog, "w") as f:
        f.write(transcript)

    ok = sum(1 for _, v in RESULTS if v)
    print(f"=== {ok}/{len(RESULTS)} desktop-app checks passed ({mode}) ===")
    sys.exit(len(RESULTS) - ok)

if __name__ == "__main__":
    main()
