#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""WP-fix-critical-security: sys_kill race stress test (BUG #5).

High-frequency kill/fork pressure: spawns background loop tasks, kills
them by pid, interleaves fork_test rounds, and verifies the kernel
never crashes (prompt responsive after every round).
Exit code: 0 = survived all rounds; 1 = kernel crash/timeout."""
import os, sys
import pexpect

OC_TOOLS = os.environ.get("OC_TOOLS", "/home/z/opt/extract")
QEMU = f"{OC_TOOLS}/usr/bin/qemu-system-x86_64"

def main():
    iso = sys.argv[1]
    rounds = int(sys.argv[2]) if len(sys.argv) > 2 else 12
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
        child.expect(r"ush>\s*", timeout=200)
        ok_rounds = 0
        for r in range(rounds):
            # spawn a background victim: fork_test runs a long
            # fork/exit chain (the pre-existing `loop` busy-freeze is
            # BUG #112, batch 6 - not a usable victim here).  Killing
            # it mid-flight hits exactly the reap-vs-tick window.
            # NOTE: with '&' the ush prompt is printed BEFORE the
            # "started pid=N" line (ush does not wait), so after this
            # match ush is parked at its read - the next prompt only
            # appears after the kill command is processed.
            child.sendline("run fork_test &")
            child.expect(r"started pid=(\d+)", timeout=60)
            pid = child.match.group(1)
            # SIGKILL it immediately (the race window is reap-vs-tick)
            child.sendline(f"kill {pid} 9")
            child.expect(r"ush>\s*", timeout=60)
            # churn the scheduler + reaper some more (foreground)
            child.sendline("run fork_test")
            child.expect(r"fork_test: PASS", timeout=120)
            child.expect(r"ush>\s*", timeout=60)
            ok_rounds += 1
            print(f"[killrace] round {r+1}/{rounds} OK (killed pid {pid})")
        # final liveness proof: kernel still answers
        child.sendline("uname -a")
        child.expect(r"Open Cube OS", timeout=60)
        child.expect(r"ush>\s*", timeout=60)
        print(f"[killrace] SURVIVED {ok_rounds}/{rounds} rounds - kernel stable")
        child.close(force=True)
        return 0
    except pexpect.TIMEOUT:
        print(f"\n[killrace] TIMEOUT after {ok_rounds} rounds - kernel "
              f"crashed or froze (BUG #5 race)")
        child.close(force=True)
        return 1

if __name__ == "__main__":
    sys.exit(main())
