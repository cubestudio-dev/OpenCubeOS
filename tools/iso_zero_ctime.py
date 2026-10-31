#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""
iso_zero_ctime.py: zero every Rock Ridge TF ctime/atime stamp in an ISO.

The G13 clean-build determinism gate requires two clean builds of the
same tree to be byte-identical.  xorriso pins the volume dates
(--modification-date), the node mtime/atime (--set_all_file_dates) and
the GPT disk GUID (--gpt_disk_guid), but the Rock Ridge TF entries
still carry the node ctime - which xorriso hardcodes to the moment it
assembles the image ("creation time" = now; -alter_date only offers
access/modification, no ctime).  This walker visits every ISO9660
directory record, parses the SUSP entry chain, and rewrites the TF
ctime stamp bytes to the 1970 epoch encoding.  ISO9660 records carry no
checksums, so the byte patch keeps the image valid.

3.0.0-WP04-fix1 (audit finding, fixed on the spot): DIRECTORY nodes
leak one more stamp - xorriso touches the source directory atime while
walking the tree (after any pre-utime), and --set_all_file_dates pins
the atime of FILE nodes only, so two clean builds differed in exactly
the root + per-directory TF atime minute/second bytes.  The walker now
zeroes the TF atime (bit 2) as well; mtime is already pinned epoch by
--set_all_file_dates, so the normalisation stays idempotent.

Usage: python3 tools/iso_zero_ctime.py <image>
"""
import sys

SECTOR = 2048


def u32le(d, o):
    return d[o] | (d[o + 1] << 8) | (d[o + 2] << 16) | (d[o + 3] << 24)


EPOCH7 = bytes([0x46, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00])  # 1970-01-01


def walk_extent(d, lba, size, seen, stats):
    base = lba * SECTOR
    end = base + size
    if base in seen or end > len(d) or size <= 0:
        return
    seen.add(base)
    off = base
    while off < end:
        rl = d[off]
        if rl == 0:                      # padding: next sector
            off = (off // SECTOR + 1) * SECTOR
            continue
        flags = d[off + 25]
        name_len = d[off + 32]
        rec_end = off + rl
        if rec_end > end:
            break
        # SUSP area: even-aligned after the name field
        susp = off + 33 + name_len
        if susp % 2:               # SUSP area starts on an even byte
            susp += 1
        p = susp
        while p + 4 <= rec_end:
            sig = d[p:p + 2]
            ln = d[p + 2]
            if ln == 0 or p + ln > rec_end:
                break
            if sig == b"TF":
                ver, tflags = d[p + 3], d[p + 4]
                stamp_len = 17 if (tflags & 0x01) else 7
                q = p + 5
                # stamp order per RRIP TF: mtime, atime, ctime, attr,
                # backup, expiration, effective (present bits 1..7)
                for bit in range(1, 8):
                    if tflags & (1 << bit):
                        if bit == 3 or bit == 2:  # ctime / atime -> epoch
                            d[q:q + stamp_len] = \
                                EPOCH7 if stamp_len == 7 else \
                                bytes([0x46, 0x01, 0x01, 0, 0, 0, 0, 0, 0, 0,
                                       0, 0, 0, 0, 0, 0, 0])
                            stats[0] += 1
                        q += stamp_len
            p += ln
        if flags & 0x02:                 # directory: recurse
            sub_lba = u32le(d, off + 2)
            sub_size = u32le(d, off + 10)
            walk_extent(d, sub_lba, sub_size, seen, stats)
        off = rec_end


def main():
    path = sys.argv[1]
    d = bytearray(open(path, "rb").read())
    if d[0x8001:0x8006] != b"CD001":
        sys.exit("iso_zero_ctime: not an ISO9660 image")
    root_lba = u32le(d, 0x8000 + 156 + 2)
    root_size = u32le(d, 0x8000 + 156 + 10)
    stats = [0]
    walk_extent(d, root_lba, root_size, set(), stats)
    open(path, "wb").write(bytes(d))
    print(f"iso_zero_ctime: {stats[0]} TF ctime stamp(s) zeroed "
          f"(root extent lba={root_lba} size={root_size})")


if __name__ == "__main__":
    main()
