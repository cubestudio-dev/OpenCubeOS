#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""WP-fix-critical-security: run a list of ush commands against an ISO
and dump each section verbatim (differential triage driver)."""
import os, sys
import pexpect

OC_TOOLS = os.environ.get("OC_TOOLS", "/home/z/opt/extract")
QEMU = f"{OC_TOOLS}/usr/bin/qemu-system-x86_64"

def main():
    iso = sys.argv[1]
    cmds = sys.argv[2:]
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
    for c in cmds:
        child.sendline(c)
        child.expect([r"ush>\s*", pexpect.TIMEOUT], timeout=120)
        out = (child.before or "") + ("\n<<TIMEOUT>>" if isinstance(child.match, type(None)) else "")
        print(f"===== {c} =====")
        print(out[:1200])
        print(f"===== end {c} =====")
    child.close(force=True)

if __name__ == "__main__":
    main()
