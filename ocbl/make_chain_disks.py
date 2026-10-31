#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - 3.0.0-WP05-ocbl
# File: ocbl/make_chain_disks.py
# Purpose: build the B5-6 chainload test disks:
#
#   chain0.img  36 MiB  GPT ESP FAT32 holding /EFI/BOOT/BOOTX64.EFI =
#             the REAL OCBL x64 PE application (we chainload ourselves:
#             the second-level OCBL re-scans, renders the menu and parks
#             - a self-contained LoadImage/StartImage positive proof
#             with no foreign binary in the tree).
#   chain1.img  36 MiB  GPT ESP FAT32 holding /EFI/BOOT/BOOTX64.EFI =
#             a non-PE stub (the B2b generic shape) - LoadImage must
#             refuse it honestly (EFI_LOAD_ERROR), never a silent hang.
#
# Both carry the same partition GUID shape as make_scan_disks.py hd4.
import os
import struct
import subprocess
import sys

BIN = "/home/z/opt/extract/usr/bin"
ENV = dict(os.environ,
           PATH=BIN + ":" + os.environ.get("PATH", ""),
           LD_LIBRARY_PATH="/home/z/opt/extract/usr/lib/x86_64-linux-gnu:"
                           "/home/z/opt/extract/lib/x86_64-linux-gnu")

H, S = 16, 63
MBR_MAGIC = 0xAA55
SPU = H * S
GPT_ESP_GUID = bytes.fromhex("28732ac11ff8d211ba4b00a0c93ec93b")
LBA_OFF = 2048                      # partition start (1 MiB)
PART_SECS = 68 * SPU


def run(cmd):
    r = subprocess.run(cmd, env=ENV, stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT)
    if r.returncode != 0:
        print("[chain_disks] FAIL:", " ".join(cmd))
        print(r.stdout.decode(errors="replace"))
        sys.exit(1)


def mbr_entry(type_, lba, nsec, boot=0):
    chs_start = b"\xfe\xff\xff"
    chs_end = b"\xfe\xff\xff"
    # boot(1) chs_start(3) type(1) chs_end(3) lba(4) nsec(4) = 16 B
    return (bytes([boot]) + chs_start + bytes([type_]) + chs_end +
            struct.pack("<II", lba, nsec))

def write_gpt(disk, parts, disk_sectors):
    """parts: list of (type_guid, first_lba, last_lba, name).
    Header layout (UEFI spec, 92 bytes):
      0-7 signature, 8-11 revision, 12-15 header size (92), 16-19 CRC,
      20-23 reserved, 24-31 current LBA(1), 32-39 backup LBA,
      40-47 first usable(34), 48-55 last usable(n-34), 56-71 disk GUID,
      72-79 entries LBA(2), 80-83 entry count, 84-87 entry size(128),
      88-91 entries CRC"""
    import binascii
    import uuid
    # protective MBR
    with open(disk, "r+b") as d:
        d.write(b"\x00" * 446 + mbr_entry(0xEE, 1, min(disk_sectors - 1,
                                                       0xFFFFFFFF)))
        d.seek(510)
        d.write(struct.pack("<H", MBR_MAGIC))
    nent = 128
    ent = b""
    for tg, first, last, name in parts:
        e = (tg + uuid.uuid4().bytes_le +
             struct.pack("<QQQ", first, last, 0) +
             name.encode("utf-16-le").ljust(72, b"\x00"))
        ent += e
    ent += b"\x00" * (nent * 128 - len(ent))
    with open(disk, "r+b") as d:
        d.seek(2 * 512)
        d.write(ent[:34 * 512])          # entries at LBA 2..35
    ent_crc = binascii.crc32(ent) & 0xFFFFFFFF
    hdr = (b"EFI PART" +
           struct.pack("<III", 0x00010000, 92, 0) +
           struct.pack("<I", 0) +                    # reserved
           struct.pack("<QQ", 1, disk_sectors - 1) +  # current/backup
           struct.pack("<QQ", 34, disk_sectors - 34) +  # usable
           uuid.uuid4().bytes_le +                   # disk GUID
           struct.pack("<Q", 2) +                    # entries LBA
           struct.pack("<II", nent, 128) +           # count, size
           struct.pack("<I", ent_crc))
    assert len(hdr) == 92, len(hdr)
    crc = binascii.crc32(hdr) & 0xFFFFFFFF
    hdr = hdr[:16] + struct.pack("<I", crc) + hdr[20:]
    with open(disk, "r+b") as d:
        d.seek(1 * 512)
        d.write(hdr)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "build/ocbl/scan_disks"
    os.makedirs(out, exist_ok=True)
    total = 36 * 1024 * 1024 // 512

    for name, src in (
        ("chain0.img", "build/ocbl/ocbl_x64.efi"),   # real PE
        ("chain1.img", None),                          # non-PE stub
    ):
        path = os.path.join(out, name)
        with open(path, "wb") as f:
            f.truncate(36 * 1024 * 1024)
        # the FAT volume (38 MiB worth of sectors is NOT needed - the
        # partition is 68 tracks * 1008 = 68544 sectors ~ 33.4 MiB)
        secs = PART_SECS
        tracks = secs // SPU
        secs = tracks * SPU
        vol = os.path.join(out, "_chain.fat")
        with open(vol, "wb") as f:
            f.truncate(secs * 512)
        run([BIN + "/mformat", "-i", vol, "-h", str(H), "-s", str(S),
             "-t", str(tracks), "-F"])
        run([BIN + "/mmd", "-i", vol, "::/EFI"])
        run([BIN + "/mmd", "-i", vol, "::/EFI/BOOT"])
        tmp = os.path.join(out, "_chain_payload.tmp")
        if src:
            data = open(src, "rb").read()
        else:
            data = b"OCBL chain stub - NOT a PE image" + b"\0" * 96
        with open(tmp, "wb") as f:
            f.write(data)
        run([BIN + "/mcopy", "-i", vol, tmp, "::/EFI/BOOT/BOOTX64.EFI"])
        os.unlink(tmp)
        # splice the volume into the image at LBA 2048
        with open(vol, "rb") as v, open(path, "r+b") as d:
            d.seek(LBA_OFF * 512)
            d.write(v.read())
        write_gpt(path, [(GPT_ESP_GUID, LBA_OFF, LBA_OFF + PART_SECS - 1,
                          "EFI system partition")], total)
        print(f"[chain_disks] {name}: {len(data)} B payload, ESP at "
              f"LBA {LBA_OFF}")
    os.unlink(os.path.join(out, "_chain.fat"))
    print("chain disks ready in", out)


if __name__ == "__main__":
    main()
