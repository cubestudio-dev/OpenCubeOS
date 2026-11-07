#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - 3.0.0-WP05-ocbl
# File: ocbl/build_image.py
# Purpose: assemble the OCBL BIOS boot image:
#            [ stage1 (512 B) ][ core (padded to a 512 multiple) ]
#          The core must stay clear of the OCBL low-memory map
#          (page tables at 0x8F000), so its size is gated here.
# Usage: build_image.py <out> <stage1.bin> <core.bin>
import sys

def main():
    out, s1, core = sys.argv[1], sys.argv[2], sys.argv[3]
    stage1 = open(s1, 'rb').read()
    coreb = open(core, 'rb').read()
    assert len(stage1) == 512, f"stage1 is {len(stage1)} bytes, must be 512"
    pad = (-len(coreb)) % 512
    coreb += b'\x00' * pad
    total = stage1 + coreb
    limit = 0x40000 - 0x7E00          # trampoline scratch starts above
    if len(coreb) > limit:
        print(f"ocbl: FATAL core {len(coreb)}B exceeds the 0x8F000 map limit {limit}B")
        sys.exit(1)
    open(out, 'wb').write(total)
    print(f"ocbl: image {len(total)} B (stage1 512 + core {len(coreb)}B "
          f"= {(len(total)+511)//512} sectors) -> {out}")

if __name__ == '__main__':
    main()
