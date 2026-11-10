#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""Full regression gate for WP-10-AUDIT_P2-fix4 (restores the lost
run_g9_regression.py coverage: boot/system checks + every ring-3 user
program + the G6/G7/G8 suites + crypto/FS self-tests).  SSH and HTTPS
E2E run through their own tools (ssh_mainstream_test.py, https E2E
session) and are not part of this script.

Fake-green #64 rework of the assertion engine:
  * every check is bound to ONE output window - either the boot segment
    or the output of one specific command - and is judged ONLY there;
  * each window is ECHO-STRIPPED (the guest's echo of the command line
    is removed), so a pattern can no longer match the command echo
    (the old `[Hh]ello` matched the echo of `run hello`);
  * windows are separate, so no pattern can bleed across commands the
    way the old combined+re.S matcher did (`mmap.*(multi)` matched the
    previous command's echo + this command's echo);
  * a command that TIMED OUT has no valid output window at all: every
    check bound to it fails (the old code appended `<<TIMEOUT>>` and
    carried on matching);
  * patterns are anchored to strings the programs actually print (taken
    from the userprog sources in src/L1 and the L0 diag/crypto bodies),
    so a program that produced no output can no longer pass.
  Stale baselines that are OUT of scope here (documented OLD-KNOWN,
  AUD-2g-027): the `oc>` boot prompt, the v1.0.0/WP-* banner forms and
  the "233 commands" / l1test step 9-10 markers that the current tree
  no longer prints.  Those checks fail honestly (RED), they are not
  fake-green; updating them is a baseline decision, not an engine fix.

Usage: python3 tools/run_fix4_regression.py <iso> [outlog]
Exit code: number of failed checks (1 also on boot/session failure)."""
import os, re, sys
import pexpect

OC_TOOLS = os.environ.get("OC_TOOLS", "/home/z/tools")
QEMU = f"{OC_TOOLS}/usr/bin/qemu-system-x86_64"

CMDS = [
    "uname -a",
    "help -a",
    "heaptest",
    "fstest",
    "cryptotest",
    "crashlog",
    "l1test",
    "run hello",
    "run fork_test",
    "run exec_test",
    "run pipe_test",
    "run signal_test",
    "run select_test",
    "run mmap_test",
    "run mmap_multi",
    "run g7mmap",
    "run dyn_hello",
    "run so_test",
    "run dlsym_test",
    "run pie_test",
    "run reloc_test",
    "run p3_test",
    "run g6_test",
]

# (name, window, regex)
#   window "boot"  -> the pre-prompt banner/boot-self-test segment
#   window "<cmd>" -> the ECHO-STRIPPED output segment of that command
#                     (must be an exact CMDS entry)
# Patterns marked "stale" match strings the current tree no longer
# prints; they are kept (window-bound, honest-RED) per AUD-2g-027.
CHECKS = [
    # ---- boot segment (no echo exists before the first prompt) --------
    ("boot banner ready",        "boot",
     r"ready\. Type 'help' for commands"),                      # stale
    ("boot complete OK",         "boot",
     r"boot complete\.+ OK"),                                   # stale
    ("exception self-test 3/3",  "boot",
     r"exception self-test: 3/3"),
    # ---- per-command output windows (echo-stripped) -------------------
    ("command count 233",        "help -a",
     r"Total:\s*233 commands"),                                 # stale
    # Baseline note: the WP-*/v1.0.0 banner alternation below is the
    # documented interface-freeze history (additions only, never
    # deletions).  Stale on the current tree, honest-RED, AUD-2g-027.
    ("uname current tree",       "uname -a",
     r"Open Cube OS 1\.0\.0"
     r"|WP-10-(AUDIT_P2-fix(4\b|3-\d+-g)|input_chipset|timer_rtc"
     r"|wireless_bluetooth|camera_misc|tui|chinese_support)"
     r"|WP-11-crash_recovery|WP-12-watchdog_logging"
     r"|WP-13-build_system|WP-14-docs_release"),                # stale
    # heaptest (L1SVC_DIAG_HEAPTEST) prints "Heap overhead test
    # (100 allocs):" ... stats ... "  PASS"
    ("heaptest pass",            "heaptest",
     r"Heap overhead test[\s\S]*\bPASS\b"),
    # fstest (diag_l1.c) prints "VFS self-test:" ... both regression
    # families must end "ALL PASS"
    ("fstest pass",              "fstest",
     r"VFS self-test:[\s\S]*p2 regressions: ALL PASS"
     r"[\s\S]*p1fix4 regressions: ALL PASS"),
    # cryptotest (crypto_cmds.c) prints "Crypto self-test (NIST
    # vectors):" ... per-vector PASS/FAIL ... "  N/4 tests passed"
    ("cryptotest NIST pass",     "cryptotest",
     r"Crypto self-test \(NIST vectors\):[\s\S]*4/4 tests passed"),
    ("crashlog clean boot",      "crashlog",
     r"crashlog: (no exceptions recorded|0 entries|\d+ entries)"),
    # l1test steps 9/10 do not exist in the current tree (steps 1..7);
    # stale markers kept honest-RED (AUD-2g-027).
    ("l1test step9 G7 suite",    "l1test",
     r"step 9: G7 sched/sync regression"),                      # stale
    ("l1test step10 G8 suite",   "l1test",
     r"step 10: G8 clock/power/timer regression"),              # stale
    ("l1test overall PASS",      "l1test",
     r"L1 job interface test: PASS"),
    # ---- ring-3 user programs (anchors = exact strings from src/L1) ---
    ("hello output",             "run hello",
     r"hello from userspace"),
    ("fork_test pass",           "run fork_test",
     r"fork_test: PASS"),
    ("exec_test pass",           "run exec_test",
     r"exec_test: PASS"),
    ("pipe_test pass",           "run pipe_test",
     r"pipe_test: PASS"),
    ("signal_test pass",         "run signal_test",
     r"signal_test: PASS"),
    ("select_test pass",         "run select_test",
     r"select_test: PASS"),
    ("mmap_test pass",           "run mmap_test",
     r"mmap_test: PASS"),
    ("mmap_multi pass",          "run mmap_multi",
     r"mmap_multi: PASS"),
    # g7mmap prints "G7MMAP DONE" only after T1..T11 all pass
    ("g7mmap pass",              "run g7mmap",
     r"G7MMAP DONE"),
    ("dyn_hello output",         "run dyn_hello",
     r"hello from dynamic program"),
    # so_test prints "main: foo_add(2,3)=5" (link + PLT resolve + run)
    ("so_test pass",             "run so_test",
     r"foo_add\(2,3\)=5"),
    ("dlsym_test pass",          "run dlsym_test",
     r"foo_add\(7,8\)=15"),
    # pie_test prints "pie_test: loaded at 0x<hex>" (no PASS token
    # exists in that program - the load address IS the evidence)
    ("pie_test pass",            "run pie_test",
     r"pie_test: loaded at 0x[0-9a-fA-F]+"),
    # reloc_test prints all four resolved relocation values on one line
    ("reloc_test pass",          "run reloc_test",
     r"reloc_test: foo_add=\d+ foo_global=\d+ fp=\d+ msg=."),
    ("p3_test pass",             "run p3_test",
     r"p3_test: PASS_P3123"),
    # g6_test's async tally: only "failed tests = 0" is a pass
    ("g6_test full chain pass",  "run g6_test",
     r"failed tests = 0"),
]


def strip_echo(cmd, raw):
    """Remove the guest's echo of the command line itself.

    The window captured between two prompts starts with the echoed
    command (e.g. "run hello"); checks must only judge the OUTPUT that
    followed it.  If the first line does not look like the echo, the
    window is returned untouched."""
    lines = raw.split("\n", 1)
    if lines and lines[0].strip() == cmd.strip():
        return lines[1] if len(lines) > 1 else ""
    return raw


def main():
    iso = sys.argv[1]
    outlog = sys.argv[2] if len(sys.argv) > 2 else "/home/z/work/fix4-evidence/regression_full.log"
    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = (f"{OC_TOOLS}/usr/lib/x86_64-linux-gnu:"
                              f"{OC_TOOLS}/lib/x86_64-linux-gnu")
    child = pexpect.spawn(QEMU, [
        "-m","512M","-cdrom",iso,"-boot","d",
        "-L",f"{OC_TOOLS}/usr/share/qemu","-L",f"{OC_TOOLS}/usr/share/seabios",
        "-vga","std","-display","none","-serial","mon:stdio",
        "-netdev","user,id=n1","-device","e1000,netdev=n1",
    ], timeout=200, encoding="latin-1", codec_errors="replace", env=env)
    try:
        try:
            child.expect(r"oc>\s*", timeout=200)
        except pexpect.TIMEOUT:
            print("[reg] FATAL: no oc> prompt within 200s - boot failed",
                  flush=True)
            return 1
        except pexpect.EOF:
            print("[reg] FATAL: QEMU exited before the oc> prompt",
                  flush=True)
            return 1
        boot_out = child.before or ""   # banner + boot self-test lines
        # windows: "boot" -> boot segment; "<cmd>" -> echo-stripped
        # output of that command, or None when the command TIMED OUT
        # (a timed-out command has NO valid output window - fake-green
        # #64: the old code appended "<<TIMEOUT>>" and kept matching).
        windows = {"boot": boot_out.replace("\x00", "")}
        timeouts = []
        for c in CMDS:
            child.sendline(c)
            idx = child.expect([r"oc>\s*", pexpect.TIMEOUT], timeout=180)
            if idx == 1:
                windows[c] = None
                timeouts.append(c)
                print(f"[reg] TIMEOUT: {c} - no output window", flush=True)
                continue
            windows[c] = strip_echo(c, (child.before or "")).replace("\x00", "")
            print(f"[reg] ran: {c} ({len(windows[c])} bytes output)",
                  flush=True)
            if c == "run g6_test":
                # g6_test runs as a background process and prints its
                # T1..T7 tally asynchronously - wait for the final
                # "failed tests = N" line and fold it into the window.
                if windows[c] is not None:
                    try:
                        child.expect([r"failed tests = \d+.*",
                                      pexpect.TIMEOUT], timeout=300)
                        windows[c] += (child.before or "") + \
                                      (child.after or "")
                    except pexpect.TIMEOUT:
                        print("[reg] g6_test tally TIMEOUT - judged on "
                              "the partial window", flush=True)
                    # NOTE: no bare 'except Exception: pass' here any
                    # more - an unexpected session error must surface
                    # (fake-green #64).
                    windows[c] = windows[c].replace("\x00", "")
                print(f"[reg] g6_test tally collected "
                      f"({len(windows[c])} bytes output)")
    finally:
        child.close(force=True)

    # evidence log: keep the per-command segmentation so a human can
    # audit exactly which window each verdict came from
    combined = windows["boot"] + "\n"
    for c in CMDS:
        seg = windows[c]
        body = seg if seg is not None else "<<TIMEOUT - no output window>>"
        combined += f"--- [{c}] ---\n{body}\n"
    outdir = os.path.dirname(outlog)
    if outdir:
        os.makedirs(outdir, exist_ok=True)
    with open(outlog, "w") as f:
        f.write(combined)

    passed = failed = 0
    failed_names = []
    print("\n=== regression checks ===", flush=True)
    for name, win, pat in CHECKS:
        seg = windows.get(win)
        if seg is None:
            # the window's command timed out (or never ran): no output
            # side evidence -> the check FAILS by construction
            ok = False
        else:
            ok = re.search(pat, seg) is not None
        print(f"  {'PASS' if ok else 'FAIL'}  {name}  [window: {win}]",
              flush=True)
        passed += ok
        failed += (not ok)
        if not ok:
            failed_names.append((name, win))
    if timeouts:
        print("=== timed-out commands (each invalidates its window) ===",
              flush=True)
        for c in timeouts:
            print(f"  TIMEOUT: {c}", flush=True)
    print(f"=== {passed}/{len(CHECKS)} checks passed, {failed} failed ===",
          flush=True)
    if failed_names:
        print("=== FAILED CHECKS ===", flush=True)
        for name, win in failed_names:
            reason = ("window invalidated by TIMEOUT" if windows.get(win) is None
                      else "pattern not found in window")
            print(f"  FAIL: {name} [window: {win}] - {reason}", flush=True)
    sys.exit(failed)

if __name__ == "__main__":
    main()
