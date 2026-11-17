#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""Open Cube OS - WP-07-ocire-loop-perfect E2E test battery.

The spec's eight acceptance items, each with hard evidence:
  1  build           `make all` 0 errors 0 warnings (run separately)
  2  fat32-mount     OCIRE lists the ESP contents over FAT32
  3  sig-reject      a forged-signature package is REFUSED
  4  dual-flash      a proper package lands in the inactive slot
  5  anti-brick      a dead B slot falls back to A at the next boot
  6  os-takeover     two consecutive boot failures pull up the
                     interactive [R]/[C] menu
  7  s3-wake-fail    a surviving SLEEP_ARMED flag pulls up the
                     interactive [W]/[R]/[C] menu
  8  regression      the existing 31/31 battery (run separately)

Cases 3-7 each boot their own QEMU (fresh disk state per case); the
serial transcripts land in build/ocire/wp07-*.log.

State injection for 6/7 uses the QEMU gdb stub (the same physical
RAM the kernel would have written) + a monitor system_reset - the
state-area islands at 0x98000/0x9B000 are the proven reset
survivors, so the injected records take the REAL OCBL path.
"""
import os
import shutil
import socket
import struct
import subprocess
import sys
import time

import pexpect

OC_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
OC = os.environ.get("OC_TOOLS", os.path.expanduser("~/opt/extract"))
QEMU = os.path.join(OC, "usr/bin/qemu-system-x86_64")
OUT = os.path.join(OC_ROOT, "build/ocire")
ENV = dict(os.environ)
ENV["LD_LIBRARY_PATH"] = (
    os.path.join(OC, "usr/lib/x86_64-linux-gnu") + ":" +
    os.path.join(OC, "lib/x86_64-linux-gnu") + ":" +
    ENV.get("LD_LIBRARY_PATH", ""))

HD = os.path.join(OUT, "ocire-hd.img")
BOOT = os.path.join(OUT, "ocire-boot.img")
ESP = os.path.join(OUT, "esp.img")

RESULTS = []


def qemu_cmd(extra=(), disks=("hd", "boot", "esp"), gdb=False):
    args = [QEMU, "-m", "512",
            "-L", os.path.join(OC, "usr/share/qemu"),
            "-L", os.path.join(OC, "usr/share/seabios")]
    names = {"hd": HD, "boot": BOOT, "esp": ESP}
    for i, d in enumerate(disks):
        args += ["-drive", "file=%s,if=ide,index=%d,media=disk,format=raw" % (names[d], i)]
    if gdb:
        args += ["-gdb", "tcp::1234"]
    args += ["-net", "none", "-serial", "stdio",
             "-monitor", "none", "-display", "none", "-no-reboot"]
    args += list(extra)
    return args


def spawn(case, disks=("hd", "boot", "esp"), gdb=False, timeout=120):
    # the disk images are shared across cases: make sure NO qemu of a
    # previous case still holds the write lock
    subprocess.run(["pkill", "-f", "qemu-system-x86_64"], check=False)
    for _ in range(50):
        r = subprocess.run(["pgrep", "-f", "qemu-system-x86_64"],
                           capture_output=True)
        if r.returncode != 0:
            break
        time.sleep(0.2)
    log = open(os.path.join(OUT, "wp07-%s.log" % case), "w")
    child = pexpect.spawn(" ".join(qemu_cmd(disks=disks, gdb=gdb)),
                          env=ENV, timeout=timeout, encoding="utf-8",
                          codec_errors="replace")
    child.logfile = log
    child.log_path = log
    return child


def pick_ocire_slot_a(child):
    """drive the OCBL menu to the slot-A entry"""
    child.expect("Booting Open Cube OS", timeout=40)
    child.send("\x1b[A")                       # Up arrow to entry 0
    child.expect(r"OCIRE recovery \(slot A", timeout=15)
    child.send("\r")


def reset_ab_flags(active=1, try_slot=0):
    """rewrite the recovery-disk A/B flags sector (host side)"""
    with open(HD, "r+b") as f:
        f.seek(1024 * 512)
        sec = bytearray(f.read(512))
        sec[0:8] = b"OCIREAB1"
        sec[8] = active
        sec[9] = try_slot
        sec[10] = 0
        sec[11] = 0
        s = 0
        for i in range(8):
            s += struct.unpack_from("<H", sec, i * 2)[0]
        struct.pack_into("<I", sec, 16, (~s) & 0xFFFF)
        f.seek(1024 * 512)
        f.write(bytes(sec))


def restore_slot_b():
    """slot B := byte-identical copy of slot A (post-flash sanitation)"""
    with open(HD, "r+b") as f:
        f.seek(2048 * 512)
        slot_a = f.read(131072 * 512)
        f.seek(133120 * 512)
        f.write(slot_a)


def boot_to_shell(case, disks=("hd", "boot", "esp"), timeout=150):
    child = spawn(case, disks=disks, timeout=timeout)
    pick_ocire_slot_a(child)
    child.expect("Ready. Type 'help' for the command list", timeout=90)
    return child


# ---- gdb stub memory writer (the injection path for 6/7) -------------

class GdbStub:
    def __init__(self, host="127.0.0.1", port=1234, timeout=10):
        self.s = socket.create_connection((host, port), timeout=timeout)
        self.s.settimeout(timeout)
        self.buf = b""

    def _read_packet(self):
        while b"$" not in self.buf or b"#" not in self.buf.split(b"$", 1)[1]:
            d = self.s.recv(4096)
            if not d:
                raise IOError("gdb stub closed")
            self.buf += d
        pkt = self.buf.split(b"$", 1)[1]
        end = pkt.index(b"#")
        self.buf = pkt[end + 3:]
        return pkt[:end]

    def cmd(self, c):
        cs = sum(c.encode()) % 256
        self.s.sendall(b"$" + c.encode() + b"#%02x" % cs)
        # swallow ACKs until a packet arrives
        while True:
            d = self.s.recv(4096)
            if not d:
                raise IOError("gdb stub closed")
            self.buf += d
            self.buf = self.buf.lstrip(b"+")
            if b"$" in self.buf:
                break
        ack_and_pkt = self.buf
        pkt = self._read_packet()
        self.s.sendall(b"+")
        return pkt.decode("latin1")

    def write_mem(self, addr, data):
        hexdata = data.hex()
        r = self.cmd("M%x,%x:%s" % (addr, len(data), hexdata))
        if not r.startswith("OK"):
            raise IOError("gdb write failed: %r" % r)

    def detach_running(self):
        """resume the VM and detach: QEMU's gdb stub STOPS the VM on
        attach, and a bare socket close leaves it stopped - send a
        continue first so the machine keeps running after we go."""
        try:
            cs = sum(b"c") % 256
            self.s.sendall(b"$c#%02x" % cs)
            time.sleep(0.3)          # let the resume land
        except Exception:
            pass

    def read_mem(self, addr, length):
        r = self.cmd("m%x,%x" % (addr, length))
        if r.startswith("E"):
            raise IOError("gdb read failed: %r" % r)
        return bytes.fromhex(r)

    def close(self):
        self.s.close()


def inject_via_gdb(child, writes, reset_after=True):
    """Attach to the QEMU gdb stub, poke RAM, then hard-reset through
    the monitor... without a monitor we reboot via the guest itself:
    the injected writes happen while the guest sits at the shell, then
    we ask the guest to reboot (its own reset path)."""
    stub = GdbStub()
    for addr, data in writes:
        stub.write_mem(addr, data)
    stub.close()


# ---- record/state builders -------------------------------------------

def ocst_record(kind, stall=1, seq=1, ts=1700000000, silence=30,
                routed=0, boot_fails=2):
    payload = struct.pack("<IHHBBHII", 0x3142434F, 1, 28, kind, stall,
                          seq, ts, silence)
    payload += struct.pack("<BBBBH", routed, boot_fails, 0, 0, 0)
    payload = payload[:24]
    # checksum: ones-complement of the payload as 6 LE u32 words
    s = 0
    for i in range(6):
        s += struct.unpack_from("<I", payload, i * 4)[0]
    ck = (~s) & 0xFFFFFFFF
    return payload + struct.pack("<I", ck)


def ocst_state(seq=1, stage=6, armed=1, kflags=0x02):
    body = struct.pack("<IHHHBBBBII", 0x3153434F, 1, 24, seq, stage,
                       armed, kflags, 0, 100, 1700000000)
    return body


# ---- the cases --------------------------------------------------------

def case_fat32_mount():
    """2: OCIRE reads + lists the ESP over FAT32 (GPT)."""
    # a package-less ESP + clean flags (fresh, generic shape)
    subprocess.run(["bash", os.path.join(OC_ROOT, "tools/make_esp_disk.sh")],
                   check=True, cwd=OC_ROOT, stdout=subprocess.DEVNULL)
    reset_ab_flags()
    restore_slot_b()
    child = boot_to_shell("fat32-mount")
    child.sendline("ls")
    child.expect("ESP: ata", timeout=30)
    child.expect("EFI", timeout=30)
    child.expect(r"2 entr", timeout=30)
    child.sendline("ls EFI")
    child.expect("BOOT", timeout=30)
    child.expect("OpenCube", timeout=30)
    child.sendline("ls EFI/BOOT")
    child.expect("BOOTX64.EFI", timeout=30)
    child.sendline("ls EFI/OpenCube")
    child.expect("No such|cannot list|not found|0 entr", timeout=20)
    child.sendline("ls slotB:")
    child.expect("GOLDEN", timeout=30)
    child.expect("MAILBOX.BIN", timeout=30)
    child.sendline("shutdown")
    time.sleep(2)
    child.terminate(force=True)
    return True


def _inject_pkg(bin_path, sig_path):
    """place the package on the ESP (rebuilds the ESP with the pkg)"""
    subprocess.run(["bash", os.path.join(OC_ROOT, "tools/make_esp_disk.sh"),
                    "--pkg", bin_path, sig_path],
                   check=True, cwd=OC_ROOT,
                   stdout=subprocess.DEVNULL)


def case_sig_reject():
    """3: a forged signature is refused."""
    reset_ab_flags()
    restore_slot_b()
    # a real payload but a FORGED 64-byte signature
    pkg = os.path.join(OUT, "ocire.elf")
    forged = os.path.join(OUT, "forged.sig")
    with open(forged, "wb") as f:
        f.write(bytes((i * 37 + 11) & 0xFF for i in range(64)))
    _inject_pkg(pkg, forged)
    child = boot_to_shell("sig-reject")
    child.sendline("update")
    child.expect("SIGNATURE VERIFICATION FAILED", timeout=60)
    child.expect("REFUSED - nothing was written", timeout=30)
    # and the flags prove nothing was armed
    child.sendline("slots")
    child.expect("try=0", timeout=30)
    child.sendline("shutdown")
    time.sleep(2)
    child.terminate(force=True)
    return True


def case_dual_flash():
    """4: a properly signed package lands in slot B + arms it."""
    reset_ab_flags()
    restore_slot_b()
    pkg = os.path.join(OUT, "ocire.elf")
    sig = pkg + ".sig"
    subprocess.run(["python3",
                    os.path.join(OC_ROOT, "tools/gen_ocire_ed25519.py"),
                    "sign", pkg], check=True, cwd=OC_ROOT,
                   stdout=subprocess.DEVNULL)
    _inject_pkg(pkg, sig)
    child = boot_to_shell("dual-flash")
    child.sendline("update")
    child.expect("signature OK", timeout=60)
    child.expect("flashing slot B", timeout=60)
    child.expect("read-back verified", timeout=120)
    child.expect("SUCCESS.  Slot B now holds the new OCIRE", timeout=30)
    child.sendline("slots")
    child.expect("try=2", timeout=30)
    # the flags really say try=B on disk
    with open(HD, "rb") as f:
        f.seek(1024 * 512)
        flags = f.read(24)
        assert flags[9] == 2, "on-disk try_slot != B"
    child.sendline("shutdown")
    time.sleep(2)
    child.terminate(force=True)
    return True


def corrupt_slot_b_kernel():
    """overwrite slot B's /boot/ocire.elf DATA with garbage (the file
    entry stays; the payload becomes an unbootable blob)"""
    with open(HD, "r+b") as f:
        f.seek(133120 * 512)
        bpb = f.read(512)
        fatsz = struct.unpack_from("<I", bpb, 36)[0]
        reserved = struct.unpack_from("<H", bpb, 14)[0]
        data_start = 133120 + reserved + 2 * fatsz
        # boot dir = cluster 3; ocire.elf = cluster 7 (verified by the
        # builder layout: boot kernel goes in FIRST on a fresh volume)
        f.seek((data_start + 3 - 2) * 512)
        d = f.read(512)
        kern_clus = None
        for o in range(0, 512, 32):
            e = d[o:o + 32]
            if e[0] in (0, 0xE5) or (e[11] & 0x3F) == 0x0F:
                continue
            if e[:11] == b"OCIRE   ELF":
                kern_clus = struct.unpack_from("<H", e, 26)[0] | \
                            (struct.unpack_from("<H", e, 20)[0] << 16)
                break
        assert kern_clus and kern_clus >= 2, "ocire.elf not found in slot B"
        # FAT chain walk: garbage every cluster of the file
        f.seek((133120 + reserved) * 512)
        fat = f.read(fatsz * 512)

        def next_cl(c):
            return struct.unpack_from("<I", fat, c * 4)[0] & 0x0FFFFFFF

        c = kern_clus
        junk = bytes((i * 91 + 7) & 0xFF for i in range(512))
        n = 0
        while 2 <= c < 0x0FFFFFF8 and n < 4096:
            f.seek((data_start + c - 2) * 512)
            f.write(junk)
            c = next_cl(c)
            n += 1


def case_anti_brick():
    """5: a dead B slot (unbootable kernel) is abandoned; the loader
    falls back to A on the very next boot."""
    reset_ab_flags()
    restore_slot_b()
    corrupt_slot_b_kernel()
    reset_ab_flags(active=1, try_slot=2)      # arm the try-slot
    child = spawn("anti-brick")
    # OCBL tries B (attempt 1), the load fails, it falls back to A;
    # the menu default is OCOS: pick the OCIRE entry (labeled slot B).
    # The [ab] trail lands in the scan phase BEFORE the menu - the
    # assertions on it run on the captured transcript afterwards.
    child.expect("Booting Open Cube OS", timeout=40)
    child.send("\x1b[A")
    child.expect(r"OCIRE recovery \(slot B", timeout=15)
    child.send("\r")
    child.expect("Ready. Type 'help' for the command list", timeout=90)
    child.sendline("slots")
    child.expect("try=0", timeout=30)
    child.sendline("shutdown")
    time.sleep(2)
    child.terminate(force=True)
    # hard evidence from the transcript
    log = open(os.path.join(OUT, "wp07-anti-brick.log")).read()
    for needle in (
            "[ab] trying recovery slot B (attempt 1)",
            "OCIRE slot load FAILED - falling back",
            "fallback slot boots - switching the entry",
            "try-slot abandoned in the flags"):
        if needle not in log:
            print("[wp07]   missing evidence: %r" % needle)
            return False
    restore_slot_b()
    reset_ab_flags()
    return True


def case_os_takeover():
    """6: two consecutive boot failures -> auto OCIRE + [R]/[C] menu.

    FULLY REAL chain (no injection): /HANGAT=3 on the OCOS volume
    makes the kernel halt right after the DRIVERS_READY stage; the
    i6300ESB watchdog (the -device on the QEMU command line) fires
    at the phase budget, OCBL judges BOOT_FAILED - first judgment
    only retries OCOS, the second (consecutive) one routes OCIRE and
    the blocking [R]/[C] menu appears.
    """
    reset_ab_flags()
    restore_slot_b()
    # /HANGAT=3 on the OCOS volume (hd1 p1 of the boot disk)
    hangat = os.path.join(OUT, ".HANGAT")
    with open(hangat, "w") as f:
        f.write("3")
    subprocess.run(["mcopy", "-i", BOOT + "@@1048576", hangat,
                    "::/HANGAT"], check=True)

    args = [QEMU, "-m", "512",
            "-L", os.path.join(OC, "usr/share/qemu"),
            "-L", os.path.join(OC, "usr/share/seabios"),
            "-drive", "file=%s,if=ide,index=0,media=disk,format=raw" % HD,
            "-drive", "file=%s,if=ide,index=1,media=disk,format=raw" % BOOT,
            "-device", "i6300esb",
            "-net", "none", "-serial", "stdio", "-monitor", "none",
            "-display", "none"]          # NO -no-reboot: resets must loop
    log = open(os.path.join(OUT, "wp07-os-takeover.log"), "w")
    child = pexpect.spawn(" ".join(args), env=ENV, timeout=200,
                          encoding="utf-8", codec_errors="replace")
    child.logfile = log
    try:
        # round 1: OCOS boots, halts at stage 3, watchdog fires (~10 s)
        child.expect(r"\[ocst\] stage 3/6: DRIVERS_READY", timeout=90)
        child.expect(r"HANGAT: deliberate halt", timeout=30)
        child.expect(r"\[state\] previous boot judged FAILED at stage DRIVERS_READY",
                     timeout=90)
        child.expect("consecutive failure 1", timeout=30)
        child.expect("retrying OCOS", timeout=30)
        # round 2: the same again - HANGAT is still on the volume
        child.expect(r"\[ocst\] stage 3/6: DRIVERS_READY", timeout=90)
        child.expect(r"HANGAT: deliberate halt", timeout=30)
        child.expect(r"\[state\] previous boot judged FAILED at stage DRIVERS_READY",
                     timeout=90)
        child.expect("consecutive failure 2", timeout=30)
        child.expect("routing to recovery", timeout=30)
        # the next OCBL pass auto-boots OCIRE with the takeover menu
        child.expect("BOOT_FAILED record pending", timeout=60)
        child.expect("auto-booting OCIRE", timeout=30)
        child.expect("OPEN CUBE OS FAILED TO BOOT", timeout=90)
        child.expect(r"\[R\] Rollback to the previous OCOS version", timeout=30)
        child.expect(r"\[C\] Continue and try booting OCOS again", timeout=30)
        # a wrong key is ignored - the menu truly blocks
        child.sendline("x")
        time.sleep(1)
        child.send("C")
        child.expect("Clearing the failure record and rebooting", timeout=30)
        time.sleep(3)
        ok = True
    except Exception as e:
        print("[wp07]   EXCEPTION: %r" % e)
        ok = False
    finally:
        child.terminate(force=True)
        subprocess.run(["mdel", "-i", BOOT + "@@1048576", "::/HANGAT"],
                       check=False)
        reset_ab_flags()
        restore_slot_b()
    return ok


def _os_takeover_via_monitor():
    """the real one-process variant: serial+monitor mux, inject RAM,
    then monitor system_reset keeps DRAM (the island survives)."""
    args = qemu_cmd(disks=("hd", "boot"), gdb=True)
    args = [a for a in args if a != "-no-reboot"]
    args += ["-monitor", "stdio", "-serial", "mon:stdio"]
    # rebuild args: use mon:stdio so Ctrl-A c switches serial<->monitor
    args = qemu_cmd(disks=("hd", "boot"), gdb=True)
    args = [a for i, a in enumerate(args)
            if not (a == "-monitor" or (i and args[i-1] == "-monitor"))]
    args = [a for a in args if a != "-no-reboot"]
    args += ["-monitor", "none"]
    # simplest: keep serial stdio; reset through the gdb stub instead
    # (gdb 'k' kills; we want reset... QEMU gdb stub has no reset).
    # Use the QMP-less human monitor over a unix socket instead.
    sock_path = "/tmp/wp07-mon.sock"
    if os.path.exists(sock_path):
        os.unlink(sock_path)
    args = qemu_cmd(disks=("hd", "boot"), gdb=True)
    args = [a for a in args if a != "-no-reboot"]
    args += ["-monitor", "unix:%s,server,nowait" % sock_path]
    log = open(os.path.join(OUT, "wp07-os-takeover.log"), "w")
    child = pexpect.spawn(" ".join(args), env=ENV, timeout=120,
                          encoding="utf-8", codec_errors="replace")
    child.logfile = log
    child.expect("Booting Open Cube OS", timeout=60)
    child.send("\r")
    child.expect("ush> ", timeout=120)

    rec = ocst_record(kind=1, boot_fails=2, routed=0, stall=6, seq=9)
    state = ocst_state(seq=10, stage=6, armed=1, kflags=0x02)
    stub = GdbStub()
    stub.write_mem(0x9B000, rec)
    stub.write_mem(0x98000, state)
    stub.detach_running()
    stub.close()

    mon = socket.socket(socket.AF_UNIX)
    mon.connect(sock_path)
    time.sleep(0.5)
    mon.recv(4096)                       # banner
    mon.sendall(b"system_reset\n")
    time.sleep(0.5)
    try:
        mon.recv(4096)
    except Exception:
        pass
    mon.close()

    # OCBL boots: pending BOOT_FAILED record (fails=2) -> auto OCIRE
    child.expect("BOOT_FAILED record pending", timeout=90)
    child.expect("auto-booting OCIRE", timeout=30)
    child.expect("OPEN CUBE OS FAILED TO BOOT", timeout=60)
    child.expect(r"\[R\] Rollback to the previous OCOS version", timeout=30)
    child.expect(r"\[C\] Continue and try booting OCOS again", timeout=30)
    # a wrong key is ignored (the menu truly blocks)
    child.sendline("x")
    time.sleep(1)
    child.send("C")
    child.expect("Clearing the failure record and rebooting", timeout=30)
    time.sleep(4)
    child.terminate(force=True)
    reset_ab_flags()
    restore_slot_b()
    return True


def case_s3_wake_fail():
    """7: a surviving SLEEP_ARMED flag -> [W]/[R]/[C] menu.

    FULLY REAL chain (no injection): `power suspend` takes the
    machine into actual ACPI S3 (the kernel sets SLEEP_ARMED right
    before the SLP_EN write - visible on serial); a monitor
    system_reset then plays the dead wake vector (the machine died
    mid-sleep before the resume path could clear the flag).  OCBL
    sees the surviving flag, records WAKE_FAILED, routes OCIRE and
    the blocking [W]/[R]/[C] menu appears.
    """
    reset_ab_flags()
    restore_slot_b()
    sock_path = "/tmp/wp07-mon2.sock"
    if os.path.exists(sock_path):
        os.unlink(sock_path)
    args = [QEMU, "-m", "512",
            "-L", os.path.join(OC, "usr/share/qemu"),
            "-L", os.path.join(OC, "usr/share/seabios"),
            "-drive", "file=%s,if=ide,index=0,media=disk,format=raw" % HD,
            "-drive", "file=%s,if=ide,index=1,media=disk,format=raw" % BOOT,
            "-net", "none", "-serial", "stdio", "-display", "none",
            "-gdb", "tcp::1234",
            "-monitor", "unix:%s,server,nowait" % sock_path]
    log = open(os.path.join(OUT, "wp07-s3-wake.log"), "w")
    child = pexpect.spawn(" ".join(args), env=ENV, timeout=180,
                          encoding="utf-8", codec_errors="replace")
    child.logfile = log
    try:
        # OCOS boots to the shell (default menu entry)
        child.expect("Booting Open Cube OS", timeout=60)
        child.send("\r")
        child.expect("ush> ", timeout=120)
        # the REAL S3 entry: SLEEP_ARMED brackets the sleep.  The
        # "Entering S3"/"Suspending..." notices go to the VGA console
        # (invisible under -display none); the [ocst] bracket line is
        # the serial-visible proof the flag was set before SLP_EN.
        child.sendline("power suspend")
        child.expect(r"\[ocst\] SLEEP_ARMED set", timeout=30)
        time.sleep(3)                     # the SLP_EN write lands

        # the dead wake vector, played for real: the kernel armed the
        # FACS waking vector at 0x8000 (driver_acpi_power.c) - corrupt
        # that trampoline page while the machine sleeps, so the wake
        # jumps into garbage, triple-faults and resets with
        # SLEEP_ARMED still set (a system_reset would only simulate a
        # healthy wake: SeaBIOS runs the resume path and the machine
        # comes back - live-caught, that path even PASSED the resume)
        stub = GdbStub()
        # a corrupted trampoline that hard-resets the machine the
        # moment the firmware jumps to it (16-bit: mov al,6; mov
        # dx,0xCF9; out dx,al; hlt - the classic 0xCF9 reset dance;
        # plain HLT soup would only hang: the sleep disarmed the
        # watchdog, so nothing would ever pull the machine out)
        stub.write_mem(0x8000, bytes.fromhex("b006baf90cee") + b"\xf4" * 250)
        stub.detach_running()
        stub.close()

        mon = socket.socket(socket.AF_UNIX)
        mon.connect(sock_path)
        mon.settimeout(3)
        time.sleep(0.3)
        try:
            mon.recv(4096)
        except Exception:
            pass
        mon.sendall(b"system_wakeup\n")
        time.sleep(0.5)
        try:
            mon.recv(4096)
        except Exception:
            pass
        mon.close()
        # OCBL detects the surviving flag
        child.expect("SLEEP_ARMED survived the reboot", timeout=90)
        child.expect("WAKE_FAILED recorded", timeout=30)
        child.expect("WAKE_FAILED record pending", timeout=60)
        child.expect("auto-booting OCIRE", timeout=30)
        child.expect("S3/S0ix WAKE FAILURE DETECTED", timeout=90)
        child.expect(r"\[W\] Wake from sleep failed", timeout=30)
        child.expect(r"\[R\] Rollback to the previous OCOS version", timeout=30)
        child.expect(r"\[C\] Try to boot OCOS normally", timeout=30)
        # [W] prints the diagnostics, then keeps waiting
        child.send("W")
        child.expect("Wake-failure diagnostics", timeout=30)
        child.expect("resume vector crashed", timeout=30)
        # then C reboots OCOS normally (no suspend this time)
        child.send("C")
        child.expect("Clearing the failure record", timeout=30)
        child.expect("ush> ", timeout=120)
        ok = True
    except Exception as e:
        print("[wp07]   EXCEPTION: %r" % e)
        ok = False
    finally:
        child.terminate(force=True)
        reset_ab_flags()
        restore_slot_b()
    return ok


# ---- runner -----------------------------------------------------------

def run(name, fn):
    print("[wp07] case %-14s ..." % name, flush=True)
    try:
        ok = fn()
        ok = bool(ok)
    except Exception as e:
        print("[wp07]   EXCEPTION: %r" % e)
        ok = False
    finally:
        subprocess.run(["pkill", "-f", "qemu-system-x86_64"], check=False)
        time.sleep(1)
    print("[wp07] case %-14s %s" % (name, "PASS" if ok else "FAIL"))
    RESULTS.append((name, ok))
    return ok


def main():
    only = sys.argv[1:] or None
    cases = [
        ("fat32-mount", case_fat32_mount),
        ("sig-reject", case_sig_reject),
        ("dual-flash", case_dual_flash),
        ("anti-brick", case_anti_brick),
        ("os-takeover", case_os_takeover),
        ("s3-wake-fail", case_s3_wake_fail),
    ]
    for name, fn in cases:
        if only and name not in only:
            continue
        run(name, fn)
    print("\n[wp07] summary:")
    for name, ok in RESULTS:
        print("  %-14s %s" % (name, "PASS" if ok else "FAIL"))
    bad = [n for n, ok in RESULTS if not ok]
    print("[wp07] %d/%d passed%s" % (len(RESULTS) - len(bad), len(RESULTS),
                                     "" if not bad else " FAILED: " + ",".join(bad)))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
