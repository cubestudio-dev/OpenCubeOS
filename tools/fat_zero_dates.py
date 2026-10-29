#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""
fat_zero_dates.py: scrub every FAT12/16 date/time field in an mtools
image so two ISO builds minutes apart stay byte-identical (the G13
clean-build determinism gate).  mformat/mmd/mcopy stamp current wall
clock into directory entries; this walker zeroes the date fields only
(creation time/date, last-access date, write time/date, and the
1/10-second byte), leaving names, attributes, cluster chains, and file
sizes untouched.

Usage: python3 tools/fat_zero_dates.py <image>
"""
import sys


def u16(b, o):
    return b[o] | (b[o + 1] << 8)


def u32(b, o):
    return b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)


def fat12_entry(fat, idx):
    off = idx // 2 * 3
    return (fat[off] | (fat[off + 1] << 8)) >> (4 * (idx & 1)) & 0xFFF


def zero_entry(img, e):
    # bytes 13 (crt tenth), 14-15 (crt time), 16-17 (crt date),
    # 18-19 (last access date), 22-23 (write time), 24-25 (write date)
    for i in list(range(13, 20)) + list(range(22, 26)):
        img[e + i] = 0


def walk_chain(img, first_clus, bps, spc, fat):
    seen = set()
    c = first_clus
    while 2 <= c < 0xFF8 and c not in seen:
        seen.add(c)
        yield (data_start + (c - 2) * spc * bps, spc * bps)
        c = fat12_entry(fat, c)


def walk_dir(img, base, size, depth, bps, spc, fat):
    end = min(base + size, len(img))
    e = base
    while e + 32 <= end:
        first = img[e]
        if first == 0:
            break
        attr = img[e + 11]
        # zero dates on every real entry: volume labels (attr 0x08)
        # and subdirectory "."/".." entries carry creation dates too;
        # ONLY long-file-name fragments (attr 0x0F) hold name bytes in
        # the date fields and must stay untouched.
        if first != 0xE5 and attr != 0x0F:
            zero_entry(img, e)
        # descend into subdirectories (not volume labels)
        if first != 0xE5 and (attr & 0x10) and not (attr & 0x08) and depth < 6:
            clus = u16(img, e + 26)
            for (dbase, dsize) in walk_chain(img, clus, bps, spc, fat):
                walk_dir(img, dbase, dsize, depth + 1, bps, spc, fat)
        e += 32


def main():
    path = sys.argv[1]
    img = bytearray(open(path, "rb").read())
    bps = u16(img, 11)
    spc = img[13]
    reserved = u16(img, 14)
    nfats = img[16]
    root_entries = u16(img, 17)
    total16 = u16(img, 19)
    total32 = u32(img, 32)
    fat_size = u16(img, 22)
    total = total16 if total16 else total32
    fat_start = reserved * bps
    global data_start
    root_start = fat_start + nfats * fat_size * bps
    root_size = root_entries * 32
    data_start = root_start + root_size
    fat = bytes(img[fat_start:fat_start + fat_size * bps])
    walk_dir(img, root_start, root_size, 0, bps, spc, fat)
    # sanity: never grow or move data
    assert len(img) == len(open(path, "rb").read())
    open(path, "wb").write(bytes(img))
    print(f"fat_zero_dates: scrubbed {path} "
          f"(bps={bps} spc={spc} root@{root_start:#x} data@{data_start:#x})")


data_start = 0
if __name__ == "__main__":
    main()
