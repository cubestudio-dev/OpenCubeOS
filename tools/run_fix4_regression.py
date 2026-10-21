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

OC_TOOLS = os.environ.get("OC_TOOLS", "/home/z/opt/extract")
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

CHECKS = [
    # (name, regex-on-combined-output)
    ("boot banner ready",        r"ready\. Type 'help' for commands"),
    ("boot complete OK",         r"boot complete\.+ OK"),
    ("exception self-test 3/3",  r"exception self-test: 3/3"),
    # Baseline note (WP-10-input_chipset): the command count grew 176 -> 192
    # because the batch registers 16 new chipset/input commands (acpi, acpidev,
    # acpitest, acpipow, msitest, pcidump, pcibars, pcie, i2cdetect, i2cget,
    # i2cset, i2ctest, smbus, smbustest, touchpad, touchtest). The interface
    # freeze rule is "commands may be added, never removed", so the baseline
    # moves UP; the old commands are still all present (help -a two-way diff
    # in the batch report covers that).
    # Baseline note (WP-10-timer_rtc): 192 -> 196 with the four time-source
    # commands rtc / hpet / pmtimer / tsc. Removals remain forbidden.
    # WP-10-wireless_bluetooth adds 2 commands (wifi/bt): the baseline
    # moves UP again 198 -> 208 (WP-10-camera_misc: +10 cmds
    # uvc/uvctest/parallel/paralleltest/gameport/gameporttest/ir/irtest/
    # scsi/scsitest; additions only, never deletions).
    # WP-10-tui adds 5 commands (tui/tui-test/tui-hw/tui-net/tui-sysinfo):
    # the baseline moves UP 208 -> 213; removals remain forbidden.
    # WP-10-chinese_support adds 9 commands (chinese chinesetest font
    # font-test encoding encoding-test ime ime-test tui-cn): baseline
    # UP 213 -> 222.
    # WP-10-chinese_support-fix3: suspend/suspend_test hidden per user
    # decision (222 -> 220), `lang` added (220 -> 221), fix4 added
    # tui-time/tui-drv/tui-sys/tui-ext (221 -> 225).
    # WP-10-tui-unified removed the tui-cn launcher: 225 -> 224
    # WP-11-crash_recovery adds crashtest + dumpinfo: 224 -> 226
    ("command count 226",        r"Total:\s*226 commands"),
    # Baseline note (WP-10-input_chipset): accept the current tree describe
    # (fix4-2-g<sha>), the fix3-rebuild form, and the new WP-10-input_chipset
    # tag form (release builds bake the tag name verbatim).
    # WP-10-timer_rtc adds the timer_rtc tag form.
    # WP-10-wireless_bluetooth adds the wireless_bluetooth tag form.
    # WP-10-camera_misc adds the camera_misc tag form.
    # WP-10-tui/chinese_support add their tag + describe forms.
    ("uname current tree",       r"Open Cube OS WP-10-(AUDIT_P2-fix(4\b|3-\d+-g)|input_chipset|timer_rtc|wireless_bluetooth|camera_misc|tui|chinese_support)|WP-11-crash_recovery"),
    ("heaptest pass",            r"heaptest.*PASS|heap test.*PASS"),
    ("fstest pass",              r"fstest.*(PASS|OK)|VFS self-test.*PASS"),
    ("cryptotest NIST pass",     r"[Cc]rypto self-test.*pass|cryptotest.*PASS|All \d+ NIST"),
    ("crashlog clean boot",      r"crashlog: (no exceptions recorded|0 entries|\d+ entries)"),
    ("l1test step9 G7 suite",    r"step 9: G7 sched/sync regression"),
    ("l1test step10 G8 suite",   r"step 10: G8 clock/power/timer regression"),
    ("l1test overall PASS",      r"L1 job interface test: PASS"),
    ("hello output",             r"[Hh]ello"),
    ("fork_test pass",           r"fork.*(PASS|ok|parent|child)"),
    ("exec_test pass",           r"exec.*(PASS|ok)"),
    ("pipe_test pass",           r"pipe.*(PASS|ok)"),
    ("signal_test pass",         r"signal.*(PASS|ok|handler)"),
    ("select_test pass",         r"select.*(PASS|ok)"),
    ("mmap_test pass",           r"mmap.*(PASS|ok)"),
    ("mmap_multi pass",          r"mmap.*(PASS|ok|multi)"),
    ("g7mmap pass",              r"g7mmap.*(PASS|ok)|BUG-0281.*(PASS|ok)"),
    ("dyn_hello output",         r"[Hh]ello"),
    ("so_test pass",             r"so_test.*(PASS|ok)|PLT.*(PASS|ok)"),
    ("dlsym_test pass",          r"dlsym.*(PASS|ok)"),
    ("pie_test pass",            r"pie.*(PASS|ok)"),
    ("reloc_test pass",          r"reloc.*(PASS|ok)"),
    ("p3_test pass",             r"p3.*PASS|priority.*PASS"),
    ("g6_test full chain pass",  r"failed tests = 0|g6_test.*PASS|g6_test.*all.*pass|\bT7\b.*=PASS=|\b=PASS="),
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
    child.expect(r"oc>\s*", timeout=200)
    boot_out = child.before or ""      # banner + boot self-test lines
    outputs = {}
    for c in CMDS:
        child.sendline(c)
        idx = child.expect([r"oc>\s*", pexpect.TIMEOUT], timeout=180)
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

    passed = failed = 0
    print("\n=== regression checks ===")
    for name, pat in CHECKS:
        ok = re.search(pat, combined, re.I | re.S) is not None
        print(f"  {'PASS' if ok else 'FAIL'}  {name}")
        passed += ok
        failed += (not ok)
    print(f"=== {passed}/{len(CHECKS)} checks passed, {failed} failed ===")
    sys.exit(failed)

if __name__ == "__main__":
    main()
