#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
"""
gen_font_gz.py: wrap the six chinese_font_data.c tables into one gzip
container (3.0.0-WP04 G11b identity-window relief).

The kernel image must end below 0x400000 (P3-1 security invariant: user
address spaces leave the 0x400000-0x600000 window empty, so kernel data
above 0x400000 is unreachable from syscall/IRQ context on a user CR3).
The embedded GB2312 font tables alone were ~298 KiB of .rodata, and the
WP-04 L1 migration (user program blobs + IME revival) ran the link into
that guard rail.  This tool replaces the six raw arrays in
src/L0/services/font/chinese_font_data.c with ONE gzip stream
(chinese_font_gz_blob[]); chinese_font.c inflates it lazily into a
kmalloc'd buffer on first glyph lookup (boot step 3 renders ASCII only,
so the heap is always up by the time CJK data is first touched).

Regeneration chain: tools/gen_chinese_font.py (Unifont source, emits the
six raw arrays) -> tools/gen_font_gz.py (this tool, wraps them).

Raw blob layout (fixed order, offsets derivable from chinese_font.h):
    [0]      chinese_font_glyphs          CHINESE_FONT_HANZI_COUNT * 32
    [1]      chinese_font_symbols         CHINESE_FONT_SYMBOL_COUNT * 32
    [2]      chinese_font_zone_index      6768 * 2
    [3]      chinese_font_symbol_index    846 * 2
    [4]      chinese_font_uto_gb          2 * CHINESE_FONT_UTO_COUNT * 2
    [5]      chinese_font_idx_to_unicode CHINESE_FONT_UTO_COUNT * 2

Usage: python3 tools/gen_font_gz.py   (run from repo root or tools/)
"""
import gzip
import hashlib
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(ROOT, "src", "L0", "services", "font", "chinese_font_data.c")

HANZI_COUNT = 6763
SYMBOL_COUNT = 682
UTO_COUNT = 7445
GLYPH_BYTES = 32
ZONE_INDEX_LEN = 6768
SYMBOL_INDEX_LEN = 846

SEC_GLYPHS = HANZI_COUNT * GLYPH_BYTES
SEC_SYMBOLS = SYMBOL_COUNT * GLYPH_BYTES
SEC_ZONE_INDEX = ZONE_INDEX_LEN * 2
SEC_SYMBOL_INDEX = SYMBOL_INDEX_LEN * 2
SEC_UTO_GB = 2 * UTO_COUNT * 2
SEC_IDX_TO_UNICODE = UTO_COUNT * 2
RAW_LEN = (SEC_GLYPHS + SEC_SYMBOLS + SEC_ZONE_INDEX +
           SEC_SYMBOL_INDEX + SEC_UTO_GB + SEC_IDX_TO_UNICODE)

OFF_SYMBOLS = SEC_GLYPHS
OFF_ZONE_INDEX = OFF_SYMBOLS + SEC_SYMBOLS
OFF_SYMBOL_INDEX = OFF_ZONE_INDEX + SEC_ZONE_INDEX
OFF_UTO_GB = OFF_SYMBOL_INDEX + SEC_SYMBOL_INDEX
OFF_IDX_TO_UNICODE = OFF_UTO_GB + SEC_UTO_GB
assert OFF_IDX_TO_UNICODE + SEC_IDX_TO_UNICODE == RAW_LEN


def parse_u8_array(text, name, count):
    m = re.search(r"const u8\s+%s\s*(?:\[[^\]]*\])+\s*=\s*\{" % re.escape(name), text)
    if not m:
        sys.exit(f"gen_font_gz: array {name} not found")
    end = text.index("};", m.end())
    span = text[m.end():end]
    vals = re.findall(r"0x([0-9A-Fa-f]{2})(?![0-9A-Fa-f])", span)
    if len(vals) != count:
        sys.exit(f"gen_font_gz: array {name}: parsed {len(vals)} != exactly {count} entries")
    return bytes(int(v, 16) for v in vals)


def parse_u16_array(text, name, count):
    m = re.search(r"const u16\s+%s\s*(?:\[[^\]]*\])+\s*=\s*\{" % re.escape(name), text)
    if not m:
        sys.exit(f"gen_font_gz: array {name} not found")
    end = text.index("};", m.end())
    span = text[m.end():end]
    vals = re.findall(r"0x([0-9A-Fa-f]{1,4})(?![0-9A-Fa-f])", span)
    if len(vals) != count:
        sys.exit(f"gen_font_gz: array {name}: parsed {len(vals)} != exactly {count} entries")
    out = bytearray()
    for v in vals:
        x = int(v, 16)
        out += bytes((x & 0xFF, (x >> 8) & 0xFF))
    return bytes(out)


def main():
    if not os.path.exists(DATA):
        sys.exit("gen_font_gz: missing chinese_font_data.c")
    src = open(DATA).read()
    if "chinese_font_gz_blob" in src:
        # Already wrapped.  Refuse to double-wrap; the raw arrays are
        # gone from this tree (recover via gen_chinese_font.py first).
        sys.exit("gen_font_gz: chinese_font_data.c is already the gzip "
                 "wrapper; regenerate the raw arrays with "
                 "tools/gen_chinese_font.py first")

    raw = (parse_u8_array(src, "chinese_font_glyphs", SEC_GLYPHS) +
           parse_u8_array(src, "chinese_font_symbols", SEC_SYMBOLS) +
           parse_u16_array(src, "chinese_font_zone_index", ZONE_INDEX_LEN) +
           parse_u16_array(src, "chinese_font_symbol_index", SYMBOL_INDEX_LEN) +
           parse_u16_array(src, "chinese_font_uto_gb", 2 * UTO_COUNT) +
           parse_u16_array(src, "chinese_font_idx_to_unicode", UTO_COUNT))
    if len(raw) != RAW_LEN:
        sys.exit(f"gen_font_gz: raw blob {len(raw)} != expected {RAW_LEN}")

    gz = gzip.compress(raw, 9, mtime=0)
    gz_sha = hashlib.sha256(gz).hexdigest()
    raw_sha = hashlib.sha256(raw).hexdigest()
    print(f"gen_font_gz: raw {len(raw)} B -> gzip {len(gz)} B "
          f"({100.0 * len(gz) / len(raw):.1f}%), saves {len(raw) - len(gz)} B")
    print(f"gen_font_gz: raw sha256   {raw_sha}")
    print(f"gen_font_gz: gzip sha256  {gz_sha}")

    lines = []
    w = lines.append
    w("/* SPDX-License-Identifier: Apache-2.0 */")
    w("/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */")
    w("/* Open Cube OS 3.0.0-WP04 - File: services/font/chinese_font_data.c")
    w(" * Purpose: embedded 16x16 GB2312 bitmap font, gzip container.")
    w(" *")
    w(" * AUTO-GENERATED - DO NOT EDIT.  Regeneration chain:")
    w(" *   1. tools/gen_chinese_font.py   (source: GNU Unifont, emits the")
    w(" *      six raw arrays into this file)")
    w(" *   2. tools/gen_font_gz.py        (wraps the six arrays into the")
    w(" *      chinese_font_gz_blob[] gzip stream and rewrites this file)")
    w(" *")
    w(" * 3.0.0-WP04 G11b: the kernel image must end below the 0x400000")
    w(" * identity-window boundary (P3-1 security invariant), and the raw")
    w(" * tables (~292 KiB .rodata) plus the WP-04 L1 app blobs ran the")
    w(" * link into that guard rail.  chinese_font.c now inflates this")
    w(" * stream lazily into a kmalloc'd buffer on first glyph lookup;")
    w(" * section offsets live in chinese_font.h (CHINESE_FONT_GZ_*).")
    w(" *")
    w(f" * raw={RAW_LEN} B sha256={raw_sha}")
    w(f" * gz={len(gz)} B sha256={gz_sha}")
    w(" */")
    w("#include \"chinese_font.h\"")
    w("")
    w("const u8 chinese_font_gz_blob[] = {")
    for i in range(0, len(gz), 20):
        chunk = gz[i:i + 20]
        w("    " + ",".join(f"0x{b:02X}" for b in chunk) + ",")
    w("};")
    w("")
    w(f"const u32 chinese_font_gz_len = {len(gz)};")
    w("")

    open(DATA, "w").write("\n".join(lines))
    print(f"gen_font_gz: wrote {DATA}")
    print("gen_font_gz: verify with the chinesetest / font-test / "
          "encoding-test commands after rebuild")


if __name__ == "__main__":
    main()
