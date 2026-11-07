#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 */
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""WP-11-crash_recovery full E2E gate.

Every acceptance scenario from the work-package spec, each in a FRESH
QEMU boot (panic variants halt or reboot the machine):

  S1  exc self-test (#DE/#UD/#PF) - kernel survives          (4.6)
  S2  run sigsegv_test - SIGSEGV delivered, caught, clean     (4.5)
  S3  run stackovf - recursion killed, parent woke, alive     (4.4/4.5)
  S4  run badapp - kernel-access fault kills process          (4.5)
  S5  crashlog lists every entry + flush to /var/log/crash.log (4.3)
  S6  crashtest dump + dumpinfo - dump captured, persisted,
      header decodes (magic/version/reason/task)              (4.2)
  S7  crashtest panic - banner + 16-GPR registers + call
      trace + dump persisted + log flushed + HALT             (4.1)
  S8  crashtest exc - ring-0 #UD -> exception frame panic     (4.1)
  S9  crashtest pf  - ring-0 NULL write -> #PF panic          (4.1)
  S10 crashtest assert - OC_ASSERT(0) -> panic                (4.1)
  S11 crashtest wd - watchdog timeout via the real tick       (4.1)
  S12 crashtest ovf - kthread stack burn -> guard page -> #DF
      -> panic continued on the dedicated panic stack         (4.4)
  S13 crashtest policy reboot + panic -> machine reboots,
      second boot reaches the banner                          (4.1)
  S14 TUI isolation E2E (tools/wp11_tui_isolation_e2e.py)     (4.4)

Usage: python3 tools/wp11_crash_e2e.py <iso> [outlog]
Exit code: number of failed scenarios.
"""
import sys, os, re, subprocess, time
import pexpect

OC_TOOLS = os.environ.get("OC_TOOLS", "/home/z/opt/extract/root")
QEMU = f"{OC_TOOLS}/usr/bin/qemu-system-x86_64"
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu_runner import boot_to_recovery

def run_boot(iso, commands, timeout=120, allow_reboot=False):
    """One fresh QEMU boot + serial commands; returns the full output."""
    args = [QEMU, "-m", "512M", "-cdrom", iso, "-boot", "d",
            "-L", f"{OC_TOOLS}/usr/share/qemu",
            "-L", f"{OC_TOOLS}/usr/share/seabios",
            "-vga", "std", "-display", "none", "-serial", "mon:stdio",
            "-netdev", "user,id=n1", "-device", "e1000,netdev=n1"]
    if not allow_reboot:
        args.insert(args.index("-boot"), "-no-reboot")
    c = pexpect.spawn(args[0], args[1:], timeout=timeout,
                      encoding="latin-1", dimensions=(30, 100))
    out = []
    try:
        try:
            out.append(boot_to_recovery(c, boot_timeout=150))
        except (pexpect.TIMEOUT, pexpect.EOF):
            return "".join(out) + "\n[BOOT-TIMEOUT]"
        terminal = ("panic", "exc", "pf", "assert", "wd", "ovf")
        for cmd in commands:
            c.sendline(cmd)
            is_terminal = cmd.startswith("crashtest") and \
                any(k in cmd for k in terminal)
            # wait for the next prompt (fast) or drain the terminal
            # banner (panic variants halt/reboot the machine)
            budget = 40 if is_terminal else 20
            try:
                i = c.expect([r"oc>\s*", pexpect.TIMEOUT, pexpect.EOF],
                             timeout=budget)
                out.append(c.before + (c.match.group(0) if i == 0 else ""))
            except (pexpect.TIMEOUT, pexpect.EOF):
                pass
            if is_terminal:
                # reboot sessions need to see the SECOND boot too
                drain_t = 45 if allow_reboot else 15
                try:
                    c.expect([pexpect.TIMEOUT, pexpect.EOF], timeout=drain_t)
                    out.append(c.before)
                except Exception:
                    pass
                break
        # final drain
        try:
            c.expect([pexpect.TIMEOUT, pexpect.EOF], timeout=3)
            out.append(c.before)
        except Exception:
            pass
    finally:
        try: c.terminate(force=True)
        except Exception: pass
    return "".join(out)

RESULTS = []
def check(sid, name, out, patterns, anti=None):
    ok = all(re.search(p, out) for p in patterns) and \
         (not anti or not re.search(anti, out))
    RESULTS.append((sid, name, ok))
    print(f"  {'PASS' if ok else 'FAIL'}  {sid} {name}", flush=True)
    return ok

def main():
    iso = os.path.abspath(sys.argv[1])
    outlog = sys.argv[2] if len(sys.argv) > 2 else "/tmp/wp11_e2e.log"
    log = open(outlog, "w")

    print("=== WP-11 crash_recovery E2E ===", flush=True)

    # ---- S1-S6: one interactive session ----
    out = run_boot(iso, [
        "exc",
        "run sigsegv_test",
        "run stackovf",
        "run badapp",
        "crashlog",
        "crashlog flush",
        "cat /var/log/crash.log",
        "crashtest dump",
        "dumpinfo",
    ])
    log.write(f"\n===== S1-S6 session =====\n{out}\n")
    check("S1",  "exc self-test 3/3 + kernel alive", out,
          [r"passed 3 exceptions, kernel alive"])
    check("S2",  "sigsegv_test: SIGSEGV delivered + caught", out,
          [r"SIGSEGV delivered to pid=\d+ \(handler will run, delivery #1\)",
           r"SIGSEGV-TEST CAUGHT"])
    check("S3",  "stackovf: child killed, parent woke, kernel alive", out,
          [r"KILLING user process \(pid=\d+\) due to exception 14",
           r"STACKOVF-TEST DONE"])
    check("S4",  "badapp: kernel-access fault kills the process", out,
          [r"badapp: trying kernel access",
           r"KILLING user process \(pid=\d+\) due to exception 14"],
           anti=r"KERNEL PANIC")
    check("S5",  "crashlog records all + persists + reads back", out,
          [r"crashlog: 3 entries",
           r"task=stackovf pid=\d+",
           r"task=sigsegv_test pid=\d+",
           r"task=badapp pid=\d+",
           r"crashlog: flushed \d+ bytes to /var/log/crash\.log",
           r"\[\d+s\] #PF Page Fault .*task=stackovf"])
    check("S6",  "crashtest dump + dumpinfo decode", out,
          [r"crashtest: captured \d+ bytes, persisted to /var/crash/core\.dump",
           r"image: \d+ bytes, magic OCDUMP01 OK",
           r"reason: manual crashtest dump",
           r"policy: HALT"])

    # ---- S7: panic direct ----
    out = run_boot(iso, ["crashtest panic"])
    log.write(f"\n===== S7 panic =====\n{out}\n")
    check("S7",  "crashtest panic: full banner + regs + trace + dump + HALT", out,
          [r"\*\*\* KERNEL PANIC: manual crashtest: kernel_panic\(\) \*\*\*",
           r"Registers \(live snapshot\)",
           r"r15=|rdi=|rax=",
           r"stack scan \(approximate return addrs",
           r"(0x[0-9A-F]{16} -> 0x[0-9A-F]{16}|\(none found\))",
           r"core dump: \d+ bytes captured, persisted to /var/crash/core\.dump",
           r"crash log: \d+ total, flushed \d+ bytes",
           r"policy: HALT - system stopped"])

    # ---- S8: #UD ----
    out = run_boot(iso, ["crashtest exc"])
    log.write(f"\n===== S8 exc =====\n{out}\n")
    check("S8",  "crashtest exc: #UD -> exception-frame panic", out,
          [r"EXCEPTION 6: #UD Invalid Opcode",
           r"KERNEL PANIC: ring-0 exception 6",
           r"Registers \(exception frame\)",
           r"int=6",
           r"policy: HALT"])

    # ---- S9: #PF ----
    out = run_boot(iso, ["crashtest pf"])
    log.write(f"\n===== S9 pf =====\n{out}\n")
    check("S9",  "crashtest pf: unmapped write -> #PF panic", out,
          [r"KERNEL PANIC: ring-0 exception 14",
           r"policy: HALT"])

    # ---- S10: assert ----
    out = run_boot(iso, ["crashtest assert"])
    log.write(f"\n===== S10 assert =====\n{out}\n")
    check("S10", "crashtest assert: OC_ASSERT(0) -> panic", out,
          [r"KERNEL PANIC: assertion failed: 0",
           r"policy: HALT"])

    # ---- S11: watchdog ----
    out = run_boot(iso, ["crashtest wd"], timeout=60)
    log.write(f"\n===== S11 wd =====\n{out}\n")
    check("S11", "crashtest wd: watchdog timeout via real tick", out,
          [r"KERNEL PANIC: watchdog timeout: no shell heartbeat",
           r"policy: HALT"])

    # ---- S12: kernel stack overflow ----
    out = run_boot(iso, ["crashtest ovf", "stats"], timeout=60)
    log.write(f"\n===== S12 ovf =====\n{out}\n")
    check("S12", "crashtest ovf: guard page -> #DF -> attributed panic -> HALT", out,
          [r"EXCEPTION 8: #DF Double Fault",
           r"KERNEL PANIC: KERNEL STACK OVERFLOW \(stack exhausted -> #DF, task=kstack-ovf\)",
           r"policy: HALT - system stopped"])

    # ---- S13: reboot policy ----
    out = run_boot(iso, ["crashtest policy reboot", "crashtest panic",
                         "uname -a"],
                   timeout=150, allow_reboot=True)
    log.write(f"\n===== S13 reboot =====\n{out}\n")
    # the machine reboots mid-session; a SECOND boot banner must appear
    banners = len(re.findall(r"boot complete\.* OK", out))
    check("S13", "policy reboot: panic -> 8042 reset -> second boot", out,
          [r"policy set - after the next panic the kernel will REBOOT",
           r"policy: REBOOT - rebooting NOW",
           r"kmain entered"],
          anti=None) or banners >= 2
    if banners < 2:
        RESULTS[-1] = ("S13", "policy reboot: panic -> 8042 reset -> second boot", False)
        print(f"  FAIL  S13 second boot banner count={banners}")

    # ---- S14: TUI isolation (dedicated script) ----
    r = subprocess.run([sys.executable,
                        os.path.join(ROOT, "tools", "wp11_tui_isolation_e2e.py"),
                        iso], capture_output=True, text=True, timeout=400)
    log.write(f"\n===== S14 tui =====\n{r.stdout}\n{r.stderr}\n")
    ok14 = "E2E-PASS" in r.stdout
    RESULTS.append(("S14", "TUI crash containment (page fault -> menu survives)", ok14))
    print(f"  {'PASS' if ok14 else 'FAIL'}  S14 TUI crash containment", flush=True)

    failed = sum(1 for _, _, ok in RESULTS if not ok)
    print(f"=== WP-11 E2E: {len(RESULTS) - failed}/{len(RESULTS)} scenarios passed, "
          f"{failed} failed ===", flush=True)
    log.close()
    return failed

if __name__ == "__main__":
    sys.exit(main())
