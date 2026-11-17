#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""WP-07 evidence shots: VGA screenshots of the four deliverable
proofs (FAT32 ESP listing, signature refusal, the boot-failure
[R]/[C] menu, the wake-failure [W]/[R]/[C] menu) + the dual-slot
partition-table dump.

Uses a VNC display + the QEMU monitor screendump (PPM -> PNG via
Pillow if present, PPM kept otherwise).
"""
import os
import socket
import struct
import subprocess
import sys
import time

import pexpect

OC = os.environ.get("OC_TOOLS", os.path.expanduser("~/opt/extract"))
QEMU = os.path.join(OC, "usr/bin/qemu-system-x86_64")
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "build/ocire")
EV = os.path.join(ROOT, "build/ocire/wp07-evidence")
os.makedirs(EV, exist_ok=True)
ENV = dict(os.environ)
ENV["LD_LIBRARY_PATH"] = (
    os.path.join(OC, "usr/lib/x86_64-linux-gnu") + ":" +
    os.path.join(OC, "lib/x86_64-linux-gnu") + ":" +
    ENV.get("LD_LIBRARY_PATH", ""))

HD = os.path.join(OUT, "ocire-hd.img")
BOOT = os.path.join(OUT, "ocire-boot.img")
ESP = os.path.join(OUT, "esp.img")


def qemu_args(mon_sock, extra=()):
    return [QEMU, "-m", "512",
            "-L", os.path.join(OC, "usr/share/qemu"),
            "-L", os.path.join(OC, "usr/share/seabios"),
            "-drive", "file=%s,if=ide,index=0,media=disk,format=raw" % HD,
            "-drive", "file=%s,if=ide,index=1,media=disk,format=raw" % BOOT,
            "-drive", "file=%s,if=ide,index=2,media=disk,format=raw" % ESP,
            "-net", "none", "-serial", "stdio", "-display", "none",
            "-vga", "std",
            "-monitor", "unix:%s,server,nowait" % mon_sock] + list(extra)


def mon_screendump(mon_sock, path):
    """ask the HMP for a screendump; the -display none VGA still has
    the framebuffer so the dump works"""
    s = socket.socket(socket.AF_UNIX)
    s.connect(mon_sock)
    s.settimeout(5)
    time.sleep(0.3)
    try:
        s.recv(4096)
    except Exception:
        pass
    s.sendall(("screendump %s\n" % path).encode())
    time.sleep(1.2)
    try:
        s.recv(4096)
    except Exception:
        pass
    s.close()
    # wait for the file
    for _ in range(30):
        if os.path.exists(path) and os.path.getsize(path) > 100:
            return True
        time.sleep(0.3)
    return os.path.exists(path)


def ppm_to_png(src, dst):
    try:
        from PIL import Image
        Image.open(src).save(dst)
        return dst
    except Exception:
        return src


def pick_ocire(child):
    child.expect("Booting Open Cube OS", timeout=60)
    child.send("\x1b[A")
    child.expect(r"OCIRE recovery \(slot A", timeout=20)
    child.send("\r")


def shot_fat32_and_sigreject():
    """one boot: ls the ESP (FAT32 proof), then a forged update"""
    mon = "/tmp/wp07-shot1.mon"
    if os.path.exists(mon):
        os.unlink(mon)
    # forged signature on the ESP
    forged = os.path.join(OUT, "forged.sig")
    with open(forged, "wb") as f:
        f.write(bytes((i * 37 + 11) & 0xFF for i in range(64)))
    subprocess.run(["bash", os.path.join(ROOT, "tools/make_esp_disk.sh"),
                    "--pkg", os.path.join(OUT, "ocire.elf"), forged],
                   check=True, stdout=subprocess.DEVNULL)
    child = pexpect.spawn(" ".join(qemu_args(mon)), env=ENV, timeout=150,
                          encoding="utf-8", codec_errors="replace")
    log = open(os.path.join(EV, "01-fat32-sigreject-serial.log"), "w")
    child.logfile = log
    try:
        pick_ocire(child)
        child.expect("Ready. Type 'help'", timeout=90)
        child.sendline("ls")
        child.expect("entr", timeout=30)
        time.sleep(1.0)
        ok = mon_screendump(mon, os.path.join(EV, "01-fat32-esp-listing.ppm"))
        print("[shot] FAT32 ESP listing:", ok)
        child.sendline("update")
        child.expect("REFUSED - nothing was written", timeout=60)
        time.sleep(1.0)
        ok = mon_screendump(mon, os.path.join(EV, "02-sig-reject.ppm"))
        print("[shot] signature refusal:", ok)
        child.sendline("shutdown")
        time.sleep(2)
    except Exception as e:
        print("[shot] ERR:", e)
    finally:
        child.terminate(force=True)
        log.close()


def shot_takeover_menu():
    """/HANGAT twice -> the [R]/[C] takeover menu on screen"""
    mon = "/tmp/wp07-shot2.mon"
    if os.path.exists(mon):
        os.unlink(mon)
    hangat = os.path.join(OUT, ".HANGAT")
    with open(hangat, "w") as f:
        f.write("3")
    subprocess.run(["mcopy", "-i", BOOT + "@@1048576", hangat, "::/HANGAT"],
                   check=True)
    args = qemu_args(mon, ["-device", "i6300esb"])
    child = pexpect.spawn(" ".join(args), env=ENV, timeout=220,
                          encoding="utf-8", codec_errors="replace")
    log = open(os.path.join(EV, "03-takeover-serial.log"), "w")
    child.logfile = log
    try:
        for rnd in (1, 2):
            child.expect(r"\[ocst\] stage 3/6: DRIVERS_READY", timeout=90)
            child.expect(r"HANGAT: deliberate halt", timeout=30)
            child.expect(r"previous boot judged FAILED", timeout=90)
            child.expect("consecutive failure %d" % rnd, timeout=30)
        child.expect("OPEN CUBE OS FAILED TO BOOT", timeout=90)
        child.expect(r"\[C\] Continue and try booting OCOS again", timeout=30)
        time.sleep(1.5)
        ok = mon_screendump(mon, os.path.join(EV, "03-rc-menu.ppm"))
        print("[shot] [R]/[C] takeover menu:", ok)
        child.send("C")
        time.sleep(3)
    except Exception as e:
        print("[shot] ERR:", e)
    finally:
        child.terminate(force=True)
        subprocess.run(["mdel", "-i", BOOT + "@@1048576", "::/HANGAT"],
                       check=False)
        log.close()


def shot_s3_menu():
    """real S3 + dead wake vector -> the [W]/[R]/[C] menu on screen"""
    mon = "/tmp/wp07-shot3.mon"
    if os.path.exists(mon):
        os.unlink(mon)
    args = [a for a in qemu_args(mon)] + ["-gdb", "tcp::1234"]
    child = pexpect.spawn(" ".join(args), env=ENV, timeout=200,
                          encoding="utf-8", codec_errors="replace")
    log = open(os.path.join(EV, "04-s3wake-serial.log"), "w")
    child.logfile = log
    try:
        child.expect("Booting Open Cube OS", timeout=60)
        child.send("\r")
        child.expect("ush> ", timeout=120)
        child.sendline("power suspend")
        child.expect(r"\[ocst\] SLEEP_ARMED set", timeout=30)
        time.sleep(3)
        # dead wake vector
        sys.path.insert(0, os.path.join(ROOT, "tools"))
        from run_wp07_tests import GdbStub
        stub = GdbStub()
        stub.write_mem(0x8000, bytes.fromhex("b006baf90cee") + b"\xf4" * 250)
        stub.detach_running()
        stub.close()
        s = socket.socket(socket.AF_UNIX)
        s.connect(mon)
        s.settimeout(3)
        time.sleep(0.3)
        try:
            s.recv(4096)
        except Exception:
            pass
        s.sendall(b"system_wakeup\n")
        time.sleep(0.5)
        try:
            s.recv(4096)
        except Exception:
            pass
        s.close()
        child.expect(r"\[C\] Try to boot OCOS normally", timeout=120)
        time.sleep(1.5)
        ok = mon_screendump(mon, os.path.join(EV, "04-wrc-menu.ppm"))
        print("[shot] [W]/[R]/[C] wake menu:", ok)
        child.send("C")
        time.sleep(3)
    except Exception as e:
        print("[shot] ERR:", e)
    finally:
        child.terminate(force=True)
        log.close()


def dump_partition_table():
    """the dual-slot partition structure, host-side evidence"""
    txt = os.path.join(EV, "05-dual-slot-partitions.txt")
    with open(HD, "rb") as f, open(txt, "w") as out:
        mbr = f.read(512)
        rel = os.path.relpath(HD, ROOT)
        out.write("OCIRE recovery disk %s (dual independent partitions)\n" % rel)
        out.write("MBR signature: %s\n\n" % mbr[510:512].hex())
        for i in range(4):
            e = mbr[446 + i * 16: 446 + (i + 1) * 16]
            if e[4] == 0:
                continue
            start = struct.unpack("<I", e[8:12])[0]
            n = struct.unpack("<I", e[12:16])[0]
            out.write("p%d type=0x%02X start=%d sectors=%d (%d MiB)\n"
                      % (i + 1, e[4], start, n, n // 2048))
        f.seek(1024 * 512)
        flags = f.read(24)
        out.write("\nA/B flags sector at LBA 1024: magic=%r active=%d "
                  "try=%d attempts=%d\n"
                  % (flags[:8], flags[8], flags[9], flags[10]))
        for lba, name in ((2048, "OCIRE-A"), (133120, "OCIRE-B")):
            f.seek(lba * 512)
            bpb = f.read(512)
            label = bpb[71:82].decode("ascii", "replace").strip()
            rootclus = struct.unpack_from("<I", bpb, 44)[0]
            out.write("slot %s: LBA %d, FAT32 label=%r root_cluster=%d\n"
                      % (name, lba, label, rootclus))
    print("[shot] partition dump ->", txt)
    # sfdisk/parted's view when available (this sandbox has no fdisk)
    for tool in (["sfdisk", "-d", HD], ["parted", "-s", HD, "print"]):
        try:
            r = subprocess.run(tool, capture_output=True, text=True, timeout=30)
        except Exception:
            continue
        if r.returncode == 0 and r.stdout.strip():
            with open(txt, "a") as out:
                out.write("\n--- %s ---\n" % " ".join(tool))
                out.write(r.stdout)
            print("[shot] %s view appended" % tool[0])
            break


def main():
    subprocess.run(["pkill", "-f", "qemu-system-x86_64"], check=False)
    time.sleep(1)
    dump_partition_table()
    shot_fat32_and_sigreject()
    subprocess.run(["pkill", "-f", "qemu-system-x86_64"], check=False)
    time.sleep(1)
    shot_takeover_menu()
    subprocess.run(["pkill", "-f", "qemu-system-x86_64"], check=False)
    time.sleep(1)
    shot_s3_menu()
    # convert PPM -> PNG
    for f in os.listdir(EV):
        if f.endswith(".ppm"):
            ppm_to_png(os.path.join(EV, f),
                       os.path.join(EV, f[:-4] + ".png"))
    print("[shot] evidence in", EV)
    for f in sorted(os.listdir(EV)):
        print("   ", f, os.path.getsize(os.path.join(EV, f)))


if __name__ == "__main__":
    main()
