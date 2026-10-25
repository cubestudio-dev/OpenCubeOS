#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 */
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""WP-12-watchdog_logging full E2E gate.

Every functional area of the spec, across three QEMU machine shapes:

  Main session (default pc machine - honestly reports no TCO):
    S1  klog 5 levels + per-level independent switches      (5.3)
    S2  logcat filters: level/module/keyword/count          (5.4/5.6)
    S3  dmesg ring replay with N-line cap                   (5.4)
    S4  logctl control plane: status/level/clear/export     (5.4)
    S5  persistence + rotation: flush, threshold, .1 chain  (5.3/5.5)
    S6  global filter scopes dmesg + logcat                 (5.6)
    S7  soft watchdog status/slots + heartbeat slot         (5.2)
    S8  watchdog test kill + test log (real timeouts)       (5.2)
    S9  health full snapshot (all metric groups)            (5.7)
    S10 health export JSON written + valid                  (5.7)
    S11 CPU alarm: three LOG-policy spinners -> alarm       (5.7)
        + sampler keeps the system heartbeat fed            (5.2)

  q35 session (real Intel TCO):
    S12 TCO discovery in the ring + hw status + hw start 5 ->
        REAL hardware reset -> second boot reaches shell     (5.1)

  q35 + synthetic WDAT session (-acpitable):
    S13 'ACPI WDAT found: 4 entries' + WDAT hw status ->
        hw start 5 -> REAL reset -> second boot              (5.1)

Line-count assertions match the klog replay format
("[ts] LEVEL module: msg") so echoed command text never counts.

Usage: python3 tools/wp12_watchdog_logging_e2e.py <iso> [outlog]
Exit code: number of failed scenarios.
"""
import sys, os, re, subprocess
import pexpect

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu_runner import boot_to_recovery

OC_TOOLS = os.environ.get("OC_TOOLS", "/home/z/opt/extract")
QEMU = f"{OC_TOOLS}/usr/bin/qemu-system-x86_64"
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ENV = dict(os.environ)
ENV["LD_LIBRARY_PATH"] = f"{OC_TOOLS}/usr/lib/x86_64-linux-gnu"

# one replayed ring line: [ 12.345] LEVEL  module: msg
LINE = re.compile(r"\[\d+\.\d+\] (DEBUG|INFO |WARN |ERROR|FATAL) (\S+): ")

def nlines(seg, level=None, module=None):
    n = 0
    for m in LINE.finditer(seg):
        if level and m.group(1).strip() != level: continue
        if module and m.group(2) != module: continue
        n += 1
    return n


def run_boot(iso, commands, timeout=120, allow_reboot=False,
             machine="pc", acpitable=None):
    """One fresh QEMU boot + serial commands; returns the full output."""
    args = [QEMU, "-m", "512M", "-cdrom", iso, "-boot", "d",
            "-L", f"{OC_TOOLS}/usr/share/qemu",
            "-L", f"{OC_TOOLS}/usr/share/seabios",
            "-vga", "std", "-display", "none", "-serial", "mon:stdio",
            "-netdev", "user,id=n1", "-device", "e1000,netdev=n1"]
    if machine != "pc":
        args += ["-machine", machine]
    if acpitable:
        args += ["-acpitable", f"file={acpitable}"]
    if not allow_reboot:
        args.insert(args.index("-boot"), "-no-reboot")
    c = pexpect.spawn(args[0], args[1:], timeout=timeout,
                      encoding="latin-1", dimensions=(30, 100), env=ENV)
    out = []
    try:
        # v2.0.0: boot lands in the ring-3 ush shell; the adapter leaves
        # it and waits for the real oc> prompt (line editor armed)
        out.append(boot_to_recovery(c, boot_timeout=150) or "")
        resets = ("hw start",)
        for cmd in commands:
            c.sendline(cmd)
            is_reset = any(k in cmd for k in resets)
            budget = 60 if is_reset else 20
            try:
                i = c.expect([r"oc>\s*", pexpect.TIMEOUT, pexpect.EOF],
                             timeout=budget)
                out.append(c.before + (c.match.group(0) if i == 0 else ""))
            except (pexpect.TIMEOUT, pexpect.EOF):
                pass
            if is_reset:
                # the machine hard-resets ~5 s later; watch the 2nd boot
                try:
                    i = c.expect([r"Type 'help' for commands",
                                  pexpect.TIMEOUT, pexpect.EOF],
                                 timeout=120)
                    out.append("\n[SECOND-BOOT]" +
                               (c.match.group(0) if i == 0 else c.before))
                except Exception:
                    pass
                break
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
def check(sid, name, ok):
    RESULTS.append((sid, name, ok))
    print(f"  {'PASS' if ok else 'FAIL'}  {sid} {name}", flush=True)
    return ok


def seg(out, begin, end):
    """substring between the first echo of `begin` and the next echo of `end`"""
    try:
        return out.split(begin, 1)[1].split(end, 1)[0]
    except IndexError:
        return ""


def main():
    iso = os.path.abspath(sys.argv[1])
    outlog = sys.argv[2] if len(sys.argv) > 2 else "/tmp/wp12_e2e.log"
    log = open(outlog, "w")

    print("=== WP-12 watchdog_logging E2E ===", flush=True)

    # ---- main session: S1-S11 ------------------------------------------
    out = run_boot(iso, [
        "uname",
        "logctl test",
        "logcat -m klogtest",
        "logctl disable info",
        "logctl test",
        "logctl enable info",
        "logcat -l warn -m klogtest",
        "logcat -k fatal -m klogtest",
        "logcat -n 3 -m klogtest",
        "dmesg 5",
        "logctl status",
        "logctl level 3",
        "logctl level -1",
        "logctl clear",
        "logctl flush",
        "logctl threshold 4096",
        "logctl status",
        "logctl test",
        "logctl test",
        "logctl test",
        "logctl test",
        "ls /var/log",
        "logctl filter 3",
        "logctl test",
        "logcat -m klogtest",
        "logctl filter clear",
        "logctl export /var/log/klog-e2e.txt",
        "watchdog status",
        "watchdog hw status",
        "watchdog test kill",
        "sleep 5",
        "watchdog status",
        "watchdog test log",
        "sleep 5",
        "dmesg 25",
        "health",
        "health export",
        "watchdog test log",
        "watchdog test log",
        "sleep 14",
        "health",
    ], timeout=200)

    log.write(f"\n===== main session S1-S11 =====\n{out}\n")

    # S1: 5 levels emitted once each
    check("S1", "klog 5 levels emitted (DEBUG..FATAL)",
          "klog: 5 lines emitted (DEBUG..FATAL, module klogtest)" in out)
    # S1b: info-off window shows no INFO klogtest lines
    win = seg(out, "logctl disable info", "logctl enable info")
    check("S1b", "info switch off -> no INFO lines",
          nlines(win, "INFO", "klogtest") == 0)

    # S2: logcat filters (level / keyword / count)
    w = seg(out, "logcat -l warn -m klogtest", "logcat -k fatal")
    f = seg(out, "logcat -k fatal -m klogtest", "logcat -n 3")
    c3 = seg(out, "logcat -n 3 -m klogtest", "dmesg 5")
    check("S2", f"logcat -l/-k/-n (warn={nlines(w)}, fatal={nlines(f)}, "
          f"n3={nlines(c3)})",
          nlines(w) >= 3 and nlines(w, "DEBUG", "klogtest") == 0
          and nlines(w, "INFO", "klogtest") == 0
          and nlines(f) >= 1 and nlines(f, "ERROR", "klogtest") == 0
          and nlines(c3) == 3)

    # S3: dmesg 5 -> at most 5 replay lines
    d5 = seg(out, "dmesg 5", "logctl status")
    check("S3", f"dmesg 5 -> {nlines(d5)} lines (<=5)",
          0 < nlines(d5) <= 5)

    # S4: logctl control plane
    check("S4", "logctl status/level/clear/export", all([
          "klog status:" in out,
          re.search(r"ring: \d+/1024 entries, dropped \d+", out),
          re.search(r"level switches: DEBUG=on INFO=on WARN=on ERROR=on "
                    r"FATAL=on", out),
          re.search(r"file: /var/log/kernel\.log open, \d+ bytes "
                    r"\(rotate at \d+, keep 3\)", out),
          "live level set" in out, "ring cleared" in out,
          re.search(r"klog: exported \d+", out)]))

    # S5: persistence + rotation
    check("S5", "flush + threshold + rotation .1 chain", all([
          re.search(r"klog: flushed \d+ bytes to /var/log/kernel\.log", out),
          "rotate threshold set" in out,
          re.search(r"kernel\.log\.1", out),
          re.search(r"rotate at 4096", out)]))

    # S6: global filter scopes dmesg/logcat
    fw = seg(out, "logctl filter 3", "logctl filter clear")
    check("S6", f"filter level>=3 -> ERR/FATAL only "
          f"(dbg={nlines(fw, 'DEBUG', 'klogtest')}, "
          f"inf={nlines(fw, 'INFO', 'klogtest')}, "
          f"wrn={nlines(fw, 'WARN', 'klogtest')}, "
          f"err={nlines(fw, 'ERROR', 'klogtest')}, "
          f"fat={nlines(fw, 'FATAL', 'klogtest')})",
          "filter set" in out and "filter cleared" in out
          and nlines(fw, "DEBUG", "klogtest") == 0
          and nlines(fw, "INFO", "klogtest") == 0
          and nlines(fw, "WARN", "klogtest") == 0
          and nlines(fw, "ERROR", "klogtest") >= 1
          and nlines(fw, "FATAL", "klogtest") >= 1)

    # S7: soft watchdog status + slots + hw honestly 'none' on pc
    check("S7", "watchdog status/slots + hw none on pc", all([
          "watchdog status:" in out,
          re.search(r"hardware: none", out),
          re.search(r"watchdog slots \(\d+\):", out),
          re.search(r"\[0\] system-heartbea\s+15000 ms\s+PANIC", out),
          "watchdog hw: none (no WDAT table, no Intel TCO LPC)" in out]))

    # S8: watchdog test kill + test log via REAL timeouts
    kill_win = seg(out, "watchdog test kill", "watchdog test log")
    log_win = seg(out, "watchdog test log", "health")
    check("S8", "test kill (task killed) + test log (ERROR recorded)",
          "-> KILL task" in out and "-> LOGGED (no action)" in out
          and "watchdog: wd-starve task started" in out
          and re.search(r"wd-starve\s+3000 ms\s+KILL", out))

    # S9: health full snapshot
    check("S9", "health all metric groups", all([
          "system health:" in out, "cpu:" in out, "memory:" in out,
          "tasks:" in out, "disk:" in out, "network:" in out,
          re.search(r"total\s+524160 KiB", out),
          re.search(r"busy\s+\d+ % \(1 s window\)", out)]))

    # S10: health export JSON written + valid
    check("S10", "health export JSON valid", all([
          re.search(r"health: exported \d+ bytes to "
                    r"/var/log/health\.json", out),
          re.search(r'\{"ts_ms":\d+,"cpu_busy_pct":\d+', out),
          re.search(r'"mem_total_kb":524160', out),
          re.search(r'"alarms_text":"(none|CPU-LOCKED\s*)"\}', out)]))

    # S11: CPU alarm from 3 LOG-policy spinners + kernel stays alive
    tail = out.split("sleep 14")[1] if "sleep 14" in out else ""
    check("S11", "3 spinners -> CPU alarm + kernel alive",
          "CPU-LOCKED" in tail and "Type 'help' for commands" in out
          and re.search(r"busy\s+9[6-9] %|busy\s+100 %", tail))

    # ---- q35 session: S12 real TCO reset -------------------------------
    out_q35 = run_boot(iso, [
        "dmesg 40",
        "watchdog hw status",
        "watchdog hw start 5",
    ], timeout=200, allow_reboot=True, machine="q35")
    log.write(f"\n===== q35 session S12 =====\n{out_q35}\n")
    check("S12", "q35 TCO discovery + hw start -> REAL reset + 2nd boot",
          re.search(r"Intel TCO found: PMBASE=0x600", out_q35)
          and re.search(r"watchdog hw: Intel TCO", out_q35)
          and "[SECOND-BOOT]" in out_q35)

    # ---- q35 + WDAT session: S13 ---------------------------------------
    wdat = "/tmp/wp12_wdat.bin"
    subprocess.run([sys.executable, f"{ROOT}/tools/make_wdat_table.py", wdat],
                   check=True, capture_output=True)
    out_wdat = run_boot(iso, [
        "dmesg 40",
        "watchdog hw status",
        "watchdog hw start 5",
    ], timeout=200, allow_reboot=True, machine="q35", acpitable=wdat)
    log.write(f"\n===== q35+WDAT session S13 =====\n{out_wdat}\n")
    check("S13", "WDAT found + WDAT mode + REAL reset + 2nd boot",
          re.search(r"ACPI WDAT found: 4 entries", out_wdat)
          and re.search(r"watchdog hw: ACPI WDAT", out_wdat)
          and "[SECOND-BOOT]" in out_wdat)

    log.close()
    failed = sum(1 for _, _, ok in RESULTS if not ok)
    total = len(RESULTS)
    print(f"\n=== {total - failed}/{total} scenarios PASS "
          f"({failed} failed) ===", flush=True)
    print(f"full log: {outlog}")
    return failed


if __name__ == "__main__":
    sys.exit(main())
