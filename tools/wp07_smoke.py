#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""WP-07 smoke: boot the recovery disk's own OCBL (hd1) and watch the
dual-slot scan + OCIRE bring-up over the serial console."""
import os
import sys
import pexpect

OC_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
OC = os.environ.get("OC_TOOLS", "/home/z/opt/extract")
QEMU = os.path.join(OC, "usr/bin/qemu-system-x86_64")
ENV = dict(os.environ)
ENV["LD_LIBRARY_PATH"] = (
    os.path.join(OC, "usr/lib/x86_64-linux-gnu") + ":" +
    os.path.join(OC, "lib/x86_64-linux-gnu") + ":" +
    ENV.get("LD_LIBRARY_PATH", ""))

cmd = ("{qemu} -m 512 -L {qdata} -L {sea} "
       "-drive file={root}/build/ocire/ocire-hd.img,if=ide,index=0,media=disk,format=raw "
       "-drive file={root}/build/ocire/ocire-boot.img,if=ide,index=1,media=disk,format=raw "
       "-drive file={root}/build/ocire/esp.img,if=ide,index=2,media=disk,format=raw "
       "-net none -serial stdio -display none -no-reboot"
       ).format(qemu=QEMU, root=OC_ROOT,
                qdata=os.path.join(OC, "usr/share/qemu"),
                sea=os.path.join(OC, "usr/share/seabios"))

child = pexpect.spawn(cmd, env=ENV, timeout=90, encoding="utf-8",
                      codec_errors="replace")
log = open(os.path.join(OC_ROOT, "build/ocire/wp07-smoke.log"), "w")

try:
    child.logfile = log
    # OCBL banner + scan
    child.expect("OCBL 3.0.0 boot loader")
    print("[smoke] OCBL banner OK")
    child.expect(r"\[scan\] entries: (\d+)")
    n = int(child.match.group(1))
    print("[smoke] scan entries: %d" % n)
    child.expect("OCIRE recovery")
    print("[smoke] OCIRE entry seen")
    # the menu defaults to OCOS with a 10 s countdown: move UP to the
    # first entry (OCIRE slot A) and press Enter during the countdown
    child.expect("Booting Open Cube OS", timeout=30)
    child.send("\x1b[A")          # Up arrow
    child.expect("OCIRE recovery \\(slot A", timeout=15)
    child.send("\r")               # Enter
    print("[smoke] selected OCIRE slot A in the menu")
    # OCIRE boots from the FAT32 slot partition
    child.expect("OCIRE - OpenCubeOS Independent Recovery Environment", timeout=120)
    print("[smoke] OCIRE kernel booted (FAT32 slot load)")
    child.expect("Ready. Type 'help' for the command list", timeout=60)
    print("[smoke] shell ready")
    child.sendline("slots")
    child.expect(r"slot A: LBA (\d+)")
    print("[smoke] slots cmd: A at LBA %s" % child.match.group(1))
    child.expect("running from here")
    print("[smoke] running from slot A confirmed")
    child.sendline("ls")
    child.expect("ESP: ata")
    print("[smoke] ESP mounted")
    child.expect("EFI")
    child.expect("entr")
    print("[smoke] ESP root listed")
    child.sendline("shutdown")
    print("[smoke] PASS - sending shutdown")
    try:
        child.expect(pexpect.EOF, timeout=30)
    except pexpect.TIMEOUT:
        pass
    rc = 0
except Exception as e:
    print("[smoke] FAIL: %s" % e)
    rc = 1
finally:
    log.close()
    if child.isalive():
        child.terminate(force=True)
sys.exit(rc)
