#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - tests/wp06g1_font_audit.py (3.0.0-WP06g1-encoding_fix)
# Host-side font-library completeness audit.  Unpacks BOTH consumers'
# data and verifies, against Python's own gb2312 codec:
#   1. the kernel gzip container (ocos/services/font/src/svc_font_chinese_data.c)
#   2. the OCSE raw tables       (ocse/chinese/chinese_font_data.c)
# Checks per consumer:
#   A. counts: 6763 hanzi + 682 symbols + 7445 uto pairs
#   B. zone/wei -> idx -> unicode == Python gb2312 decode, ALL slots
#      (the WP06g1 bit15 fix: 2366 hanzi live at >= U+8000)
#   C. uto_gb: every pair's unicode agrees with B, idx agrees, sorted
#   D. every glyph bitmap non-empty
# Exit code 0 only when every check passes on BOTH files.
import gzip
import re
import struct
import sys

ROOT = "/home/z/oc-os"
HANZI, SYM, UTO = 6763, 682, 7445
ZI, SI = 6768, 846
fails = 0


def check(cond, label):
    global fails
    print(("  PASS " if cond else "  FAIL ") + label)
    if not cond:
        fails += 1


def parse_u8(text, name, count):
    m = re.search(r"const u8\s+%s\s*(?:\[[^\]]*\])+\s*=\s*\{" % re.escape(name), text)
    end = text.index("};", m.end())
    vals = re.findall(r"0x([0-9A-Fa-f]{2})(?![0-9A-Fa-f])", text[m.end():end])
    assert len(vals) == count, (name, len(vals), count)
    return bytes(int(v, 16) for v in vals)


def parse_u16(text, name, count):
    m = re.search(r"const u16\s+%s\s*(?:\[[^\]]*\])+\s*=\s*\{" % re.escape(name), text)
    end = text.index("};", m.end())
    vals = re.findall(r"0x([0-9A-Fa-f]{1,4})(?![0-9A-Fa-f])", text[m.end():end])
    assert len(vals) == count, (name, len(vals), count)
    return [int(v, 16) for v in vals]


def audit_kernel():
    print("[kernel gz container]")
    src = open(f"{ROOT}/ocos/services/font/src/svc_font_chinese_data.c").read()
    m = re.search(r"const u8\s+chinese_font_gz_blob\s*(?:\[[^\]]*\])+\s*=\s*\{", src)
    end = src.index("};", m.end())
    vals = re.findall(r"0x([0-9A-Fa-f]{2})(?![0-9A-Fa-f])", src[m.end():end])
    gz = bytes(int(v, 16) for v in vals)
    raw = gzip.decompress(gz)
    check(len(raw) == 298138, "raw length 298138")
    return audit_tables(
        glyphs=raw[0:HANZI * 32],
        symbols=raw[HANZI * 32:HANZI * 32 + SYM * 32],
        zone_index=struct.unpack_from("<%dH" % ZI, raw, HANZI * 32 + SYM * 32),
        sym_index=struct.unpack_from("<%dH" % SI, raw, HANZI * 32 + SYM * 32 + ZI * 2),
        uto=struct.unpack_from("<%dH" % (2 * UTO), raw, HANZI * 32 + SYM * 32 + ZI * 2 + SI * 2),
        idx2u=struct.unpack_from("<%dH" % UTO, raw, HANZI * 32 + SYM * 32 + ZI * 2 + SI * 2 + 2 * UTO * 2))


def audit_ocse():
    print("[OCSE raw tables]")
    src = open(f"{ROOT}/ocse/chinese/chinese_font_data.c").read()
    glyphs = b"".join(bytes(r) for r in
                      [parse_row(l) for l in
                       re.search(r"const u8\s+chinese_font_glyphs\s*(?:\[[^\]]*\])+\s*=\s*\{(.*?)\};",
                                 src, re.S).group(1).strip().splitlines()])
    symbols = b"".join(bytes(r) for r in
                       [parse_row(l) for l in
                        re.search(r"const u8\s+chinese_font_symbols\s*(?:\[[^\]]*\])+\s*=\s*\{(.*?)\};",
                                  src, re.S).group(1).strip().splitlines()])
    check(len(glyphs) == HANZI * 32, "hanzi table %d B" % (HANZI * 32))
    check(len(symbols) == SYM * 32, "symbol table %d B" % (SYM * 32))
    return audit_tables(
        glyphs=glyphs, symbols=symbols,
        zone_index=parse_u16(src, "chinese_font_zone_index", ZI),
        sym_index=parse_u16(src, "chinese_font_symbol_index", SI),
        uto=parse_u16(src, "chinese_font_uto_gb", 2 * UTO),
        idx2u=parse_u16(src, "chinese_font_idx_to_unicode", UTO))


def parse_row(line):
    return [int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{2})(?![0-9A-Fa-f])", line)]


def audit_tables(glyphs, symbols, zone_index, sym_index, uto, idx2u):
    # ---- build the Python reference mapping -------------------------
    ref_hanzi = []      # (zone, wei, cp) in compact order
    for zone in range(16, 88):
        for wei in range(1, 95):
            try:
                ch = bytes([zone + 0xA0, wei + 0xA0]).decode("gb2312")
            except UnicodeDecodeError:
                continue
            ref_hanzi.append((zone, wei, ord(ch)))
    ref_sym = []
    for zone in range(1, 10):
        for wei in range(1, 95):
            try:
                ch = bytes([zone + 0xA0, wei + 0xA0]).decode("gb2312")
            except UnicodeDecodeError:
                continue
            ref_sym.append((zone, wei, ord(ch)))
    check(len(ref_hanzi) == HANZI, "GB2312 reference: %d hanzi" % HANZI)
    check(len(ref_sym) == SYM, "GB2312 reference: %d symbols" % SYM)

    # ---- B: zone/wei -> idx -> unicode against the reference --------
    bad = hi = 0
    for i, (zone, wei, cp) in enumerate(ref_hanzi):
        idx = zone_index[(zone - 16) * 94 + (wei - 1)]
        if idx != i or idx2u[idx] != cp:
            bad += 1
        if cp >= 0x8000:
            hi += 1
    check(bad == 0, "all %d hanzi: zone/wei -> idx -> cp exact" % HANZI)
    check(hi == 2366, "%d hanzi at >= U+8000 all stored raw" % hi)
    bad = 0
    for i, (zone, wei, cp) in enumerate(ref_sym):
        idx = sym_index[(zone - 1) * 94 + (wei - 1)]
        if idx != i or idx2u[HANZI + idx] != cp:
            bad += 1
    check(bad == 0, "all %d symbols: zone/wei -> idx -> cp exact" % SYM)

    # ---- C: uto_gb pairs --------------------------------------------
    pairs = [(uto[2 * i], uto[2 * i + 1]) for i in range(UTO)]
    check(all(pairs[i][0] < pairs[i + 1][0] for i in range(UTO - 1)),
          "uto_gb sorted by unicode")
    exp = {cp: (i if i < HANZI else (i - HANZI) | 0x8000)
           for i, (_z, _w, cp) in enumerate(ref_hanzi)}
    for i, (_z, _w, cp) in enumerate(ref_sym):
        exp[cp] = i | 0x8000
    bad = sum(1 for cp, idx in pairs if exp.get(cp) != idx)
    check(bad == 0 and len(pairs) == UTO,
          "uto_gb: all %d pairs agree with reference mapping" % UTO)

    # ---- D: glyph ink ------------------------------------------------
    # U+3000 (ideographic space, zone 1 wei 1) is SUPPOSED to be blank;
    # every other glyph must carry ink.
    blank_sym_cps = []
    ref_sym_cps = [cp for (_z, _w, cp) in ref_sym]
    for i in range(SYM):
        if not any(symbols[i * 32:i * 32 + 32]):
            blank_sym_cps.append(ref_sym_cps[i])
    empty_h = sum(1 for i in range(HANZI) if not any(glyphs[i * 32:i * 32 + 32]))
    check(empty_h == 0, "no blank hanzi bitmaps")
    check(blank_sym_cps == [0x3000],
          "only U+3000 (ideographic space) is a blank symbol bitmap")
    # spot glyphs: 'zhong' row 4 carries the full horizontal bar
    # (rows: 0100 0100 0100 0100 3FF8 2108 ... verified against the
    # Unifont rasterizer output)
    zhi = ref_hanzi.index((54, 48, 0x4E2D))
    row4 = struct.unpack_from(">H", glyphs, zhi * 32 + 4 * 2)[0]
    row0 = struct.unpack_from(">H", glyphs, zhi * 32 + 0)[0]
    check(row0 == 0x0100 and row4 == 0x3FF8,
          "zhong U+4E2D rows 0/4 == 0x0100/0x3FF8 (got 0x%04X/0x%04X)"
          % (row0, row4))


audit_kernel()
audit_ocse()
print("font audit: %s (%d failures)" % ("PASS" if fails == 0 else "FAIL", fails))
sys.exit(1 if fails else 0)
