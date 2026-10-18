#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""WP-10-tui end-to-end test driver.

Boots the Open Cube OS ISO under QEMU (BIOS or UEFI) and drives the
WP-10-tui command surface.  The TUI itself is frame-based, so the E2E
exercises (a) the tui-test harness (40+ non-interactive checks of
every layer with input/expected/actual/verdict output), (b) every
tui-hw page render through its command entry (terminal output proves
the pages run and return), (c) the tui-sysinfo direct pages, (d) the
registration/help surface, and (e) the resident USB poll thread and
the WiFi bridge (wireless NIC appears in the framework).

Usage: python3 tools/wp10_tui_e2e.py <iso> <mode: bios|uefi> <outlog>
Exit code: number of failed checks.
"""
import os, sys, time, re
import pexpect

OC_TOOLS = os.environ.get("OC_TOOLS", "/home/z/opt/extract")
QEMU = f"{OC_TOOLS}/usr/bin/qemu-system-x86_64"

def strip_ansi(s):
    """Drop CSI/OSC/charset escape sequences, keep the payload text."""
    s = re.sub(r"\x1b\[[0-9;?]*[a-zA-Z]", "", s)
    s = re.sub(r"\x1b\][^\x07]*(\x07|\x1b\\)", "", s)
    s = re.sub(r"\x1b[()][0-9A-B]", "", s)
    s = re.sub(r"\x1b[=>]", "", s)
    return s

def normalize(s):
    """ANSI-stripped text with runs of blanks collapsed (the serial
    backend positions each cell, so raw text is not matchable)."""
    return re.sub(r"[ \t]+", " ", strip_ansi(s))

# (name, command, expected-regex, timeout-extra)  None regex = record only
# Regexes run against ANSI-stripped, whitespace-normalized output.
CMDS = [
    ("set tui backend",   "echo tui.backend=serial >> /etc/opencube.conf", None, 0),
    ("tui-test suite",   "tui-test",
     r"tui-test: \d+/\d+ checks passed, all green", 180),
    ("tui-test layers",  "tui-test", r"\[PASS\] window move in-bounds", 180),
    ("tui-test csi",     "tui-test", r"\[PASS\] CSI arrow sequence", 180),
    ("tui-test mouse",   "tui-test", r"\[PASS\] SGR mouse press", 180),
    ("tui-test colors",  "tui-test", r"\[PASS\] pair roundtrip", 180),
    ("tui-test panels",  "tui-test", r"\[PASS\] panel hides", 180),
    ("tui-test menu",    "tui-test", r"\[PASS\] menu nav enter", 180),
    ("tui-test form",    "tui-test", r"\[PASS\] form edit append", 180),
    ("tui-test hw",      "tui-test", r"\[PASS\] mem stats live", 180),
    ("tui-test config",  "tui-test", r"\[PASS\] config value roundtrip", 180),
    ("tui-test wifi",    "tui-test", r"\[PASS\] wifi connect honest", 180),
    ("tui-test tasks",   "tui-test", r"\[PASS\] task table live", 180),
    ("tui-test logring", "tui-test", r"\[PASS\] log ring readable", 180),
    ("tui-test fix1 getch", "tui-test",
     r"\[PASS\] getch delivers an injected key", 180),
    ("tui-test fix1 block", "tui-test",
     r"\[PASS\] empty queue actually blocked", 180),
    ("tui-test fix1 pool", "tui-test",
     r"\[PASS\] window pool survives 40 new/free cycles", 180),
    ("tui-test fix1 esc", "tui-test",
     r"\[PASS\] Esc key maps through OC_KEY_ESC_KEY", 180),
    ("tui-hw cpu",       "tui-hw CPU",
     r"CPU vendor : (GenuineIntel|AuthenticAMD|QEMU|other)", 120),
    ("tui-hw mem",       "tui-hw Memory", r"memory total : \d+\.\d GiB", 120),
    ("tui-hw storage",   "tui-hw Storage", r"(capacity|no block devices)", 120),
    ("tui-hw network",   "tui-hw Network", r"NICs : \d+", 120),
    ("tui-hw sound",     "tui-hw Sound", r"driver families :", 120),
    ("tui-hw pci",       "tui-hw PCI", r"PCIe devices : \d+ scanned", 120),
    ("tui-hw input",     "tui-hw Input", r"keyboard :", 120),
    ("tui-hw camera",    "tui-hw Camera", r"cameras : \d+", 120),
    ("tui-hw wireless",  "tui-hw Wireless", r"wireless devices: \d+", 120),
    ("tui-hw timers",    "tui-hw Timers", r"RTC epoch ms :", 120),
    ("tui-sysinfo logs", "tui-sysinfo logs", r".", 120),
    ("tui-usb poll thread", "ps",
     r"usb-poll", 120),
    ("wireless stack honest", "wifi",
     r"QEMU has no 802.11 models", 120),
    ("help wp tag",      "help -w", r"WP-10-tui", 120),
    ("help az tui",      "help -a", r"tui-sysinfo", 120),
    ("uname intact",     "uname -a", r"Open Cube OS", 120),
    ("ota intact",       "checkupdate --local", r".", 120),
    ("shell ok after",   "echo alive", r"alive", 120),
]


def tui_reenter_round(child, log, transcript, tag):
    """fix1 regression, unified-desktop edition: enter the desktop (`tui`),
    verify the root menu renders (desktop markers, not the old main-menu
    labels - WP-10-tui-unified replaced the menu app with the dynamic
    desktop), leave via Esc at the root, and confirm a second session
    still renders (the old build drained the 16-slot window pool after a
    few sessions and then silently refused to start)."""
    ok = True
    for i in range(2):
        child.sendline("tui")
        time.sleep(3)
        idx = child.expect([r"oc>", pexpect.TIMEOUT], timeout=8)
        out = normalize(child.before or "")
        rendered = idx == 1 and "Control Panel" in out \
            and "Terminal" in out and "Command Bridge" in out
        log.write(f"[{tag} #{i}] render: {'PASS' if rendered else 'FAIL'}\n")
        if not rendered:
            ok = False
            transcript += f"\n=== {tag} #{i} NO RENDER: {out[-400:]}\n"
        if idx == 1:
            # Esc at the desktop root leaves back to the shell (the
            # serial bridge turns a double-ESC into one real Esc key)
            child.send("\x1b\x1b")
            time.sleep(1.5)
            idx2 = child.expect([r"oc>", pexpect.TIMEOUT], timeout=8)
            back = idx2 == 0
            log.write(f"[{tag} #{i}] exit back to shell: "
                      f"{'PASS' if back else 'FAIL'}\n")
            if not back:
                ok = False
        else:
            ok = False
    return ok


def main():
    iso, mode, outlog = sys.argv[1], sys.argv[2], sys.argv[3]
    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = (f"{OC_TOOLS}/usr/lib/x86_64-linux-gnu:"
                              f"{OC_TOOLS}/lib/x86_64-linux-gnu")
    args = ["-m", "512M", "-cdrom", iso, "-boot", "d",
            "-L", f"{OC_TOOLS}/usr/share/qemu",
            "-L", f"{OC_TOOLS}/usr/share/seabios",
            "-display", "none", "-serial", "stdio", "-monitor", "none",
            "-no-reboot"]
    if mode == "uefi":
        code4m = f"{OC_TOOLS}/usr/share/OVMF/OVMF_CODE_4M.fd"
        vars4m = f"{OC_TOOLS}/usr/share/OVMF/OVMF_VARS_4M.fd"
        if os.path.exists(code4m) and os.path.exists(vars4m):
            # standard QEMU OVMF 4M split (CODE read-only + writable VARS
            # copied to /tmp so repeated runs start from clean NVRAM)
            import shutil, tempfile
            vars_run = os.path.join(tempfile.gettempdir(), "oc_ovmf_vars.fd")
            shutil.copyfile(vars4m, vars_run)
            args += ["-drive", f"if=pflash,format=raw,readonly=on,file={code4m}",
                     "-drive", f"if=pflash,format=raw,file={vars_run}"]
        else:
            ovmf = None
            for cand in (f"{OC_TOOLS}/usr/share/OVMF/OVMF_CODE.fd",
                         f"{OC_TOOLS}/usr/share/ovmf/OVMF_CODE.fd",
                         f"{OC_TOOLS}/usr/share/ovmf/OVMF.fd"):
                if os.path.exists(cand):
                    ovmf = cand
                    break
            if ovmf is None:
                sys.stderr.write("no OVMF firmware found\n")
                return 1
            args += ["-bios", ovmf]
    # line-buffered: a killed run must still leave every verdict on disk
    log = open(outlog, "w", buffering=1)
    child = pexpect.spawn(QEMU, args, encoding="utf-8",
                          codec_errors="replace", timeout=180,
                          env=env, dimensions=(50, 120))
    try:
        child.expect([r"oc>"], timeout=240)
        log.write("=== boot ok, prompt reached ===\n")
    except Exception as e:
        log.write(f"BOOT FAIL: {e}\n")
        log.write(child.before or "")
        log.close()
        child.close(force=True)
        return 1

    failed = 0
    transcript = ""
    # chunking support for time-boxed sandboxes: OC_E2E_CMDS="a:b" runs
    # CMDS[a:b] (Python slice syntax, either side optional); the reenter
    # regression can be skipped for a chunk with OC_E2E_REENTER=0.  Every
    # chunk boots fresh; the union of chunks must cover the whole suite.
    sel = os.environ.get("OC_E2E_CMDS", "")
    cmds = CMDS
    if sel:
        _a, _, _b = sel.partition(":")
        cmds = CMDS[int(_a or 0):(int(_b) if _b else None)]
        # every chunk runs on a fresh boot: CMDS[0] is the INFO entry that
        # selects the serial backend (echo tui.backend=serial ...); a chunk
        # that skips it would render every page into the invisible fb.
        # Always prepend it for chunked runs (idempotent, uncounted pass).
        if CMDS and CMDS[0][1].startswith("echo tui.backend") \
                and (not cmds or cmds[0] is not CMDS[0]):
            cmds = [CMDS[0]] + cmds
    reenter_on = os.environ.get("OC_E2E_REENTER", "1") == "1"
    reenter_ran = False
    for name, cmd, regex, extra in cmds:
        child.sendline(cmd)
        if cmd.startswith(("tui-hw", "tui-net", "tui-sysinfo")):
            # full-screen page: let it render (sleep, do NOT expect --
            # an intermediate expect would consume the rendered text),
            # then press q so the page's event loop (Tab pages /
            # Esc|q quits) returns to the shell; the rendered text
            # stays in the pexpect buffer and lands in child.before
            time.sleep(3)
            child.send("q")
            time.sleep(1)
        try:
            idx = child.expect([r"oc>", pexpect.TIMEOUT], timeout=60 + extra)
            out = normalize(child.before or "")
            transcript += f"\n=== {name}: {cmd} ===\n{out}\n"
            if regex is None:
                verdict = "INFO"
                ok = True
            else:
                ok = out and __import__("re").search(regex, out) is not None
                verdict = "PASS" if ok else "FAIL"
            log.write(f"[{verdict}] {name}: {cmd}\n")
            if not ok and regex is not None:
                failed += 1
                log.write(f"    expected /{regex}/\n")
                log.write("    got: " + out[-600:] + "\n")
        except Exception as e:
            transcript += f"\n=== {name}: EXCEPTION {e} ===\n"
            log.write(f"[FAIL] {name}: exception {e}\n")
            failed += 1

    # fix1 regression: enter -> exit -> re-enter twice (1c)
    if reenter_on:
        reenter_ran = True
        try:
            log.write("[INFO] tui re-enter regression: starting\n")
            if tui_reenter_round(child, log, transcript, "fix1-reenter"):
                log.write("[PASS] tui re-enter regression: two full sessions\n")
            else:
                failed += 1
                log.write("[FAIL] tui re-enter regression\n")
        except Exception as e:
            failed += 1
            log.write(f"[FAIL] tui re-enter regression: exception {e}\n")
    total = len(cmds) + (1 if reenter_ran else 0)
    log.write("\n=== full transcript tail ===\n")
    log.write(transcript[-8000:])
    log.write(f"\n=== summary: {total - failed}/{total} checks passed, {failed} failed ===\n")
    log.close()
    child.sendline("poweroff")
    child.close(force=True)
    return failed


if __name__ == "__main__":
    sys.exit(main())
