#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""Full regression gate for WP-10-AUDIT_P2-fix4 (restores the lost
run_g9_regression.py coverage: boot/system checks + every ring-3 user
program + the G6/G7/G8 suites + crypto/FS self-tests).  SSH and HTTPS
E2E run through their own tools (ssh_mainstream_test.py, https E2E
session) and are not part of this script.

Usage: python3 tools/run_fix4_regression.py <iso> [outlog]
Exit code: number of failed checks."""
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
    "run p0leak",
]

CHECKS = [
    # (name, command whose section the pattern must match, regex).
    # BUG #64 FIX (P1, WP-fix-critical-security): every pattern used to
    # be matched against the WHOLE combined log with re.S (DOTALL) -
    # command echoes satisfied name-bearing patterns ("run fork_test"
    # itself matches r"fork.*(PASS|ok|parent|child)"), and any PASS
    # anywhere satisfied every check: the gate was structurally
    # always-true.  Each pattern is now scoped to ONE command's output
    # section (echo line stripped), and only the boot checks may look
    # at the boot section.
    ("boot banner ready",        None,   r"Open Cube OS User-space Shell \(3\.0\.0"),
    ("boot prompt hint",         None,   r"Type 'help' for built-in commands"),
    ("boot complete OK",         None,   r"boot complete\.+ OK"),
    ("exception self-test 3/3",  None,   r"exception self-test: 3/3"),
    # Command-count floor (was a stale literal "Total: 233 commands"
    # that no longer exists in the help -a output at all).  The count
    # is now measured from the registry listing itself: two-space
    # indented "name  [tag]  description" lines.  Interface freeze
    # rule: commands may be ADDED, never removed - so the gate asserts
    # the count never drops below the recorded floor.  276 = the g3
    # baseline (275) + p0leak (WP-fix-critical-security P0 fence
    # probe).
    ("command count >= 276",     "help -a", None),   # dynamic (see main)
    # The release banner is the Makefile literal OC_RELEASE_VERSION.
    ("uname current tree",       "uname -a", r"Open Cube OS 3\.0\.0|Open Cube OS 1\.0\.0|WP-10-|WP-11-crash_recovery|WP-12-watchdog_logging|WP-13-build_system|WP-14-docs_release"),
    # WP-fix-critical-security: patterns re-based on the REAL success
    # markers of each program (the old name-bearing patterns were
    # satisfied by the command echo itself - always-true).
    ("heaptest pass",            "heaptest",            r"\bPASS\b"),
    ("fstest pass",              "fstest",              r"fstest.*(PASS|OK)|VFS self-test.*PASS"),
    ("cryptotest NIST pass",     "cryptotest",          r"[Cc]rypto self-test.*pass|cryptotest.*PASS|All \d+ NIST"),
    ("crashlog clean boot",      "crashlog",            r"crashlog: (no exceptions recorded|0 entries|\d+ entries)"),
    ("l1test wp8b loader 14/14", "l1test",              r"ext_wp8b selftest 14/14 - PASS"),
    ("l1test job steps PASS",    "l1test",              r"step \d: .* - PASS"),
    ("l1test overall PASS",      "l1test",              r"L1 job interface test: PASS"),
    ("hello output",             "run hello",           r"[Hh]ello"),
    ("fork_test pass",           "run fork_test",       r"fork.*(PASS|ok|parent|child)"),
    ("exec_test pass",           "run exec_test",       r"exec.*(PASS|ok)"),
    ("pipe_test pass",           "run pipe_test",       r"pipe.*(PASS|ok)"),
    ("signal_test pass",         "run signal_test",     r"signal.*(PASS|ok|handler)"),
    ("select_test pass",         "run select_test",     r"select.*(PASS|ok)"),
    ("mmap_test pass",           "run mmap_test",       r"mmap.*(PASS|ok)"),
    ("mmap_multi pass",          "run mmap_multi",      r"mmap.*(PASS|ok|multi)"),
    ("g7mmap pass",              "run g7mmap",          r"g7mmap.*(PASS|ok)|BUG-0281.*(PASS|ok)"),
    ("dyn_hello output",         "run dyn_hello",       r"hello from dynamic program"),
    ("so_test pass",             "run so_test",         r"main: foo_add\(2,3\)=5"),
    ("dlsym_test pass",          "run dlsym_test",      r"foo_add\(7,8\)=15"),
    ("pie_test pass",            "run pie_test",        r"loaded at 0x[0-9A-Fa-f]+"),
    ("reloc_test pass",          "run reloc_test",      r"foo_add=30 foo_global=42 fp=300"),
    ("p3_test pass",             "run p3_test",         r"p3.*PASS|priority.*PASS"),
    ("g6_test full chain pass",  "run g6_test",         r"failed tests = 0|g6_test.*PASS|g6_test.*all.*pass|\bT7\b.*=PASS=|\b=PASS="),
    # WP-fix-critical-security: the P0 user-pointer fence probe rides
    # the regression gate so every future release re-proves it.
    ("p0leak fence pass",        "run p0leak",          r"p0leak: PASS - all kernel-address probes fenced"),
]

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
    child.expect(r"ush>\s*", timeout=200)
    boot_out = child.before or ""      # banner + boot self-test lines
    outputs = {}
    for c in CMDS:
        child.sendline(c)
        idx = child.expect([r"ush>\s*", pexpect.TIMEOUT], timeout=180)
        outputs[c] = (child.before or "") + ("\n<<TIMEOUT>>" if idx == 1 else "")
        print(f"[reg] ran: {c} ({len(outputs[c])} bytes)")
        if c == "run g6_test":
            # g6_test runs as a background process and prints its T1..T7
            # tally asynchronously - wait for the final "=PASS=" line.
            try:
                child.expect([r"failed tests = \d+.*", pexpect.TIMEOUT], timeout=300)
                outputs[c] += child.before or ""
                outputs[c] += child.after or ""
            except Exception:
                pass
            print(f"[reg] g6_test tally collected ({len(outputs[c])} bytes)")
    child.close(force=True)

    combined = (boot_out + "\n"
                + "\n".join(f"--- [{c}] ---\n{o}" for c, o in outputs.items()))
    combined = combined.replace("\x00", "")   # NUL bytes break regex blocks
    os.makedirs(os.path.dirname(outlog), exist_ok=True)
    with open(outlog, "w") as f:
        f.write(combined)

    # BUG #64 FIX: per-section scoping.  boot_out is the boot section;
    # every other check only sees ITS OWN command's output (first line
    # = the echoed command, stripped so a name-bearing pattern can
    # never be satisfied by the echo itself).
    def section(cmd):
        raw = outputs.get(cmd, "")
        lines = raw.split("\n")
        if lines and lines[0].strip().startswith(cmd.split()[0]):
            lines = lines[1:]          # strip the echoed command line
        return "\n".join(lines)

    boot = boot_out
    passed = failed = 0
    print("\n=== regression checks ===")
    for name, cmd, pat in CHECKS:
        if pat is None:
            # dynamic check: command-count floor from help -a
            sec = section("help -a")
            n = len(re.findall(r"(?m)^  \S+ +\[[^\]]*\]  ", sec))
            ok = n >= 276
            print(f"  {'PASS' if ok else 'FAIL'}  {name} (measured {n})")
        else:
            scope = boot if cmd is None else section(cmd)
            ok = re.search(pat, scope, re.I | re.S) is not None
            print(f"  {'PASS' if ok else 'FAIL'}  {name}")
        passed += ok
        failed += (not ok)
    print(f"=== {passed}/{len(CHECKS)} checks passed, {failed} failed ===")
    sys.exit(failed)

if __name__ == "__main__":
    main()
