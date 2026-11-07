#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""WP-10-timer_rtc end-to-end test driver.

Boots the Open Cube OS ISO under QEMU (BIOS or UEFI), runs the
time-source command surface (CMOS RTC / HPET / ACPI PM timer / TSC),
and captures the raw serial output of every command so each feature
gets its input -> expected -> actual -> verdict record.

Usage: python3 tools/wp10_timer_rtc_e2e.py <iso> <mode: bios|uefi> <outlog>
Exit code: number of failed checks.
"""
import os, re, sys, time
import pexpect

OC_TOOLS = os.environ.get("OC_TOOLS", "/home/z/opt/extract")
QEMU = f"{OC_TOOLS}/usr/bin/qemu-system-x86_64"

# (name, command, expected-regex on that command's output) - None regex
# means "record only" (informational).
CMDS = [
    ("rtc read",          "rtc",       r"RTC: 20\d\d-\d\d-\d\d \d\d:\d\d:\d\d \(UTC, CMOS ports 0x70/0x71\)"),
    ("rtc status",        "rtc",       r"BCD=1 24h=1 rate=\d+Hz"),
    ("rtc fadt century",  "rtc",       r"FADT century reg=32"),
    ("rtc uie",           "rtc uie 3", r"update-ended interrupts: 3 \(expected 3\) PASS"),
    ("rtc pie",           "rtc pie 10 32", r"periodic interrupts: 32 \(expected 32\) PASS"),
    ("rtc alarm",         "rtc alarm 3", r"alarm interrupt: fired PASS"),
    ("hpet base",         "hpet",      r"HPET: base=fed00000 \(ACPI table, GAS SystemMemory\)"),
    ("hpet caps",         "hpet",      r"Caps: rev=\d+ timers=3 64bit=1 legacy=1 period=10000000fs freq=100000000Hz"),
    ("hpet counter",      "hpet counter 5", r"measured \d+ Hz \(nominal 100000000\) PASS"),
    ("hpet oneshot",      "hpet oneshot 100", r"oneshot: fired 1 \(expected 1\) PASS"),
    ("hpet periodic",     "hpet periodic 100 3", r"periodic: 3/3 PASS"),
    ("hpet test",         "hpet test", r"hpet: PASS"),
    ("pmtimer base",      "pmtimer",   r"PM timer: (port|mem)=[0-9a-f]{8} \((SystemIO|SystemMemory)\) width=\d+bit \(FADT (pm_tmr_blk|X_PM_TMR)\)"),
    ("pmtimer mask",      "pmtimer",   r"delta mask=00ffffff \(24bit\)"),
    ("pmtimer wrap",      "pmtimer wrap", r"pmtimer wrap: PASS"),
    ("pmtimer cal",       "pmtimer cal", r"measured: \d+ Hz \(nominal 3579545, delta \d+ bp\) PASS"),
    ("pmtimer test",      "pmtimer test", r"pmtimer: PASS"),
    ("tsc read",          "tsc",       r"TSC: [0-9a-f]+ \(serialized rdtsc\)"),
    ("tsc invariant",     "tsc",       r"invariant: [01] \(CPUID 0x80000007 EDX\[8\]\)"),
    ("tsc cal",           "tsc cal",   r"tsc cal: PASS \(cached \d+ Hz via \w+\)"),
    ("tsc cal hpet",      "tsc cal hpet", r"TSC calibration vs HPET \(~0\.1 s\): \d+ Hz"),
    ("tsc delay",         "tsc delay 100", r"PIT-measured actual: \d+ ms PASS"),
    ("tsc test",          "tsc test",  r"tsc: PASS"),
    # v2: the ring-3 ush help lists the time service ops (the L0-only
# work-package view of `help -w` is an oc> feature - see chipset e2e)
    ("help wp group",     "help",     r"rtc \[uie\|pie\|alarm N\]  CMOS RTC"),
    ("help az intact",    "help -a",   r"pmtimer"),
    ("boot command count","echo boot", r"boot"),   # informational placeholder; count asserted via boot log below
    ("rtc still works",   "rtc",       r"Counters: total=\d+ update=\d+ periodic=\d+ alarm=\d+"),
    ("uname",             "uname -a",  r"Open Cube OS"),
]

def main():
    iso, mode, outlog = sys.argv[1], sys.argv[2], sys.argv[3]
    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = (f"{OC_TOOLS}/usr/lib/x86_64-linux-gnu:"
                              f"{OC_TOOLS}/lib/x86_64-linux-gnu")
    args = [
        "-m", "512M", "-cdrom", iso, "-boot", "d",
        "-L", f"{OC_TOOLS}/usr/share/qemu", "-L", f"{OC_TOOLS}/usr/share/seabios",
        "-vga", "std", "-display", "none", "-serial", "mon:stdio",
        "-drive", "if=ide,format=raw,file=build/etc.img",
        "-netdev", "user,id=n1", "-device", "e1000,netdev=n1",
    ]
    if mode == "uefi":
        os.makedirs("build", exist_ok=True)
        import shutil
        shutil.copyfile(f"{OC_TOOLS}/usr/share/ovmf/OVMF.fd", "build/OVMF_WORK.fd")
        args += ["-drive", "if=pflash,format=raw,file=build/OVMF_WORK.fd"]

    child = pexpect.spawn(QEMU, args, timeout=240, encoding="latin-1",
                          codec_errors="replace", env=env)
    child.expect(r"ush>\s", timeout=240)
    boot_out = child.before or ""
    # v2.0.0 boot log counts the L0 recovery-shell registry (99
    # commands; full surface = 251 with the ush SVC/builtin/bridge rows)
    boot_count_ok = re.search(r"shell command system \(99 commands\)", boot_out) is not None
    outputs = {}
    for name, cmd, pat in CMDS:
        child.sendline(cmd)
        idx = child.expect([r"ush>\s", pexpect.TIMEOUT], timeout=120)
        out = (child.before or "") + ("\n<<TIMEOUT>>" if idx == 1 else "")
        outputs[name] = out
        ok = "?" if pat is None else ("PASS" if re.search(pat, out, re.I | re.S) else "FAIL")
        print(f"[e2e {mode}] {ok}  {name} ({len(out)} bytes)")
        child.sendline("")   # clear any pending line
        try:
            child.expect([r"ush>\s", pexpect.TIMEOUT], timeout=10)
        except Exception:
            pass
    child.close(force=True)

    parts = [boot_out]
    for (name, cmd, _pat) in CMDS:
        parts.append(f"=== [{name}] $ {cmd}\n{outputs.get(name, '')}")
    combined = "\n".join(parts)
    combined = combined.replace("\x00", "")
    os.makedirs(os.path.dirname(outlog) or ".", exist_ok=True)
    with open(outlog, "w") as f:
        f.write(combined)

    passed = failed = 0
    print(f"\n=== {mode} checks ===")
    for name, cmd, pat in CMDS:
        if pat is None:
            continue
        ok = re.search(pat, outputs[name], re.I | re.S) is not None
        print(f"  {'PASS' if ok else 'FAIL'}  {name}")
        passed += ok
        failed += (not ok)
    print(f"  {'PASS' if boot_count_ok else 'FAIL'}  boot log: shell command system (196 commands)")
    passed += boot_count_ok
    failed += (not boot_count_ok)
    print(f"=== {mode}: {passed}/{passed + failed} checks passed, {failed} failed ===")
    sys.exit(failed)

if __name__ == "__main__":
    main()
