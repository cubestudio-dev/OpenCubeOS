#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 */
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - 3.0.0-WP-runtime-hardening-and-experience-rebase
# File: ocrmecl/bios/build_ocr_image.py
# Purpose: assemble the OCRMECL BIOS-track El Torito image:
#
#            [ ocr stage1 (512 B, patch block filled) ]
#            [ ocr core   (padded to a 512 multiple)  ]
#            [ OCBL.BIN   (the untouched OCBL image)   ]
#
#          SeaBIOS preloads the whole composite at linear 0x7C00; the
#          stage1 patch block (offset 0x1A0, contract in
#          interface/boot_handoff.h) tells the core where the OCBL
#          payload starts and how big it is, so the core can move it
#          down over 0x7C00 and hand off with the plain El Torito
#          register contract.
#
# Budget gates (the low-memory map is the law):
#   - ocr stage1+core stay small (<= 0x8000): the core only flattens
#     state; every big consumer stays in OCBL where it always was.
#   - the whole composite must end below 0x9F800 - below the lowest
#     EBDA any observed firmware uses (the WP-tui VMware finding).
#
# Usage: build_ocr_image.py <out> <stage1.bin> <core.bin> <ocbl_bios.bin>
import sys

def main():
    out, s1, core, ocbl_path = sys.argv[1:5]
    stage1 = bytearray(open(s1, 'rb').read())
    coreb = open(core, 'rb').read()
    ocbl = open(ocbl_path, 'rb').read()
    assert len(stage1) == 512, f"stage1 is {len(stage1)} bytes, must be 512"

    pad = (-len(coreb)) % 512
    coreb += b'\x00' * pad
    ocr_total = 512 + len(coreb)          # OCBL.BIN offset in the image

    if ocr_total > 0x8000:
        print(f"ocrmecl: FATAL ocr stage1+core {ocr_total}B exceeds the "
              f"0x8000 budget")
        sys.exit(1)
    total_end = 0x7C00 + ocr_total + len(ocbl)
    if total_end > 0x9F800:
        print(f"ocrmecl: FATAL composite ends at {total_end:#x} - above "
              f"the 0x9F800 low-EBDA line")
        sys.exit(1)

    # patch block (offset 0x1A0 in the stage1 sector)
    magic = 0x4D52434F                     # 'OCRM' little-endian
    stage1[0x1A0:0x1B0] = (magic.to_bytes(4, 'little') +
                           len(ocbl).to_bytes(4, 'little') +
                           ocr_total.to_bytes(4, 'little') +
                           (0).to_bytes(4, 'little'))

    image = bytes(stage1) + coreb + ocbl
    open(out, 'wb').write(image)
    print(f"ocrmecl: image {len(image)} B "
          f"(stage1 512 + core {len(coreb)}B + OCBL {len(ocbl)}B "
          f"= {(len(image)+511)//512} sectors, OCBL at +{ocr_total}) "
          f"-> {out}")

if __name__ == '__main__':
    main()
