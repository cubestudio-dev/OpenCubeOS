#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""WP-10-camera_misc E2E: boots the ISO in QEMU and exercises the
five new domains against live hardware models:

  UVC      uvctest 30/30 (descriptor parse, stream control, format
           enumeration, frame capture, terminal controls) + the
           honest zero-camera `uvc` status
  parallel paralleltest 10/10 with the SPP bytes verified in the
           chardev file (-parallel file:)
  gameport gameporttest 6/6 (axis reads, buttons, calibration)
  ir       irtest 14/14 (NEC/RC5/RC6/SIRC round trips + live SIR)
  scsi     scsitest against a real LSI 53C895A HBA + scsi-hd
           (-device lsi), including the CDB golden checks, the
           INQUIRY/capacity live reads, the RW round trip on the
           last sector of the dedicated test disk and the blk-layer
           hookup; `scsi` lists the target honestly

Usage: python3 tools/wp10_camera_misc_e2e.py <iso> <bios|uefi> <outlog>
Exit code: number of failed checks.
"""
import os
import re
import subprocess
import sys

import pexpect

OC_TOOLS = os.environ.get("OC_TOOLS", "/home/z/opt/extract")
QEMU = f"{OC_TOOLS}/usr/bin/qemu-system-x86_64"
BOOT_COUNT = 208   # 198 after wireless_bluetooth + 10 camera_misc cmds

CHECKS = [
    # (name, regex-on-combined-output)
    ("uvc honest zero cameras",  r"camera subsystem: 0 cameras"),
    ("uvctest all pass",         r"uvctest: 0 failures - PASS"),
    ("paralleltest all pass",    r"paralleltest: 0 failures - PASS"),
    ("gameporttest all pass",    r"gameporttest: 0 failures - PASS"),
    ("irtest all pass",          r"irtest: 0 failures - PASS"),
    ("scsi hba listed",          r"lsi0 \(initiator id 7\)"),
    ("scsi sda target 0",        r"sda: target 0 lun 0"),
    ("scsi qemu harddisk",       r"QEMU.*QEMU HARDDISK"),
    ("scsitest all pass",        r"scsitest: 0 failures - PASS"),
    ("boot command count",       rf"shell command system \({BOOT_COUNT} commands\)"),
]


def main():
    iso, mode, outlog = sys.argv[1], sys.argv[2], sys.argv[3]
    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = (f"{OC_TOOLS}/usr/lib/x86_64-linux-gnu:"
                              f"{OC_TOOLS}/lib/x86_64-linux-gnu")

    # dedicated SCSI test disk (64 MiB, zero-filled, this file only
    # exists for the scsitest round trip)
    os.makedirs("build", exist_ok=True)
    scsi_img = "build/scsi-test.img"
    with open(scsi_img, "wb") as f:
        f.truncate(64 * 1024 * 1024)

    args = [
        "-m", "512M", "-cdrom", iso, "-boot", "d",
        "-L", f"{OC_TOOLS}/usr/share/qemu", "-L", f"{OC_TOOLS}/usr/share/seabios",
        "-vga", "std", "-display", "none",
        "-serial", "mon:stdio",
        "-drive", "if=ide,format=raw,file=build/etc.img",
        "-netdev", "user,id=n1", "-device", "e1000,netdev=n1",
        # camera_misc: parallel port output capture + game port is
        # probed honestly (QEMU has no model), SCSI via the LSI HBA
        "-parallel", "file:build/parallel-out.txt",
        "-device", "lsi,id=scsi0",
        "-drive", f"if=none,id=sd0,format=raw,file={scsi_img}",
        "-device", "scsi-hd,drive=sd0,bus=scsi0.0",
    ]
    if mode == "uefi":
        import shutil
        shutil.copyfile(f"{OC_TOOLS}/usr/share/ovmf/OVMF.fd",
                        "build/OVMF_WORK.fd")
        args += ["-drive", "if=pflash,format=raw,file=build/OVMF_WORK.fd"]

    child = pexpect.spawn(QEMU, args, timeout=240, encoding="latin-1",
                          codec_errors="replace", env=env)
    child.expect(r"oc>\s*", timeout=240)
    boot_out = child.before or ""
    boot_count_ok = re.search(
        rf"shell command system \({BOOT_COUNT} commands\)", boot_out
    ) is not None

    outputs = {}
    fails = 0
    for label, cmd, pattern in [
        ("uvc", "uvc", None),
        ("uvctest", "uvctest", r"uvctest: 0 failures - PASS"),
        ("parallel", "parallel", None),
        ("paralleltest", "paralleltest", r"paralleltest: 0 failures - PASS"),
        ("gameport", "gameport", None),
        ("gameporttest", "gameporttest", r"gameporttest: 0 failures - PASS"),
        ("ir", "ir", None),
        ("irtest", "irtest", r"irtest: 0 failures - PASS"),
        ("scsi", "scsi", r"lsi0 \(initiator id 7\)"),
        ("scsitest", "scsitest", r"scsitest: 0 failures - PASS"),
    ]:
        child.sendline(cmd)
        child.expect(r"oc>\s*", timeout=240)
        outputs[label] = child.before or ""
        if pattern and not re.search(pattern, outputs[label]):
            fails += 1
        print(f"[e2e {mode}] {cmd} done")

    child.sendline("uname -a")
    child.expect(r"oc>\s*", timeout=60)
    uname_out = child.before or ""
    child.sendline("quit")
    try:
        child.expect(pexpect.EOF, timeout=30)
    except pexpect.TIMEOUT:
        pass
    child.close(force=True)

    combined = boot_out + "\n" + "\n".join(outputs.values())

    with open(outlog, "w") as f:
        f.write("===== boot =====\n")
        f.write(boot_out)
        for label in ("uvc", "uvctest", "parallel", "paralleltest",
                      "gameport", "gameporttest", "ir", "irtest",
                      "scsi", "scsitest"):
            f.write(f"\n===== {label} =====\n")
            f.write(outputs.get(label, ""))
        f.write("\n===== uname =====\n")
        f.write(uname_out)

    print(f"=== {mode}: per-command pattern checks ===")
    for name, pat in CHECKS:
        ok = re.search(pat, combined) is not None
        if not ok:
            fails += 1
        print(f"  {'PASS' if ok else 'FAIL'}  {name}")
    if not boot_count_ok:
        fails += 1
    print(f"  {'PASS' if boot_count_ok else 'FAIL'}  boot count {BOOT_COUNT}")

    # verify the SPP byte capture ('OPENCUBE' written by paralleltest)
    try:
        with open("build/parallel-out.txt", "rb") as f:
            data = f.read()
        spp_ok = b"OPENCUBE" in data
    except OSError:
        spp_ok = False
    if not spp_ok:
        fails += 1
    print(f"  {'PASS' if spp_ok else 'FAIL'}  parallel SPP bytes in file")

    print(f"=== {mode}: {len(CHECKS) + 2} checks, {fails} failed ===")
    return fails


if __name__ == "__main__":
    sys.exit(main())
