#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 -*-
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""v2.0.0 group-B gate: boot the new declaration-collected kernel in QEMU
(BIOS), verify the command surface is unchanged (233 lines in help -a,
the registry collection log fired, and the service-group view works).
Usage: python3 tools/v2_registry_gate.py <iso>"""
import os, re, sys
import pexpect

OC_TOOLS = os.environ.get("OC_TOOLS", "/home/z/opt/extract")
QEMU = f"{OC_TOOLS}/usr/bin/qemu-system-x86_64"

def main():
    iso = sys.argv[1]
    child = pexpect.spawn(QEMU, [
        "-m", "512M", "-cdrom", iso, "-boot", "d",
        "-L", f"{OC_TOOLS}/usr/share/qemu",
        "-L", f"{OC_TOOLS}/usr/share/seabios",
        "-display", "none", "-serial", "stdio",
    ], timeout=240, encoding="utf-8", codec_errors="replace")
    out = ""
    idx = child.expect([r"oc>\s*", pexpect.TIMEOUT], timeout=200)
    out += child.before or ""
    if idx != 0:
        print("FAIL: no shell prompt")
        child.close(force=True)
        return 1
    child.sendline("help -a")
    idx = child.expect([r"oc>\s*", pexpect.TIMEOUT], timeout=60)
    out += child.before or ""
    child.sendline("help -w")
    idx = child.expect([r"oc>\s*", pexpect.TIMEOUT], timeout=60)
    out += child.before or ""
    child.sendline("help")
    idx = child.expect([r"oc>\s*", pexpect.TIMEOUT], timeout=60)
    out += child.before or ""
    child.close(force=True)

    open("/tmp/vcheck/v2_gate_out.txt", "w").write(out)

    fails = 0
    # 1. boot + collection log
    if "registry: file-local declarations collected" not in out:
        print("FAIL: registry collection log line missing")
        fails += 1
    else:
        print("PASS: registry collection log present")
    # 2. command count from help -a (anchored section; mkfs.* carries a dot)
    i = out.find("Available commands (A-Z):")
    j = out.find("Commands by work package:")
    if i < 0 or j < 0 or j <= i:
        print("FAIL: help -a/-w anchors not captured")
        fails += 1
    else:
        cmds = re.findall(r'^  ([a-z0-9][a-z0-9_.-]*)\s+-\s+\S', out[i:j], re.M)
        print(f"help -a command lines: {len(cmds)}")
        if len(cmds) != 231:  # 233 regs - health export/watch subcommands
            print(f"FAIL: expected 231 lines, got {len(cmds)}")
            fails += 1
        else:
            print("PASS: command surface unchanged (231 lines / 233 commands)")
    # 2b. total from help -w footer
    if "Total: 233 commands." not in out:
        print("FAIL: help -w footer 'Total: 233 commands.' missing")
        fails += 1
    else:
        print("PASS: help -w total = 233")
    # 3. service-group view present
    if "recovery" not in out or "watchdog" not in out or "=== l1 (" not in out:
        print("FAIL: service-group view (help -w) missing category rows")
        fails += 1
    else:
        print("PASS: help -w service-group view present (incl. l1)")
    # 4. spot commands from different groups
    if i >= 0 and j > i:
        for probe in ("uname", "crashlog", "usb", "tui", "ping", "mkfs.fat32"):
            if not re.search(rf'^  {probe}\s+-\s+\S', out[i:j], re.M):
                print(f"FAIL: command '{probe}' missing from help -a")
                fails += 1
    print("RESULT:", "PASS" if fails == 0 else f"FAIL ({fails})")
    return fails

if __name__ == "__main__":
    sys.exit(main())
