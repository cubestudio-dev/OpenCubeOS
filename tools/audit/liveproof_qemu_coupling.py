#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""Live verification for the QEMU-coupling inventory (WP-docs-cleanup).

Boots the release ISO WITH the coupling-relevant devices attached and
captures the exact console lines that prove four audited entries fire
in the running system:

  A1  real_hw_test     - the CPUID hypervisor-bit branch
  A5  virtio_blk_test  - the 1AF4:1004 PCI-ID match path
  B2  e1000 legacy     - the QEMU-model register ordering (init path)
  B1  i6300esb         - the watchdog BAR0/probe path (boot + status)

Output: the captured transcript (verbatim) on stdout.
"""
import os
import re
import sys
import time

import pexpect

OC = os.environ.get("OC_TOOLS", os.path.expanduser("~/opt/extract"))
QEMU = os.path.join(OC, "usr", "bin", "qemu-system-x86_64")
QDATA = os.path.join(OC, "usr", "share", "qemu")
SEABIOS = os.path.join(OC, "usr", "share", "seabios")
LIB = ":".join([os.path.join(OC, "usr", "lib", "x86_64-linux-gnu"),
                os.path.join(OC, "lib", "x86_64-linux-gnu")])

VDISK = "/tmp/oc-audit-vdisk.raw"


def quiesce(child, lull=2.0, cap=30):
    buf = []
    deadline = time.time() + cap
    while time.time() < deadline:
        try:
            i = child.expect([".+", pexpect.TIMEOUT], timeout=lull)
            if i == 0:
                buf.append(child.after)
                continue
            break
        except (pexpect.EOF, pexpect.TIMEOUT):
            break
    return "".join(buf)


def main(iso):
    if not os.path.exists(VDISK):
        with open(VDISK, "wb") as f:
            f.truncate(16 * 1024 * 1024)
    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = LIB
    cmd = ("{q} -m 512 -L {qd} -L {sb} -cdrom {iso} -boot d "
           "-drive file={vd},if=virtio,format=raw "
           "-device e1000 "
           "-device i6300esb "
           "-net none -serial stdio -display none -no-reboot"
           ).format(q=QEMU, qd=QDATA, sb=SEABIOS, iso=iso, vd=VDISK)
    child = pexpect.spawn(cmd, env=env, timeout=240,
                          encoding="latin-1", codec_errors="replace")
    log = open("/tmp/oc-audit-liveproof.log", "w")
    child.logfile = log
    try:
        child.expect(r"ush>\s", timeout=240)
        print("=== boot OK (virtio-blk + e1000 + i6300esb attached) ===")

        def run(cmdline, lull=2.0, cap=40):
            child.sendline(cmdline)
            out = quiesce(child, lull=lull, cap=cap)
            child.sendline("echo AUDPROBE")
            try:
                child.expect("AUDPROBE", timeout=10)
                child.expect(r"ush>\s", timeout=6)
            except (pexpect.EOF, pexpect.TIMEOUT):
                print("!! probe failed after %r" % cmdline)
            return out

        print("=== A1: real_hw_test (CPUID hypervisor branch) ===")
        out = run("real_hw_test", lull=2, cap=30)
        print(out)

        print("=== A5: virtio_blk_test (1AF4:1004 ID match) ===")
        out = run("virtio_blk_test", lull=2, cap=40)
        print(out)

        print("=== B2: e1000 (legacy NIC init path) ===")
        out = run("e1000e", lull=2, cap=20)
        print(out)
        out = run("ifconfig", lull=2, cap=20)
        print(out)

        print("=== B1: watchdog (i6300esb status) ===")
        out = run("watchdog", lull=2, cap=20)
        print(out)
        out = run("dmesg", lull=2, cap=20)
        print(out)
        return 0
    finally:
        log.close()
        child.terminate(force=True)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
