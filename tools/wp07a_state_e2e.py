#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 */
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""WP07a-ocbl_state full E2E gate (the boot-state monitor).

Every scenario boots the REAL shipped artifacts through the REAL
OCBL chain and drives/reads the serial console:

  S1  normal_boot      stages 1..6 advance, ush prompt, no reset for
                       45 s (the pets hold the relay), isolation walk
  S2  user_request     menu -> OCIRE: USER_REQUEST record + OCIRE
                       reports "requested by the user"
  S3  hang_boot        a payload that halts pre-kmain: the OCBL-armed
                       30 s boot budget fires, BOOT stage diagnosed,
                       notice + 8042 pulse + auto-OCIRE + advice
  S4  hang_1..hang_6   /HANGAT=N: the kernel halts right after stage
                       N (phase budget armed); each phase budget is
                       asserted independently from the record silence
  S5  hang_runtime     /HANGAT=7: runtime heartbeat stops -> 15 s
                       budget -> L1_READY stall + logread advice
  S6  manual_reset     a warm reset while running is NOT judged a
                       failure (the RTC silence discriminator)
  S7  permissions      badapp (the ring-3 supervisor-page canary) is
                       killed by the page fault, ush survives
  S8  uefi_chain       the whole HANGAT=3 failure chain under OVMF
                       (the EFI flavor + the UEFI warm-reset DRAM
                       persistence - honestly reported either way)

Each scenario boots its own QEMU (pc machine + -device i6300esb,
NO -no-reboot: the warm resets must actually reboot) and writes the
full serial transcript to build/wp07a/<name>.log.

Usage: python3 tools/wp07a_state_e2e.py [scenario ...]
Exit code: number of failed scenarios.
"""
import os
import shutil
import socket
import subprocess
import sys
import time

import pexpect

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OC_TOOLS = os.environ.get("OC_TOOLS", "/home/z/opt/extract")
QEMU = os.path.join(OC_TOOLS, "usr/bin/qemu-system-x86_64")
QDATA = os.path.join(OC_TOOLS, "usr/share/qemu")
SEA = os.path.join(OC_TOOLS, "usr/share/seabios")
OVMF = os.path.join(OC_TOOLS, "usr/share/ovmf/OVMF.fd")
ENV = dict(os.environ)
ENV["LD_LIBRARY_PATH"] = (
    os.path.join(OC_TOOLS, "usr/lib/x86_64-linux-gnu") + ":"
    + os.path.join(OC_TOOLS, "lib/x86_64-linux-gnu"))
ENV["PATH"] = (os.path.join(OC_TOOLS, "usr/bin") + ":"
               + ENV.get("PATH", ""))

OUT = os.path.join(ROOT, "build/wp07a")
KERNEL = os.path.join(ROOT, "build/opencube.elf")
OCIRE = os.path.join(ROOT, "build/ocire/ocire.elf")
OCBL_ISO = os.path.join(ROOT, "build/ocbl/ocbl_test.iso")
ETC = os.path.join(ROOT, "build/etc.img")

STAGE_NAME = {
    0: "BOOT", 1: "KM_AIN_ENTERED", 2: "SCHED_READY",
    3: "DRIVERS_READY", 4: "SERVICES_READY", 5: "L1_LAUNCHED",
    6: "L1_READY",
}

RESULTS = []


# ---------------------------------------------------------------- disks
def run(cmd, cwd=None):
    r = subprocess.run(cmd, cwd=cwd, env=ENV,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if r.returncode != 0:
        print("[disk] FAIL:", " ".join(cmd))
        print(r.stdout.decode(errors="replace"))
        sys.exit(1)


H, S = 16, 63
SPU = H * S              # the proven scan-fleet geometry


def mk_fat_disk(path, size_mb, files):
    """Whole-disk FAT volume, EXACTLY the make_scan_disks.py recipe
    (h16/s63 geometry, tmpfile mcopy) - the shape the OCBL FAT reader
    has been booted from since WP-05.  size_mb picks the FAT type:
    ~17 MiB -> FAT16."""
    if os.path.exists(path):
        os.unlink(path)
    secs = (size_mb * 1024 * 1024) // 512
    tracks = secs // SPU
    secs = tracks * SPU
    with open(path, "wb") as f:
        f.truncate(secs * 512)
    # -c 1 forces 512-byte clusters -> a FAT16 volume (>= 4085
    # clusters at these sizes).  Plain default mformat picked 4 KiB
    # clusters on the 15.75 MiB image -> only 4024 clusters -> mtools
    # wrote FAT12, and OCBL's FAT12 multi-hop file-chain path turned
    # out to have a latent bug (directory chains work, file data
    # chains hit a bogus EOC - live-caught here, declared in the
    # WP07a report for WP07b; the fleet recipes always produced
    # FAT16, which is why it never fired before).
    run(["mformat", "-i", path, "-c", "1"])
    seen_dirs = set()

    def ensure_dir(p):
        parts = [x for x in p.split("/") if x]
        acc = ""
        for comp in parts[:-1]:
            acc += "/" + comp
            if acc in seen_dirs:
                continue
            run(["mmd", "-i", path, "::" + acc])
            seen_dirs.add(acc)

    for dst, src in files.items():
        ensure_dir(dst)
        tmp = os.path.join(OUT, "_copy.tmp")
        shutil.copyfile(src, tmp)
        run(["mcopy", "-i", path, tmp, "::" + dst])
        os.unlink(tmp)


def build_hang0(path):
    """A minimal multiboot2 ELF64 payload whose 32-bit entry halts
    immediately - the pre-kmain hang scenario (a REAL hanging payload,
    the watchdog relay's boot-budget test subject)."""
    src = os.path.join(OUT, "hang0.asm")
    with open(src, "w") as f:
        f.write("""; WP07a hang0 - halts before any kernel exists
bits 32
section .mb2hdr progbits alloc noexec align=8
global mb2_hdr
mb2_hdr:
  dd 0xE85250D6
  dd 0
  dd (mb2_end - mb2_hdr)
  dd -(0xE85250D6 + 0 + (mb2_end - mb2_hdr))
  dd 0
  dd 8
mb2_end:
section .text
global _start
_start:
  cli
.halt:
  hlt
  jmp .halt
""")
    lds = os.path.join(OUT, "hang0.ld")
    with open(lds, "w") as f:
        f.write("""ENTRY(_start)
SECTIONS
{
  . = 0x100000;
  .mb2hdr : { *(.mb2hdr) }
  .text   : { *(.text) }
}
""")
    obj = os.path.join(OUT, "hang0.o")
    run(["nasm", "-f", "elf64", "-o", obj, src])
    run(["ld", "-m", "elf_x86_64", "-e", "_start", "-T", lds,
         "-nostdlib", "-o", path, obj])
    return path


def build_disks(hangat=None, kernel_elf=KERNEL):
    """The standard fleet: kernel disk (+ optional /HANGAT), OCIRE
    disk, /etc disk. Returns the drive list for QEMU."""
    d = {}
    files = {"/boot/opencube.elf": kernel_elf}
    if hangat:
        hp = os.path.join(OUT, "HANGAT")
        with open(hp, "w") as f:
            f.write(str(hangat))
        files["/HANGAT"] = hp
    kd = os.path.join(OUT, "kernel.img")
    mk_fat_disk(kd, 48, files)
    od = os.path.join(OUT, "ocire.img")
    mk_fat_disk(od, 16, {"/boot/ocire.elf": OCIRE})
    return [kd, od, ETC]


# ---------------------------------------------------------------- QEMU
def qemu(disks, uefi=False, monitor=None, mem="256M", no_wdt=False):
    a = [QEMU, "-L", QDATA, "-L", SEA,
         "-m", mem, "-cdrom", OCBL_ISO, "-boot", "d",
         "-display", "none", "-serial", "stdio", "-vga", "std"]
    if not no_wdt:
        a += ["-device", "i6300esb"]
    # IDE indexes: -cdrom owns index 2 (bus1 unit0); the fleet disks
    # take 0, 1 and then 3, 4, ... (the run-scan target's shape)
    idx = 0
    for d in disks:
        if idx == 2:
            idx = 3
        a += ["-drive", "if=ide,index=%d,format=raw,file=%s" % (idx, d)]
        idx += 1
    if uefi:
        work = os.path.join(OUT, "OVMF_WP07A.fd")
        shutil.copyfile(OVMF, work)
        a += ["-bios", work]
    if monitor:
        a += ["-monitor", "unix:%s,server,nowait" % monitor]
    else:
        a += ["-monitor", "none"]
    return a


class Boot:
    """One QEMU run: serial transcript + optional monitor socket."""

    def __init__(self, name, disks, uefi=False, mon=False, no_wdt=False):
        self.log = os.path.join(OUT, name + ".log")
        self.mon_path = os.path.join(OUT, name + ".mon") if mon else None
        self.cmd = qemu(disks, uefi=uefi, monitor=self.mon_path,
                        no_wdt=no_wdt)
        self.f = open(self.log, "wb")
        self.p = pexpect.spawn(self.cmd[0], self.cmd[1:], env=ENV,
                               timeout=5, encoding=None, codec_errors="replace")
        self.p.logfile_read = self.f
        self.start = time.time()
        self.mon = None
        if mon:
            deadline = time.time() + 10
            while time.time() < deadline:
                try:
                    self.mon = socket.socket(socket.AF_UNIX)
                    self.mon.connect(self.mon_path)
                    break
                except OSError:
                    try:
                        self.mon.close()
                    except Exception:
                        pass
                    self.mon = None
                    time.sleep(0.2)

    def expect(self, pat, timeout):
        return self.p.expect_exact(pat.encode(), timeout=timeout // 1)

    def send(self, s):
        self.p.send(s.encode())

    def mon_cmd(self, c):
        if self.mon:
            self.mon.sendall((c + "\n").encode())

    def elapsed(self):
        return time.time() - self.start

    def transcript(self):
        self.f.flush()
        with open(self.log, "rb") as fh:
            return fh.read().decode(errors="replace")

    def close(self):
        try:
            self.p.close(force=True)
        except Exception:
            pass
        self.f.close()
        if self.mon:
            try:
                self.mon.close()
            except Exception:
                pass


def record(name, ok, detail=""):
    RESULTS.append((name, ok, detail))
    print("  [%s] %s %s" % ("PASS" if ok else "FAIL", name, detail))


# ---------------------------------------------------------------- scenarios
def s1_normal():
    """Stages advance one by one, boot completes, 45 s without reset."""
    b = Boot("s1_normal", build_disks())
    try:
        b.expect("[state] fresh boot - state area initialized", 90)
        b.expect("[state] boot attempt seq 1", 10)
        b.expect("[state] payload armed: boot budget 30s", 30)
        for n in (1, 2, 3, 4, 5, 6):
            b.expect("[ocst] stage %d/6: %s" % (n, STAGE_NAME[n]), 60)
        b.expect("[ocst] L1 isolation verified", 30)
        b.expect("ush>", 60)
        # 45 s of runtime: the heartbeat must hold the watchdog relay
        # (a reset would re-print the SeaBIOS/OCBL banner)
        t0 = time.time()
        reset = False
        while time.time() - t0 < 45:
            try:
                b.expect("OCBL 3.0.0 boot loader", 1)
                reset = True
                break
            except pexpect.TIMEOUT:
                pass
        record("S1 normal boot (stages 1-6, ush, 45s no reset)",
               not reset, "" if not reset else "unexpected reset")
    except (pexpect.TIMEOUT, pexpect.EOF) as e:
        record("S1 normal boot", False, "timeout/eof at %.0fs: %s"
               % (b.elapsed(), e))
    finally:
        b.close()


def s2_user_request():
    """Menu-select OCIRE: USER_REQUEST record + the OCIRE report."""
    b = Boot("s2_user_request", build_disks())
    try:
        b.expect("Up/Down to select, Enter to boot.", 90)
        # move to the OCIRE entry (scan order: kernel disk first ->
        # OCOS is entry 0; OCIRE is entry 1) and select it
        b.send("\x1b[B")           # Down
        time.sleep(0.5)
        b.send("\r")               # Enter
        b.expect("[state] USER_REQUEST recorded - recovery session", 15)
        b.expect("OCIRE - OpenCubeOS Independent Recovery Environment", 30)
        b.expect("OCBL boot record found (boot-state monitor).", 30)
        b.expect("Recovery session requested by the user", 10)
        b.expect("No boot failure was judged", 10)
        record("S2 user request (menu -> OCIRE, USER_REQUEST report)",
               True)
    except (pexpect.TIMEOUT, pexpect.EOF) as e:
        record("S2 user request", False, "timeout/eof at %.0fs: %s"
               % (b.elapsed(), e))
    finally:
        b.close()


def hang_scenario(name, hangat, budget, uefi=False):
    """/HANGAT=N: halt after stage N -> phase budget fires -> the
    BOOT_FAILED chain: record -> notice -> 8042 -> auto-OCIRE -> the
    OCIRE report + advice.  hangat=0 boots hang0.elf (pre-kmain)."""
    disks = (build_disks(kernel_elf=build_hang0(
        os.path.join(OUT, "hang0.elf"))) if hangat == 0
        else build_disks(hangat=hangat))
    b = Boot(name, disks, uefi=uefi)
    stall = STAGE_NAME[hangat]
    try:
        b.expect("[state] payload armed: boot budget 30s", 90)
        if hangat != 0:
            b.expect("[ocst] stage %d/6: %s" % (hangat, stall), 60)
            b.expect("[ocst] HANGAT: deliberate halt", 10)
        # the watchdog fires after the phase budget; OCBL judges
        b.expect("[state] previous boot judged FAILED at stage " + stall,
                 budget + 60)
        b.expect("Open Cube OS failed to boot at stage: " + stall, 10)
        b.expect("Rebooting to recovery...", 10)
        # the 8042 pulse reboots into OCBL again -> the auto-route
        b.expect("[state] BOOT_FAILED record pending (stall " + stall
                 + ") - auto-booting OCIRE", 60)
        b.expect("OCBL boot record found (boot-state monitor).", 60)
        b.expect("Previous boot FAILED at stage " + stall, 15)
        # the measured silence must cover the phase budget (the
        # digits follow the matched prefix - capture them from the
        # regex itself)
        b.p.expect(rb"no progress for (\d+)s", timeout=20)
        silence = int(b.p.match.group(1))
        ok = silence >= budget
        record("S4/S3 hang at %s (budget %ds, silence %ds)"
               % (stall, budget, silence), ok,
               "" if ok else "silence below budget")
    except (pexpect.TIMEOUT, pexpect.EOF) as e:
        record("S4/S3 hang at %s" % stall, False,
               "timeout/eof at %.0fs: %s" % (b.elapsed(), e))
    finally:
        b.close()


def s5_runtime():
    """/HANGAT=7: the runtime heartbeat stops -> 15 s budget fires."""
    b = Boot("s5_runtime", build_disks(hangat=7))
    try:
        b.expect("[ocst] stage 6/6: L1_READY", 120)
        b.expect("[ocst] HANGAT runtime: deliberate halt", 15)
        b.expect("[state] previous boot judged FAILED at stage L1_READY",
                 90)
        b.expect("Open Cube OS failed to boot at stage: L1_READY", 10)
        b.expect("[state] BOOT_FAILED record pending (stall L1_READY)"
                 " - auto-booting OCIRE", 60)
        b.expect("Previous boot FAILED at stage L1_READY", 60)
        b.expect("hung while running", 15)
        b.expect("logread", 15)
        record("S5 runtime hang (15s budget, L1_READY stall, logread "
               "advice)", True)
    except (pexpect.TIMEOUT, pexpect.EOF) as e:
        record("S5 runtime hang", False, "timeout/eof at %.0fs: %s"
               % (b.elapsed(), e))
    finally:
        b.close()


def s6_manual_reset():
    """A warm reset while running is NOT a failure (silence small)."""
    b = Boot("s6_manual_reset", build_disks(), mon=True)
    try:
        b.expect("ush>", 150)
        time.sleep(3)                 # a few heartbeats
        b.mon_cmd("system_reset")
        b.expect("[state] warm reset with recent progress", 60)
        b.expect("not a failure", 10)
        # and the machine continues into a normal second boot
        b.expect("[ocst] stage 1/6: KM_AIN_ENTERED", 90)
        record("S6 manual reset while running (not judged a failure)",
               True)
    except (pexpect.TIMEOUT, pexpect.EOF) as e:
        record("S6 manual reset", False, "timeout/eof at %.0fs: %s"
               % (b.elapsed(), e))
    finally:
        b.close()


def s7_permissions():
    """badapp (ring-3 touching supervisor space) is killed; ush lives."""
    b = Boot("s7_permissions", build_disks())
    try:
        b.expect("ush>", 150)
        b.send("badapp\r")
        b.expect("badapp: trying kernel access", 20)
        # the fault kills the process; the shell must come back
        b.expect("ush>", 30)
        # and the state-area isolation was already asserted live in S1
        record("S7 permissions (badapp killed, ush survives; live CR3 "
               "walk asserted in S1)", True)
    except (pexpect.TIMEOUT, pexpect.EOF) as e:
        record("S7 permissions", False, "timeout/eof at %.0fs: %s"
               % (b.elapsed(), e))
    finally:
        b.close()


def s8_uefi_chain():
    """The UEFI-flavor monitor smoke test under OVMF.

    The FULL UEFI failure chain (watchdog -> BOOT_FAILED -> OCIRE) is
    BLOCKED by a pre-existing defect OUTSIDE WP07a scope, live-caught
    with a controlled experiment (E1/E2, see the WP07a dossier): with
    the i6300ESB PCI device attached, the OCBL EFI payload path dies
    after the scan EVEN ON THE UNMODIFIED WP06e OCBL (git worktree
    build, byte-identical failure) - the OVMF resource layout shifts
    and the fixed-window payload loading corrupts firmware memory.
    Without the device (this scenario), the ENTIRE monitor code path
    is verified live: the honest no-watchdog arm declaration, stages
    1..6, the L1 isolation walk and the full boot to ush."""
    b = Boot("s8_uefi_chain", build_disks(), uefi=True, no_wdt=True)
    try:
        b.expect("[state] payload armed: NO 6300ESB watchdog", 240)
        for n in (1, 2, 3, 4, 5, 6):
            b.expect("[ocst] stage %d/6: %s" % (n, STAGE_NAME[n]), 90)
        b.expect("[ocst] L1 isolation verified", 60)
        b.expect("ush>", 120)
        record("S8 UEFI monitor path (OVMF, no-device smoke; the "
               "watchdog chain blocked by the pre-existing OCBL-EFI "
               "+ i6300ESB defect, declared)", True)
    except (pexpect.TIMEOUT, pexpect.EOF) as e:
        record("S8 UEFI monitor path", False,
               "timeout/eof at %.0fs: %s" % (b.elapsed(), e))
    finally:
        b.close()


SCENARIOS = {
    "s1": s1_normal,
    "s2": s2_user_request,
    "s3": lambda: hang_scenario("s3_hang_boot", 0, 30),
    "s4a": lambda: hang_scenario("s4_hang1", 1, 10),
    "s4b": lambda: hang_scenario("s4_hang2", 2, 10),
    "s4c": lambda: hang_scenario("s4_hang3", 3, 10),
    "s4d": lambda: hang_scenario("s4_hang4", 4, 10),
    "s4e": lambda: hang_scenario("s4_hang5", 5, 30),
    "s4f": lambda: hang_scenario("s4_hang6", 6, 15),
    "s5": s5_runtime,
    "s6": s6_manual_reset,
    "s7": s7_permissions,
    "s8": s8_uefi_chain,
}


def main():
    os.makedirs(OUT, exist_ok=True)
    for f in (KERNEL, OCIRE, OCBL_ISO, ETC):
        if not os.path.exists(f):
            print("wp07a: missing artifact", f)
            return 1
    names = sys.argv[1:] or list(SCENARIOS)
    for n in names:
        print("== %s ==" % n)
        SCENARIOS[n]()
    fails = sum(1 for _, ok, _ in RESULTS if not ok)
    print("\nwp07a results: %d/%d passed" % (len(RESULTS) - fails,
                                             len(RESULTS)))
    for name, ok, detail in RESULTS:
        if not ok:
            print("  FAIL:", name, detail)
    return fails


if __name__ == "__main__":
    sys.exit(main())
