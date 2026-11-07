#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - 3.0.0-WP05-ocbl
# File: ocbl/make_scan_disks.py
# Purpose: build the B2b scanner test-fleet images - one disk per
#          boot-entry class plus boundary cases:
#
#   hd0.img  128 MiB  MBR  p1 FAT32 32 MiB  OCOS flags volume
#                     (/boot/opencube.elf stub + /boot/grub/grub.cfg
#                     + /boot/next_B flag)  p2 FAT32 slot A
#                     (/boot/opencube.elf)  p3 FAT32 slot B
#                     (/boot/opencube.elf) -> expected: slot B entry
#   hd1.img   16 MiB  MBR single 0xEE shadow entry (no GPT header,
#                     the WP-02 shape) over a FAT12 volume with
#                     /boot/ocire.elf     -> OCIRE entry
#   hd2.img   36 MiB  GPT ESP FAT32 with
#                     /EFI/Microsoft/Boot/bootmgfw.efi (LFN!) -> Windows
#   hd3.img   36 MiB  GPT ESP FAT32 with /EFI/ubuntu/grubx64.efi -> Linux
#   hd4.img   36 MiB  GPT ESP FAT32 with /EFI/BOOT/BOOTX64.EFI -> generic
#   fat0.img  16 MiB  NO partition table, whole-disk FAT16 OCOS
#                     (/boot/opencube.elf)  -> superfloppy OCOS entry
#   bare0.img 16 MiB  MBR signature only, garbage inside -> 0 entries
#   cd0.iso           ISO9660 with /ocse.marker and a /boot dir
#                     (must NOT register as OCOS)           -> OCSE
#
# FAT geometry is fixed (h16 s63); the volumes are formatted with
# mformat and populated with mcopy, then dd'ed into the disk images at
# their partition offsets; MBR/GPT bytes are written directly.
import os
import struct
import subprocess
import sys

OUT = sys.argv[1] if len(sys.argv) > 1 else "build/ocbl/scan_disks"
MODE = ""
if "--rollback" in sys.argv:
    MODE = "rollback"
elif "--empty" in sys.argv:
    MODE = "empty"
elif "--many" in sys.argv:
    MODE = "many"
MANY_N = 0
for a in sys.argv:
    if a.startswith("--many="):
        MANY_N = int(a.split("=")[1])
BIN = "/home/z/opt/extract/usr/bin"
ENV = dict(os.environ,
           PATH=BIN + ":" + os.environ.get("PATH", ""),
           LD_LIBRARY_PATH="/home/z/opt/extract/usr/lib/x86_64-linux-gnu:"
                           "/home/z/opt/extract/lib/x86_64-linux-gnu")

H, S = 16, 63
SPU = H * S              # sectors per unit (track)

MBR_MAGIC = 0xAA55
GPT_ESP_GUID = bytes.fromhex("28732ac11ff8d211ba4b00a0c93ec93b")


def run(cmd):
    r = subprocess.run(cmd, env=ENV, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT)
    if r.returncode != 0:
        print("[scan_disks] FAIL:", " ".join(cmd))
        print(r.stdout.decode(errors="replace"))
        sys.exit(1)


def mk_elf_stub(mb2=True, note=b"OCBL scan stub"):
    """64+ byte file; with mb2=True it embeds a valid multiboot2
    header (magic + arch 0 + headerlen 0x30 + checksum)."""
    if not mb2:
        return note + b"\x00" * (128 - len(note))
    magic = 0xE85250D6
    arch = 0
    hlen = 0x30
    checksum = (-(magic + arch + hlen)) & 0xFFFFFFFF
    hdr = struct.pack("<IIII", magic, arch, hlen, checksum)
    body = note
    return hdr + body + b"\x00" * (256 - len(hdr) - len(body))


def mk_fat(size_mb, files, fat32=False):
    """Format a detached FAT volume file (h16 s63 geometry) and copy
    `files` (dict path->bytes) into it with mtools.  size_mb chosen
    by the caller so the FAT type comes out right:
      ~4.4 MiB  -> FAT12,  ~17 MiB -> FAT16,  >=35 MiB -> FAT32."""
    secs = size_mb * 1024 * 1024 // 512
    tracks = secs // SPU
    secs = tracks * SPU
    path = OUT + "/_vol.fat"
    with open(path, "wb") as f:
        f.truncate(secs * 512)
    run([BIN + "/mformat", "-i", path, "-h", str(H), "-s", str(S),
         "-t", str(tracks)] + (["-F"] if fat32 else []))
    seen_dirs = set()

    def ensure_dir(vol_path, p):
        parts = [x for x in p.split("/") if x]
        acc = ""
        for comp in parts[:-1]:
            acc += "/" + comp
            if acc in seen_dirs:
                continue
            run([BIN + "/mmd", "-i", vol_path, "::" + acc])
            seen_dirs.add(acc)

    for p, data in files.items():
        ensure_dir(path, p)
        tmp = OUT + "/_copy.tmp"
        with open(tmp, "wb") as f:
            f.write(data)
        run([BIN + "/mcopy", "-i", path, tmp,
             "::" + p.replace("//", "/")])
        os.unlink(tmp)
    return path, secs


def dd_in(disk, vol_path, lba, nsec):
    with open(vol_path, "rb") as v, open(disk, "r+b") as d:
        d.seek(lba * 512)
        left = nsec * 512
        while left:
            chunk = v.read(min(1 << 20, left))
            if not chunk:
                break
            d.write(chunk)
            left -= len(chunk)


def mbr_entry(type_, lba, nsec, boot=0):
    chs_start = b"\xfe\xff\xff"
    chs_end = b"\xfe\xff\xff"
    # boot(1) chs_start(3) type(1) chs_end(3) lba(4) nsec(4) = 16 B
    return (bytes([boot]) + chs_start + bytes([type_]) + chs_end +
            struct.pack("<II", lba, nsec))


def write_mbr(disk, entries):
    tbl = b""
    for e in entries:
        tbl += mbr_entry(*e)
    tbl += b"\x00" * (64 - len(tbl))
    with open(disk, "r+b") as d:
        d.write(b"\x00" * 440 + b"\x00\x00\x00\x00" + b"\x00\x00" + tbl)
        d.seek(510)
        d.write(struct.pack("<H", MBR_MAGIC))


def guid_bytes():
    import uuid
    return uuid.uuid4().bytes_le


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
    os.makedirs(OUT, exist_ok=True)
    os_elf = mk_elf_stub(True)
    plain = mk_elf_stub(False)
    # real payloads when the kernels are built (B4a: OCBL loads the
    # actual opencube.elf / ocire.elf, not the stubs)
    real_ocos = OUT + "/_real_ocos.elf"
    real_ocire = OUT + "/_real_ocire.elf"
    import shutil
    if os.path.exists("build/opencube.elf"):
        shutil.copy("build/opencube.elf", real_ocos)
        os_elf = open(real_ocos, "rb").read()
    if os.path.exists("build/ocire/ocire.elf"):
        shutil.copy("build/ocire/ocire.elf", real_ocire)

    secs2 = 36 * 1024 * 1024 // 512
    if MODE == "many":
        # scroll fleet: one OCOS disk + MANY_N generic ESP disks -
        # the menu rows (1 + N + Shutdown) must exceed the 19-row
        # window so the scroll path is exercised
        n = MANY_N if MANY_N else 22
        hd0 = OUT + "/hd0.img"
        with open(hd0, "wb") as f:
            f.truncate(16 * 1024 * 1024)
        vol, _ = mk_fat(5, {"/boot/opencube.elf": os_elf})
        dd_in(hd0, vol, 32, 9 * SPU)
        write_mbr(hd0, [(0x0C, 32, 9 * SPU, 0x80)])
        for i in range(n):
            p = OUT + f"/g{i}.img"
            with open(p, "wb") as f:
                f.truncate(secs2 * 512)
            vol, _ = mk_fat(38, {"/EFI/BOOT/BOOTX64.EFI": plain},
                            fat32=True)
            dd_in(p, vol, 2048, 68 * SPU)
            write_gpt(p, [(GPT_ESP_GUID, 2048, 2048 + 68 * SPU - 1,
                           "EFI system partition")], secs2)
        print(f"many fleet ready in {OUT} ({n} generics)")
        return

    if MODE == "empty":
        # boundary fleet: all disks present but no bootable payload
        bare0 = OUT + "/bare0.img"
        with open(bare0, "wb") as f:
            f.truncate(16 * 1024 * 1024)
        write_mbr(bare0, [(0x0C, 2048, 32736, 0)])
        for name in ("hd1.img", "hd2.img", "hd3.img", "hd4.img",
                     "fat0.img"):
            with open(OUT + "/" + name, "wb") as f:
                f.write(open(bare0, "rb").read())
        # a non-OCSE CD (no marker): our own test ISO shape
        stage = OUT + "/iso_stage"
        os.makedirs(stage + "/boot", exist_ok=True)
        with open(stage + "/boot/opencube.elf", "wb") as f:
            f.write(os_elf)
        run([BIN + "/xorriso", "-as", "mkisofs", "-quiet", "-V",
             "PLAIN_TEST", "-o", OUT + "/cd0.iso", stage])
        os.unlink(bare0)
        print("empty fleet ready in", OUT)
        return

    # ---------- hd0: OCOS flags + slot A/B ----------
    hd0 = OUT + "/hd0.img"
    with open(hd0, "wb") as f:
        f.truncate(128 * 1024 * 1024)
    p1_lba, p1_n = 2048, 32 * 1024 * 1024 // 512
    p2_lba, p2_n = p1_lba + p1_n, 32 * 1024 * 1024 // 512
    p3_lba, p3_n = p2_lba + p2_n, 128 * 1024 * 1024 // 512 - \
        (p2_lba + p2_n)
    flags = {
        "/boot/opencube.elf": os_elf,
        "/boot/grub/grub.cfg": b"# legacy flag volume\n",
        "/boot/next_B": b"B\n",
    }
    if MODE == "rollback":
        flags["/boot/bootfail_B"] = b"fail\n"
    vol, _ = mk_fat(38, flags)
    dd_in(hd0, vol, p1_lba, p1_n)
    vol, _ = mk_fat(38, {"/boot/opencube.elf": os_elf})
    dd_in(hd0, vol, p2_lba, p2_n)      # slot A
    vol, _ = mk_fat(38, {"/boot/opencube.elf": os_elf})
    dd_in(hd0, vol, p3_lba, p3_n)      # slot B
    write_mbr(hd0, [(0x0C, p1_lba, p1_n, 0x80),
                    (0x0C, p2_lba, p2_n, 0),
                    (0x0C, p3_lba, p3_n, 0)])
    print("hd0 ok (OCOS flags + slots)")

    # ---------- hd1: OCIRE 0xEE shadow ----------
    hd1 = OUT + "/hd1.img"
    with open(hd1, "wb") as f:
        f.truncate(16 * 1024 * 1024)
    sh_lba, sh_n = 32, 9 * SPU          # ~4.5 MiB FAT12 shadow
    ocire_blob = open(real_ocire, "rb").read() \
        if os.path.exists(real_ocire) else os_elf
    vol, _ = mk_fat(5, {"/boot/ocire.elf": ocire_blob,
                        "/golden/MANIFEST.TXT": b"golden\n"})
    dd_in(hd1, vol, sh_lba, sh_n)
    write_mbr(hd1, [(0xEE, sh_lba, sh_n, 0)])
    print("hd1 ok (OCIRE shadow)")

    # ---------- hd2: Windows GPT ESP ----------
    hd2 = OUT + "/hd2.img"
    with open(hd2, "wb") as f:
        f.truncate(secs2 * 512)
    esp_lba, esp_last = 2048, 2048 + 68 * SPU - 1
    vol, _ = mk_fat(38, {
        "/EFI/Microsoft/Boot/bootmgfw.efi": plain,
        "/EFI/BOOT/BOOTX64.EFI": plain,
    }, fat32=True)
    dd_in(hd2, vol, esp_lba, 68 * SPU)
    write_gpt(hd2, [(GPT_ESP_GUID, esp_lba, esp_last,
                     "EFI system partition")], secs2)
    print("hd2 ok (Windows ESP; BOOTX64 also present - Windows wins)")

    # ---------- hd3: Linux GPT ESP ----------
    hd3 = OUT + "/hd3.img"
    with open(hd3, "wb") as f:
        f.truncate(secs2 * 512)
    vol, _ = mk_fat(38, {"/EFI/ubuntu/grubx64.efi": plain}, fat32=True)
    dd_in(hd3, vol, esp_lba, 68 * SPU)
    write_gpt(hd3, [(GPT_ESP_GUID, esp_lba, esp_last,
                     "EFI system partition")], secs2)
    print("hd3 ok (Linux ESP)")

    # ---------- hd4: generic ESP ----------
    hd4 = OUT + "/hd4.img"
    with open(hd4, "wb") as f:
        f.truncate(secs2 * 512)
    vol, _ = mk_fat(38, {"/EFI/BOOT/BOOTX64.EFI": plain}, fat32=True)
    dd_in(hd4, vol, esp_lba, 68 * SPU)
    write_gpt(hd4, [(GPT_ESP_GUID, esp_lba, esp_last,
                     "EFI system partition")], secs2)
    print("hd4 ok (generic ESP)")

    # ---------- fat0: whole-disk FAT16 OCOS (no table) ----------
    fat0 = OUT + "/fat0.img"
    vol, _ = mk_fat(17, {"/boot/opencube.elf": os_elf})
    with open(fat0, "wb") as f:
        f.write(open(vol, "rb").read())
    print("fat0 ok (superfloppy OCOS)")

    # ---------- bare0: MBR signature, garbage volumes ----------
    bare0 = OUT + "/bare0.img"
    with open(bare0, "wb") as f:
        f.truncate(16 * 1024 * 1024)
    write_mbr(bare0, [(0x0C, 2048, 32736, 0)])
    print("bare0 ok (garbage FAT area, no entry expected)")

    # ---------- hd5: chainload disk --------------------------------
    # Whole-disk FAT12 with /EFI/BOOT/BOOTX64.EFI (registers as the
    # generic EFI loader entry); the boot sector's code area (0x3E+)
    # is patched with a 16-bit stub that prints 'C','L','!' over COM1
    # and halts - proving OCBL dropped to real mode and jumped.
    hd5 = OUT + "/hd5.img"
    vol, _ = mk_fat(5, {"/EFI/BOOT/BOOTX64.EFI": plain})
    stub = bytes([0xFA])                                  # cli
    stub += bytes([0xBA, 0xF8, 0x03])                     # mov dx,3F8
    for ch in (0x43, 0x4C, 0x21):                         # 'C','L','!'
        stub += bytes([0xB0, ch, 0xEE])                   # mov al,ch; out
    stub += bytes([0xF4])                                 # hlt
    stub += bytes([0xEB, 0xFD])                           # jmp $
    with open(vol, "r+b") as f:
        f.seek(0x3E)
        f.write(stub)
    with open(hd5, "wb") as f:
        f.write(open(vol, "rb").read())
    print("hd5 ok (chainload boot sector)")

    # ---------- cd0: OCSE install media ----------
    stage = OUT + "/iso_stage"
    os.makedirs(stage + "/boot", exist_ok=True)
    with open(stage + "/ocse.mrk", "w") as f:
        f.write("OCSE install media marker\n")
    with open(stage + "/boot/opencube.elf", "wb") as f:
        f.write(os_elf)
    cd0 = OUT + "/cd0.iso"
    run([BIN + "/xorriso", "-as", "mkisofs", "-quiet", "-V",
         "OCSE_TEST", "-o", cd0, stage])
    print("cd0 ok (OCSE ISO; the /boot kernel must NOT register)")

    for t in ("_vol.fat",):
        p = OUT + "/" + t
        if os.path.exists(p):
            os.unlink(p)
    print("fleet ready in", OUT)


if __name__ == "__main__":
    main()
