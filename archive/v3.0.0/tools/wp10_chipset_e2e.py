#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""WP-10-input_chipset end-to-end test driver.

Boots the Open Cube OS ISO under QEMU (BIOS or UEFI), runs the
chipset/input command surface, and captures the raw serial output of
every command so each feature gets its input -> expected -> actual ->
verdict record.

Usage: python3 tools/wp10_chipset_e2e.py <iso> <mode: bios|uefi> <outlog>
Exit code: number of failed checks.
"""
import os, re, sys, time
import pexpect
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu_runner import boot_to_recovery

OC_TOOLS = os.environ.get("OC_TOOLS", "/home/z/opt/extract")
QEMU = f"{OC_TOOLS}/usr/bin/qemu-system-x86_64"

# (name, command, expected-regex on that command's output) - None regex
# means "record only" (informational).
CMDS = [
    ("acpi tables",        "acpi",       r"RSDP: 0x[0-9a-f]+ rev"),
    ("acpi FADT/PM regs",  "acpi",       r"FADT: rev \d+ SCI=IRQ\d+ PM1a_CNT=0x[0-9a-f]+"),
    ("acpi MCFG or legacy","acpi",       r"MCFG:"),
    ("acpi devices",       "acpidev",    r"AML namespace: nodes=\d+ methods=\d+ devices=\d+"),
    ("acpi device list",   "acpidev",    r"\\_SB_"),
    ("acpitest",           "acpitest",   r"acpitest: PASS"),
    ("acpipow states",     "acpipow",    r"sleep states offered: 0x[0-9a-f]+"),
    ("acpipow S5",         "acpipow",    r"  S5: SLP_TYP=0x[0-9a-f]+"),
    ("acpipow sci",        "acpipow",    r"SCI: IRQ \d+"),
    ("msitest lapic",      "msitest",    r"self-IPI vector 0x50: fired \d+ time\(s\)\s+PASS"),
    ("msitest caps",       "msitest",    r"MSI-capable devices: \d+"),
    ("pcibars",            "pcibars",    r"vid=0x[0-9a-f]+ did=0x"),
    ("pcibars BAR sizes",  "pcibars",    r"BAR\d: base=0x[0-9a-f]+ size=0x[0-9a-f]+"),
    ("pcie caps",          "pcie",       r"capabilities per device:"),
    ("pcidump",            "pcidump 0:1.3", r"config dump 00:01\.3|config dump 0?1\.3|config dump"),
    ("i2ctest",            "i2ctest",    r"i2ctest: PASS"),
    ("i2cdetect scan",     "i2cdetect",  r"i2c-0 \("),
    ("i2cdetect eeprom",   "i2cdetect",  r" 50 "),
    ("smbus devices",      "smbus",      r"SMBus devices: [1-9]"),
    ("smbus eeprom",       "smbus",      r"EEPROM"),
    ("smbustest",          "smbustest",  r"smbustest: byte/word/block round-trips\s+PASS"),
    ("i2cget eeprom",      "i2cget 0 0x50 0x00", r"i2cget 0 0x50 0x00\s*\n0x[0-9a-f]{2}\s*\n"),
    # v2.0.0: help -w groups follow the new L0 layout (recovery tree
    # groups: core/input/fs/...).  The chipset commands live in 'input'.
    ("help intact",        "help -w",    r"=== input \("),
    ("help az intact",     "help -a",    r"acpipow"),
]

# v2.0.0 ring-3 surface: these moved out of the recovery shell into the
# ush SVC table - they are exercised right after boot, inside ush>.
USH_CMDS = [
    ("touchtest",          "touchtest",  r"touchtest: descriptor parse \+ TAP/SCROLL/PINCH/SWIPE\s+PASS|TAP/SCROLL/PINCH/SWIPE\s+PASS"),
    ("touchpad status",    "touchpad",   r"touchpad:|Touchpad"),
    ("lspci still works",  "lspci",      r"vendor="),
    ("uname",              "uname -a",   r"Open Cube OS"),
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
        # the pc machine pre-plugs 8 smbus-eeprom devices (0x50-0x57) on
        # its internal "i2c" SMBus - no extra devices needed for the
        # SMBus scan / protocol tests.
    ]
    if mode == "uefi":
        os.makedirs("build", exist_ok=True)
        import shutil
        shutil.copyfile(f"{OC_TOOLS}/usr/share/ovmf/OVMF.fd", "build/OVMF_WORK.fd")
        args += ["-drive", "if=pflash,format=raw,file=build/OVMF_WORK.fd"]

    child = pexpect.spawn(QEMU, args, timeout=240, encoding="latin-1",
                          codec_errors="replace", env=env)
    outputs = {}

    # ---- v2 segment 1: ring-3 ush shell (straight after boot) ----
    i = child.expect([r"ush>\s", pexpect.TIMEOUT, pexpect.EOF], timeout=240)
    if i != 0:
        outputs["(boot)"] = "\n<<NO-USH-PROMPT>>"
    else:
        for name, cmd, pat in USH_CMDS:
            child.sendline(cmd)
            idx = child.expect([r"ush>\s", pexpect.TIMEOUT], timeout=90)
            out = (child.before or "") + ("\n<<TIMEOUT>>" if idx == 1 else "")
            outputs[name] = out
            ok = "?" if pat is None else ("PASS" if re.search(pat, out, re.I | re.S) else "FAIL")
            print(f"[e2e {mode}] {ok}  {name} ({len(out)} bytes)")

    # ---- v2 segment 2: leave ush, recovery shell at oc> ----
    # (manual exit: the ush banner was already consumed by segment 1, so
    # the shared boot_to_recovery() adapter - which keys on the fresh-boot
    # stream - cannot be reused here)
    child.sendline("exit")
    child.expect(r"Type 'help' for commands", timeout=60)
    child.expect(r"oc>\s", timeout=30)
    boot_out = child.before or ""
    for name, cmd, pat in CMDS:
        child.sendline(cmd)
        idx = child.expect([r"oc>\s", pexpect.TIMEOUT], timeout=90)
        out = (child.before or "") + ("\n<<TIMEOUT>>" if idx == 1 else "")
        outputs[name] = out
        ok = "?" if pat is None else ("PASS" if re.search(pat, out, re.I | re.S) else "FAIL")
        print(f"[e2e {mode}] {ok}  {name} ({len(out)} bytes)")
    child.close(force=True)

    parts = [boot_out]
    for (name, cmd, _pat) in USH_CMDS + CMDS:
        parts.append(f"=== [{name}] $ {cmd}\n{outputs.get(name, '')}")
    combined = "\n".join(parts)
    combined = combined.replace("\x00", "")
    os.makedirs(os.path.dirname(outlog) or ".", exist_ok=True)
    with open(outlog, "w") as f:
        f.write(combined)

    passed = failed = 0
    print(f"\n=== {mode} checks ===")
    for name, cmd, pat in USH_CMDS + CMDS:
        if pat is None:
            continue
        if name not in outputs:
            failed += 1
            print(f"  FAIL  {name} (no output)")
            continue
        ok = re.search(pat, outputs[name], re.I | re.S) is not None
        print(f"  {'PASS' if ok else 'FAIL'}  {name}")
        passed += ok
        failed += (not ok)
    print(f"=== {mode}: {passed}/{passed + failed} checks passed, {failed} failed ===")
    sys.exit(failed)

if __name__ == "__main__":
    main()
