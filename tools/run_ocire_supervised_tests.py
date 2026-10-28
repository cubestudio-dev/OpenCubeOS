#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 */
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - 3.0.0-WP03 G4
# File: tools/run_ocire_supervised_tests.py
# Purpose: the OCIRE-supervised boot battery - the OCOS side of the
#          embed (services/ocembed) exercised end to end.
#
# Topology (tools/make_ocire_disk.sh):
#   hd0 ocire-boot.img - GRUB dual menu; default entry = the OCIRE
#                       boot supervisor (60 s watchdog + mailbox)
#   hd1 ocire-hd.img   - the OCIRE recovery disk (0xEE shadow, FAT12:
#                       mailbox + log ring + agent + golden files)
#
# Cases:
#   supervised_bios  - supervisor -> kexec -> OCOS: the embed facts
#                      parsed, whole-disk protection ARMED on hdb,
#                      SIGNAL_OK written, agent spawned protected;
#                      ush: ps lists ocire_guard, kill is REFUSED,
#                      disk_rw_test hdb REFUSED (hard protection),
#                      lsblk shows NO hdbp1 (0xEE isolation).
#                      Then the full recovery shell on the SAME disk
#                      copy: syscheck sees SIGNAL_OK + boots=1 and
#                      logread shows the agent's ring appends.
#   supervised_wdt   - same with -device i6300esb: the supervisor
#                      arms the 60 s window, OCOS disarms it, and the
#                      machine is STILL ALIVE at t=75 s (the WP-02
#                      honest boundary - hard reset on silence - is
#                      closed by the OCOS-side disarm).
#   unsupervised_open- OCOS from the ISO with both disks attached:
#                      no oc.* facts, the embed is dormant, and the
#                      whole-disk surface stays OPEN (honest
#                      declaration: the protection is a property of
#                      the supervised relationship, not of the disk
#                      bytes) while the PARTITION isolation still
#                      holds (no hdbp1 device).
import os
import re
import shutil
import sys
import time
import pexpect

OC_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
OC = os.environ.get("OC_TOOLS", "/home/z/opt/extract")
QEMU = os.path.join(OC, "usr/bin/qemu-system-x86_64")
QDATA = os.path.join(OC, "usr/share/qemu")
SEA = os.path.join(OC, "usr/share/seabios")
BOOT = os.path.join(OC_ROOT, "build/ocire/ocire-boot.img")
HD = os.path.join(OC_ROOT, "build/ocire/ocire-hd.img")
ISO = os.path.join(OC_ROOT, "build/opencube.iso")
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


def throwaway(tag):
    d0 = os.path.join(OUT, f"boot-{tag}.img")
    d1 = os.path.join(OUT, f"hd-{tag}.img")
    shutil.copyfile(BOOT, d0)
    shutil.copyfile(HD, d1)
    return d0, d1


def spawn_supervised(disk0, disk1, log_path, wdt=False, timeout=300):
    """Boot hd0 with the default GRUB entry: the OCIRE supervisor
    hands the machine to OCOS via kexec (no keypress - the 8 s
    timeout picks the supervisor)."""
    args = [QEMU, "-L", QDATA, "-L", SEA, "-m", "256M",
            "-drive", f"if=ide,index=0,format=raw,file={disk0}",
            "-drive", f"if=ide,index=1,format=raw,file={disk1}",
            "-no-reboot", "-display", "none", "-serial", "stdio",
            "-monitor", "none", "-vga", "std", "-boot", "c"]
    if wdt:
        args.insert(1, "-device")
        args.insert(2, "i6300esb")
    child = pexpect.spawn(args[0], args[1:], timeout=timeout,
                          encoding="utf-8", codec_errors="replace", env=ENV)
    child.logfile = open(log_path, "w")
    return child


def spawn_full_shell(disk0, disk1, log_path, timeout=300):
    """Boot hd0 picking menu entry 2 (the OCIRE full recovery shell)."""
    args = [QEMU, "-L", QDATA, "-L", SEA, "-m", "256M",
            "-drive", f"if=ide,index=0,format=raw,file={disk0}",
            "-drive", f"if=ide,index=1,format=raw,file={disk1}",
            "-no-reboot", "-display", "none", "-serial", "stdio",
            "-monitor", "none", "-vga", "std", "-boot", "c"]
    child = pexpect.spawn(args[0], args[1:], timeout=timeout,
                          encoding="utf-8", codec_errors="replace", env=ENV)
    child.logfile = open(log_path, "w")
    i = child.expect_exact(["GNU GRUB", pexpect.TIMEOUT, pexpect.EOF],
                           timeout=120)
    if i != 0:
        child.logfile.close()
        child.close(force=True)
        raise RuntimeError("GRUB menu never appeared on serial")
    time.sleep(1.5)
    child.send("\x1b[B")                     # cursor-down -> entry 2
    time.sleep(1.5)
    child.send("\r")
    i = child.expect_exact(["ocire>", pexpect.TIMEOUT, pexpect.EOF],
                           timeout=120)
    if i != 0:
        child.logfile.close()
        child.close(force=True)
        raise RuntimeError("no ocire> prompt after menu pick")
    return child


def await_output(child, want, label, timeout=180):
    i = child.expect_exact([want, pexpect.TIMEOUT, pexpect.EOF],
                           timeout=timeout)
    if i != 0:
        print(f"    (await failed: {label})")
        return False
    return True


def run(child, cmd, want, timeout=180):
    child.sendline(cmd)
    i = child.expect_exact([want, pexpect.TIMEOUT, pexpect.EOF],
                           timeout=timeout)
    return i == 0


# ----------------------------------------------------------------------

def case_supervised_bios():
    case = "supervised_bios"
    d0, d1 = throwaway("sup1")
    try:
        c = spawn_supervised(d0, d1, os.path.join(OUT, "supervised-bios.log"))
    except Exception as e:
        record(case, False, str(e))
        return
    ok = True
    agent_pid = None
    try:
        ok &= await_output(c, "OCIRE boot supervisor", "grub menu default")
        ok &= await_output(c, "Handing the machine to Open Cube OS",
                           "supervisor hand-over")
        ok &= await_output(c, "ocire embed: SUPERVISED boot (mb=",
                           "OCOS facts parsed")
        ok &= await_output(c, "ocire embed: whole-disk write protection ARMED",
                           "protection armed")
        ok &= await_output(c, "boot signal written (SIGNAL_OK, boot_count+1)",
                           "boot signal")
        ok &= await_output(c, "ocire embed: log agent spawned (ocire_guard",
                           "agent spawn")
        # capture the pid from the spawn line
        m = re.search(r"ocire_guard, pid=(\d+)",
                      open(os.path.join(OUT, "supervised-bios.log")).read())
        agent_pid = m.group(1) if m else None
        ok &= await_output(c, "[ocire-agent] started",
                           "agent first output")
        ok &= await_output(c, "L0 ready, waiting for L1", "L0 ready")
        ok &= await_output(c, "ush>", "ush prompt")

        # process protection: ps lists it, kill is refused, it stays
        ok &= run(c, "ps", "ocire_guard")
        if agent_pid:
            ok &= run(c, f"kill {agent_pid}", "kill: no such process")
            ok &= run(c, "ps", "ocire_guard")      # still alive
        else:
            ok = False

        # whole-disk hard protection: the recovery disk write test is refused
        ok &= run(c, "disk_rw_test hdb", "REFUSED (ocire recovery disk)")

        # partition isolation on the same boot: the 0xEE shadow has no
        # parseable table and no partition device node
        ok &= run(c, "parted hdb", "no partition table found")
        ok &= run(c, "disk_rw_test hdbp1", "not found")
        # Let the agent run ~4 cycles (sleep 5 s + snapshot + append):
        # the whole command block above finishes INSIDE the agent's
        # FIRST 5-second sleep, so without this wait the on-disk ring
        # would hold nothing but the 28-byte banner (found live - the
        # case used to close QEMU before the agent's first wake).
        print("    (waiting 22 s for agent cycles...)")
        time.sleep(22)
        ok &= run(c, "ps", "ocire_guard")           # still alive+listed
    finally:
        # CLEAN shutdown: QEMU's default writeback disk cache means a
        # SIGKILL loses the agent's ring appends (found live: the first
        # battery run verified a 28-byte ring after ~7 agent cycles).
        # ACPI poweroff makes QEMU exit cleanly and flush everything.
        try:
            c.sendline("shutdown")
            c.expect([pexpect.EOF, pexpect.TIMEOUT], timeout=30)
        except Exception:
            pass
        c.logfile.close()
        c.close(force=True)

    # verify the on-disk state from the recovery side (same disk copy)
    if ok:
        try:
            r = spawn_full_shell(d0, d1,
                                 os.path.join(OUT, "supervised-verify.log"))
        except RuntimeError as e:
            record(case, False, f"verify boot failed: {e}")
            return
        try:
            ok &= run(r, "syscheck", "state SIGNAL_OK")
            ok &= run(r, "syscheck", "boots 1")
            ok &= run(r, "logread", "ocire-agent started")
            ok &= run(r, "logread", "ocire-agent alive")
        finally:
            r.logfile.close()
            r.close(force=True)
    record(case, ok, "facts+signal+protection+agent+kill-refused+"
                     "disk_rw refused+isolation+on-disk verify")


def case_supervised_wdt():
    case = "supervised_wdt"
    d0, d1 = throwaway("sup2")
    try:
        c = spawn_supervised(d0, d1, os.path.join(OUT, "supervised-wdt.log"),
                             wdt=True)
    except Exception as e:
        record(case, False, str(e))
        return
    ok = True
    try:
        ok &= await_output(c, "Watchdog armed (i6300esb): 60 s boot window",
                           "supervisor armed the wdt")
        ok &= await_output(c, "ocire embed: SUPERVISED boot",
                           "supervised hand-over")
        ok &= await_output(c, "ocire embed: i6300esb watchdog DISARMED",
                           "OCOS disarmed the wdt")
        ok &= await_output(c, "ush>", "ush prompt")
        # the machine must still be alive AFTER the 60 s window closed:
        # total elapsed since boot is ~14 s here, so wait to t=75 s
        print("    (waiting past the 60 s window...)")
        time.sleep(62)
        ok &= run(c, "stats", "ticks=", timeout=60)
        ok &= run(c, "ps", "ocire_guard", timeout=60)
    finally:
        # clean shutdown so the ring writes flush
        try:
            c.sendline("shutdown")
            c.expect([pexpect.EOF, pexpect.TIMEOUT], timeout=30)
        except Exception:
            pass
        c.logfile.close()
        c.close(force=True)
    # the definitive liveness proof: the on-disk ring (the ps listing
    # alone cannot distinguish a live cycler from a frozen sleeper -
    # both show up; a boot-time ps even caught the agent at 1 tick
    # despite 17 completed cycles, the tick accounting is sampling
    # based).  Boot the recovery shell on the SAME disks and read the
    # ring: an agent that cycled through the 60 s window left its
    # snapshots + heartbeats there.
    try:
        r = spawn_full_shell(d0, d1,
                             os.path.join(OUT, "supervised-wdt-verify.log"))
    except RuntimeError as e:
        record(case, False, f"verify boot failed: {e}")
        return
    try:
        ok &= run(r, "syscheck", "state SIGNAL_OK")
        ok &= run(r, "logread", "ocire-agent alive")
        ok &= run(r, "logread", "bytes capacity")
    finally:
        r.logfile.close()
        r.close(force=True)
    record(case, ok, "wdt armed -> disarmed -> alive at t>75s + "
                     "on-disk ring shows live agent cycles "
                     "(WP-02 honest boundary closed)")


def case_unsupervised_open():
    case = "unsupervised_open"
    d0, d1 = throwaway("sup3")
    # WP-01 isolation topology: the OCIRE disk at index 0 (hda - the
    # isolation target), the dual-boot disk at index 1 (hdb).  The OCOS
    # ISO's GRUB probes (hd0,msdos2) for its A/B layout; the OCIRE
    # disk's 0xEE shadow makes the probe fail and OCOS boots from the
    # CD's ISO slot (this is exactly how run_ocire_dual_tests.py's
    # isolation case orders the drives).
    args = [QEMU, "-L", QDATA, "-L", SEA, "-m", "256M",
            "-cdrom", ISO,
            "-drive", f"if=ide,index=0,format=raw,file={d1}",
            "-drive", f"if=ide,index=1,format=raw,file={d0}",
            "-no-reboot", "-display", "none", "-serial", "stdio",
            "-monitor", "none", "-vga", "std", "-boot", "d"]
    child = pexpect.spawn(args[0], args[1:], timeout=240,
                          encoding="utf-8", codec_errors="replace", env=ENV)
    child.logfile = open(os.path.join(OUT, "unsupervised-open.log"), "w")
    ok = True
    try:
        ok &= await_output(child, "L0 ready, waiting for L1", "boot")
        ok &= await_output(child, "ush>", "ush prompt")
        # no oc.* facts on this boot: the embed must be dormant
        log = open(os.path.join(OUT, "unsupervised-open.log")).read()
        ok &= ("SUPERVISED boot" not in log)
        ok &= ("protection ARMED" not in log)
        # the whole-disk surface stays OPEN without supervision (the
        # WP-01 rawdecl honest declaration, unchanged by WP-03; the
        # OCIRE disk is hda in this topology)
        ok &= run(child, "disk_rw_test hda", "PASS", timeout=240)
        # the partition isolation still holds (WP-01 property)
        ok &= run(child, "parted hda", "no partition table found")
        ok &= run(child, "disk_rw_test hdap1", "not found")
    finally:
        child.logfile.close()
        child.close(force=True)
    record(case, ok, "unsupervised: embed dormant, whole-disk surface "
                     "open (protection is supervision-scoped), "
                     "partition isolation holds")


def main():
    for f in (BOOT, HD, ISO):
        if not os.path.exists(f):
            print(f"[FATAL] {f} missing (make iso && make ocire-disk)",
                  file=sys.stderr)
            sys.exit(1)
    case_supervised_bios()
    case_supervised_wdt()
    case_unsupervised_open()
    bad = sum(1 for _, ok, _ in RESULTS if not ok)
    print(f"\nsupervised battery: {len(RESULTS) - bad} passed, {bad} failed")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
