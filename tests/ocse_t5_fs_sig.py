#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - tests/ocse_t5_fs_sig.py (3.0.0-WP06a-ocse)
# Disk identification acceptance:
#   disk0: 128 MiB MBR disk with four partitions carrying REAL
#          filesystem signatures - FAT32 boot-sector tag, exFAT boot
#          tag, NTFS boot tag, and the ext2/3/4 superblock magic
#          (0xEF53 at byte 1024+56).
#   disk1: 32 MiB blank disk - below the 64 MiB installer minimum.
# Expected: `disks` classifies all four partitions by signature and
# reports the honest low-space notice for the tiny disk.
import os
import socket
import struct
import time
import pexpect
import sys

BUILD = "/home/z/oc-public/build"
BIG = f"{BUILD}/ocse-test-disk-fs.raw"
SMALL = f"{BUILD}/ocse-test-disk-tiny.raw"
MON = f"{BUILD}/t5-mon.sock"
SS = 512


def build_fixtures():
    with open(BIG, "wb") as f:
        f.truncate(128 * 1024 * 1024)
        parts = [
            (0x0B, 2048,         32768, "fat32"),   # 16 MiB each
            (0x07, 2048 + 32768,  32768, "exfat"),
            (0x07, 2048 + 65536,  32768, "ntfs"),
            (0x83, 2048 + 98304, 32768, "ext4"),
        ]
        mbr = bytearray(SS)
        for i, (typ, start, size, _) in enumerate(parts):
            e = 446 + i * 16
            mbr[e + 0] = 0x00
            mbr[e + 4] = typ
            struct.pack_into("<I", mbr, e + 8, start)
            struct.pack_into("<I", mbr, e + 12, size)
        mbr[510:512] = b"\x55\xAA"
        f.seek(0)
        f.write(bytes(mbr))
        for _, start, _, kind in parts:
            off = start * SS
            if kind == "fat32":
                f.seek(off + 0x52); f.write(b"FAT32   ")
            elif kind == "exfat":
                f.seek(off + 0x03); f.write(b"EXFAT   ")
            elif kind == "ntfs":
                f.seek(off + 0x03); f.write(b"NTFS    ")
            elif kind == "ext4":
                f.seek(off + 0x438); f.write(b"\x53\xEF")
    with open(SMALL, "wb") as f:
        f.truncate(32 * 1024 * 1024)


build_fixtures()
if os.path.exists(MON):
    os.unlink(MON)

QEMU = "qemu-system-x86_64"
CMD = (f"{QEMU} -L /home/z/opt/extract/usr/share/qemu "
       f"-L /home/z/opt/extract/usr/share/seabios -m 256M "
       f"-drive file={BIG},format=raw,if=ide,index=0,media=disk "
       f"-drive file={SMALL},format=raw,if=ide,index=1,media=disk "
       f"-cdrom {BUILD}/ocse.iso -boot d "
       f"-no-reboot -display none -serial stdio "
       f"-monitor unix:{MON},server,nowait -vga std")

child = pexpect.spawn(CMD, timeout=180, encoding="utf-8",
                      codec_errors="replace")
child.logfile = sys.stdout


class Mon:
    def __init__(self, path, wait=30):
        for _ in range(wait * 2):
            try:
                self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                self.s.connect(path)
                self.s.settimeout(0.2)
                try:
                    while self.s.recv(4096):
                        pass
                except socket.timeout:
                    pass
                return
            except OSError:
                time.sleep(0.5)
        raise RuntimeError("monitor never came up")

    def cmd(self, c, settle=0.4):
        self.s.sendall((c + "\n").encode())
        time.sleep(settle)
        try:
            self.s.recv(65536)
        except socket.timeout:
            pass

    def type_line(self, text):
        for ch in text:
            self.cmd(f"sendkey {'spc' if ch == ' ' else ch}", 0.10)
        self.cmd("sendkey ret", 0.6)


try:
    mon = Mon(MON)
    child.expect("checks: cpu=PASS mem=PASS disp=PASS disk=PASS", timeout=150)
    child.expect("environment up: GUI mode", timeout=30)
    mon.cmd("sendkey ctrl-alt-f2", 0.8)
    child.expect("switched: CLI mode", timeout=30)

    mon.type_line("disks")
    # disk0: four signature-classified partitions + 64 MiB (no low-space)
    child.expect(r"hda \[MBR\] 128 MiB, 4 part\(s\):", timeout=30)
    child.expect(r"#0 FAT32 lba=2048", timeout=20)
    child.expect(r"#1 exFAT lba=34816", timeout=20)
    child.expect(r"#2 NTFS lba=67584", timeout=20)
    child.expect(r"#3 ext lba=100352", timeout=20)
    print("[T5.1 PASS] signature classification: FAT32/exFAT/NTFS/ext4")

    # disk1: 32 MiB, no table, honest low-space notice
    child.expect(r"hdb \[no table\] 32 MiB", timeout=20)
    child.expect(r"\(!\) low space", timeout=20)
    print("[T5.2 PASS] low-space notice on the 32 MiB disk")

    # zh path: the low-space message converts to GB2312 like all other
    # catalog lines (visible as U+FFFD through the UTF-8 decoder)
    mon.type_line("lang zh")
    child.expect("\ufffd", timeout=20)
    mon.type_line("disks")
    child.expect(r"hdb", timeout=20)
    child.expect("\ufffd", timeout=20)
    mon.type_line("lang en")
    print("[T5.3 PASS] zh catalog path exercised (GB2312 conversion)")

    print("\n[T5 PASS] disk identification: signatures + low space")
except Exception as e:
    print(f"\n[T5 FAIL] {e}")
    sys.exit(1)
finally:
    try:
        mon.s.close()
    except Exception:
        pass
    child.terminate(force=True)
