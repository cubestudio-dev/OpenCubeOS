#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""WP-fix-critical-security: P0 pre/post-fix evidence driver.

Boots the ISO, runs the p0leak ring-3 probe, and reports whether the
kernel-address sys_write/sys_read probes were fenced (-1) or leaked.
Exit code 0 = all probes fenced; 1 = leak observed; 2 = boot failure.
"""
import os, sys
import pexpect

OC_TOOLS = os.environ.get("OC_TOOLS", "/home/z/opt/extract")
QEMU = f"{OC_TOOLS}/usr/bin/qemu-system-x86_64"

def main():
    iso = sys.argv[1]
    tag = sys.argv[2] if len(sys.argv) > 2 else "run"
    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = (f"{OC_TOOLS}/usr/lib/x86_64-linux-gnu:"
                              f"{OC_TOOLS}/lib/x86_64-linux-gnu")
    child = pexpect.spawn(QEMU, [
        "-m","512M","-cdrom",iso,"-boot","d",
        "-L",f"{OC_TOOLS}/usr/share/qemu","-L",f"{OC_TOOLS}/usr/share/seabios",
        "-vga","std","-display","none","-serial","mon:stdio",
        "-netdev","user,id=n1","-device","e1000,netdev=n1",
    ], timeout=200, encoding="latin-1", codec_errors="replace", env=env)
    child.logfile = sys.stdout
    try:
        child.expect(r"ush>\s*", timeout=200)
        child.sendline("run p0leak")
        child.expect(r"p0leak: (PASS - .*|BUG - .*)", timeout=120)
        out = child.match.group(0)
        # Collect the full test output until the prompt returns.
        child.expect(r"ush>\s*", timeout=60)
    except pexpect.TIMEOUT:
        print(f"\n[{tag}] TIMEOUT - no verdict from p0leak")
        child.close(force=True)
        return 2
    child.close(force=True)
    full = out
    if "PASS" in full and "BUG -" not in full:
        print(f"\n[{tag}] VERDICT: fenced (PASS)")
        return 0
    print(f"\n[{tag}] VERDICT: LEAK (BUG present)")
    return 1

if __name__ == "__main__":
    sys.exit(main())
