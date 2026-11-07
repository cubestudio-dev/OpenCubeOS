#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - 3.0.0-WP-01-ocire-core
# File: tools/run_ocire_dual_tests.py
# Purpose: WP-01 dual-boot + partition-isolation test battery.
#
# Disk layout under test (built by tools/make_ocire_disk.sh):
#   hd0 ocire-boot.img - GRUB dual-menu disk (p1 FAT32: OCOS + OCIRE elfs)
#   hd1 ocire-hd.img   - OCIRE disk: single 0xEE "GPT protective shadow"
#                       partition, FAT12 content, NO GPT header
#
# Cases:
#   dual_bios_ocos   - menu default (timeout) boots OCOS from disk  -> "ush>"
#   dual_bios_ocire  - menu keypress selects OCIRE                   -> "ocire>"
#   dual_uefi_ocos   - same under OVMF                               -> "ush>"
#   dual_uefi_ocire  - same under OVMF                               -> "ocire>"
#   isolation        - OCOS from CD with both disks: partscan must
#                      register hd0's p1 but NOTHING for the OCIRE
#                      disk; parted hdb / mkfs.fat32 hdbp1 /
#                      disk_rw_test hdbp1 must all fail - the OCIRE
#                      partition has no device node and no fs path.
#   isolation_rawdecl- honest-declaration probe on a THROWAWAY copy
#                      of the OCIRE disk: whole-disk device nodes
#                      still exist (disk_rw_test hdb, mkfs.fat32 hdb),
#                      exactly like Linux root with /dev/sdb - the
#                      partition-level isolation is measured first,
#                      this documents the remaining whole-disk surface
#                      (protection for it belongs to WP-03).
#
# QEMU menu selection uses the monitor (telnet) sendkey, because the
# GRUB menu takes input from the console only (serial input is the
# kernels' service channel).

import os
import shutil
import socket
import sys
import time
import pexpect

OC_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
OC = os.environ.get("OC_TOOLS", "/home/z/opt/extract")
QEMU = os.path.join(OC, "usr/bin/qemu-system-x86_64")
QDATA = os.path.join(OC, "usr/share/qemu")
SEA = os.path.join(OC, "usr/share/seabios")
OVMF = os.path.join(OC, "usr/share/ovmf/OVMF.fd")
HD0 = os.path.join(OC_ROOT, "build/ocire/ocire-boot.img")
HD1 = os.path.join(OC_ROOT, "build/ocire/ocire-hd.img")
OCOS_ISO = os.path.join(OC_ROOT, "build/opencube.iso")
OUT = os.path.join(OC_ROOT, "build/ocire")

ENV = dict(os.environ)
ENV["LD_LIBRARY_PATH"] = (
    os.path.join(OC, "usr/lib/x86_64-linux-gnu") + ":"
    + os.path.join(OC, "lib/x86_64-linux-gnu") + ":"
    + os.path.join(OC, "usr/lib"))

RESULTS = []


def record(case, ok, note=""):
    RESULTS.append((case, ok, note))
    print(f"[{'PASS' if ok else 'FAIL'}] {case}" + (f" - {note}" if note else ""))


def spawn_disks(uefi, disk0, disk1, with_cd, mon_port, log_path, timeout):
    """Boot with the given disks on IDE index 0/1 (plus optional OCOS CD).

    The isolation cases pass the OCIRE disk as disk0: the OCOS ISO's
    GRUB probes (hd0,msdos1) for its A/B layout, and the OCIRE disk's
    0xEE shadow entry makes GRUB skip that partition - so the probe
    fails and OCOS boots from the CD's ISO slot, leaving hda = the
    OCIRE disk (isolation target) and hdb = the dual-boot disk
    (control group with a normal, visible partition table).
    """
    if uefi:
        work = os.path.join(OUT, "OVMF_DUAL.fd")
        shutil.copyfile(OVMF, work)
        pflash = ["-drive", f"if=pflash,format=raw,file={work}"]
    else:
        pflash = []
    args = [QEMU, "-L", QDATA, "-L", SEA,
            "-m", "256M",
            "-drive", f"if=ide,index=0,format=raw,file={disk0}",
            "-drive", f"if=ide,index=1,format=raw,file={disk1}",
            "-no-reboot", "-display", "none", "-serial", "stdio",
            "-monitor", f"telnet:127.0.0.1:{mon_port},server,nowait",
            "-vga", "std"] + pflash
    if with_cd:
        args += ["-cdrom", OCOS_ISO, "-boot", "d"]
    else:
        args += ["-boot", "c"]
    child = pexpect.spawn(args[0], args[1:], timeout=timeout,
                          encoding="utf-8", codec_errors="replace", env=ENV)
    child.logfile = open(log_path, "w")
    return child


def monitor_send(port, keys, wait=0.4):
    """Send QEMU monitor 'sendkey' commands over the telnet monitor."""
    s = socket.create_connection(("127.0.0.1", port), timeout=5)
    time.sleep(0.3)
    s.recv(4096)                       # banner + prompt
    for k in keys:
        s.sendall(f"sendkey {k}\n".encode())
        time.sleep(wait)
    s.close()


def case_dual(uefi, pick_ocire):
    tag = "uefi" if uefi else "bios"
    target = "OCIRE" if pick_ocire else "OCOS"
    case = f"dual_{tag}_{target.lower()}"
    port = 41000 + len(RESULTS)
    # WP-02: the menu default is the BOOT SUPERVISOR, which is STATEFUL
    # - it writes WAIT_BOOT_SIGNAL into the hd1 mailbox and (without
    # the WP-03 OCOS-side signal) the NEXT supervised boot degrades
    # into the recovery path by design.  Each dual case therefore
    # boots THROWAWAY copies so it starts from a fresh (NONE)
    # mailbox, exactly like the shell battery.
    d0 = os.path.join(OUT, f"dual-throw0-{case}.img")
    d1 = os.path.join(OUT, f"dual-throw1-{case}.img")
    shutil.copyfile(HD0, d0)
    shutil.copyfile(HD1, d1)
    child = spawn_disks(uefi, d0, d1, with_cd=False, mon_port=port,
                        log_path=os.path.join(OUT, f"dual-{case}.log"),
                        timeout=180)
    try:
        # Wait for the GRUB menu to render on serial.  (The menu-entry
        # text changed with the WP-02 3-entry menu; match the banner.)
        i = child.expect(["GNU GRUB", pexpect.TIMEOUT])
        if i != 0:
            record(case, False, "GRUB menu never appeared on serial")
            return False
        if pick_ocire:
            # GRUB menus have no digit hotkeys; the serial terminal is a
            # VT100, so speak ANSI: cursor-down (cancels the timeout and
            # moves the highlight), then CR to boot entry 2.
            child.send("\x1b[B")
            child.expect(["OCIRE", pexpect.TIMEOUT], timeout=10)
            time.sleep(1.0)
            child.send("\r")
            i = child.expect(["ocire>", pexpect.TIMEOUT])
            record(case, i == 0,
                   "OCIRE prompt" if i == 0 else "no OCIRE prompt after menu pick")
        else:
            # default entry 0 = OCOS, let the 8 s timeout run
            i = child.expect(["ush>", pexpect.TIMEOUT])
            record(case, i == 0,
                   "OCOS prompt" if i == 0 else "no ush prompt (default entry)")
        return i == 0
    finally:
        child.logfile.close()
        child.close(force=True)


def case_isolation(honest_raw, uefi=False):
    """Boot OCOS from CD with both disks and probe the isolation."""
    case = "isolation_rawdecl" if honest_raw else "isolation"
    ocire_disk = HD1
    if honest_raw:
        ocire_disk = os.path.join(OUT, "ocire-hd-throwaway.img")
        shutil.copyfile(HD1, ocire_disk)  # the raw probes destroy this copy
    port = 41000 + len(RESULTS)
    # disk0 = OCIRE disk (hda, the isolation target), disk1 = dual-boot
    # disk (hdb, the control group).  See spawn_disks for why this order
    # keeps the OCOS ISO GRUB on its ISO slot.
    child = spawn_disks(uefi, ocire_disk, HD0, with_cd=True, mon_port=port,
                        log_path=os.path.join(OUT, f"{case}.log"),
                        timeout=240)
    try:
        # partscan evidence line appears during boot; then the ush prompt.
        i = child.expect(["ush>", pexpect.TIMEOUT])
        if i != 0:
            record(case, False, "OCOS never reached the ush prompt")
            return False

        def run(cmd, want, sub, timeout=60):
            child.sendline(cmd)
            j = child.expect([want, pexpect.TIMEOUT], timeout=timeout)
            record(f"{case}:{sub}", j == 0,
                   "" if j == 0 else f"{cmd!r} did not print {want!r}")
            return j == 0

        ok_all = True
        if not honest_raw:
            # 1. control group: the dual-boot disk table IS visible
            ok_all &= run("parted hdb", r"Partition table \(MBR\)", "parted-hdb-control")
            # 2. the OCIRE disk has no partition table OCOS can parse
            ok_all &= run("parted hda", "no partition table found",
                          "parted-hda-refused")
            # 3. no device node exists for the OCIRE partition
            ok_all &= run("mkfs.fat32 hdap1 confirm", "device not found",
                          "mkfs-hdap1-refused")
            # 4. no raw device node either
            ok_all &= run("disk_rw_test hdap1", "not found",
                          "diskrw-hdap1-refused")
            # 5. mounted volumes: only OCOS volumes, nothing from hd1
            child.sendline("df")
            j = child.expect([r"Filesystem|\bFS\b|/etc|Type", pexpect.TIMEOUT])
            df_dump = child.before or ""
            record(f"{case}:df-captured", j == 0)
        else:
            # honest declaration probes (throwaway copy): whole-disk
            # device nodes still exist, like Linux root with /dev/sdb
            ok_all &= run("disk_rw_test hda", r"passed|PASS|OK",
                          "rawdecl-diskrw-hda-whole", timeout=90)
            ok_all &= run("mkfs.fat32 hda confirm", "FAT32 formatted",
                          "rawdecl-mkfs-hda-whole", timeout=90)
        return ok_all
    finally:
        child.logfile.close()
        child.close(force=True)


def main():
    for path in (HD0, HD1, OCOS_ISO):
        if not os.path.exists(path):
            print(f"FATAL: {path} missing (make iso ocire-disk)", file=sys.stderr)
            return 1
    case_dual(False, pick_ocire=False)   # BIOS menu default -> OCOS
    case_dual(False, pick_ocire=True)    # BIOS menu pick   -> OCIRE
    case_dual(True,  pick_ocire=False)   # UEFI menu default -> OCOS
    case_dual(True,  pick_ocire=True)    # UEFI menu pick   -> OCIRE
    case_isolation(honest_raw=False)     # partition-level isolation
    case_isolation(honest_raw=True)      # whole-disk surface (declared)

    fails = [r for r in RESULTS if not r[1]]
    print("\n===== OCIRE dual-boot + isolation summary =====")
    print(f"total cases: {len(RESULTS)}, passed: {len(RESULTS)-len(fails)}, "
          f"failed: {len(fails)}")
    for case, ok, note in fails:
        print(f"  FAIL: {case} {note}")
    print(f"logs: {OUT}/dual-*.log, {OUT}/isolation*.log")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
