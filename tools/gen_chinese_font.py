#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - WP-10-chinese_support font generator.
#
# Generates the embedded 16x16 Chinese bitmap font from GNU Unifont
# (unifont.otf).  Every Unifont CJK glyph is a union of axis-aligned
# rectangles on a 16x16 pixel grid (upem=64, so one font unit == 1/4
# pixel); we extract the outline rectangles and rasterize them exactly,
# giving a clean binary bitmap with no anti-aliasing.
#
# Outputs drivers output file: chinese/chinese_font_data.c with:
#   - chinese_font_glyphs[6763][32]      GB2312 hanzi (zones 16..87), row-major,
#                                        16 rows x 2 bytes, bit7 = leftmost pixel
#   - chinese_font_symbols[682][32]      GB2312 symbols (zones 1..9), same layout
#   - chinese_font_zone_index[6768]      (zone-16)*94+(wei-1) -> hanzi index, 0xFFFF = gap
#   - chinese_font_symbol_index[846]     (zone-1)*94+(wei-1) -> symbol index, 0xFFFF = gap
#   - chinese_font_uto_gb[7445]          {u16 unicode, u16 idx} sorted by unicode;
#                                        bit15 of idx set => symbol table, else hanzi
#                                        (idx is into the compact arrays)
#
# The output is committed to the repo (same precedent as
# drivers/display/screen_font_data.c); regenerate with:
#   python3 tools/gen_chinese_font.py
#
# Requirements: fontTools (pip).  Unifont path default
# /usr/share/fonts/opentype/unifont/unifont.otf, override with UNIFONT_OTF env.

import os
import sys

from fontTools.ttLib import TTFont
from fontTools.pens.recordingPen import RecordingPen

UNIFONT = os.environ.get("UNIFONT_OTF",
                         "/usr/share/fonts/opentype/unifont/unifont.otf")
OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                   "chinese", "chinese_font_data.c")

# ---------------------------------------------------------------- GB2312 set

def gb2312_enumerate(zones):
    """Enumerate GB2312 chars in the given zone range.

    Returns list of (zone, wei, unicode_cp) skipping undefined slots.
    Python's 'gb2312' codec is a GBK alias but inside zones 16..87 with
    trail bytes 0xA1..0xFE the GBK mapping is identical to GB2312 for
    hanzi, and zones 1..9 likewise for the symbol set, so decoding the
    two lead/trail bytes is the standard way to derive the mapping.
    """
    out = []
    for zone in zones:
        for wei in range(1, 95):
            try:
                ch = bytes([zone + 0xA0, wei + 0xA0]).decode("gb2312")
            except UnicodeDecodeError:
                continue
            out.append((zone, wei, ord(ch)))
    return out

# --------------------------------------------------------------- rasterizer

def load_contours(font, cp):
    """Extract closed outlines of a codepoint in font units (upem=64)."""
    cmap = font.getBestCmap()
    if cp not in cmap:
        return None
    pen = RecordingPen()
    font.getGlyphSet()[cmap[cp]].draw(pen)
    contours, cur = [], None
    for op, args in pen.value:
        if op == "moveTo":
            cur = [args[0]]
        elif op == "lineTo":
            cur.append(args[0])
        elif op in ("curveTo", "qCurveTo"):
            # Unifont glyphs are pure rectangles; a curve here would mean
            # the source font changed - fail loudly instead of mis-render.
            raise SystemExit("unexpected curve op %s at U+%04X" % (op, cp))
        elif op == "closePath":
            if cur:
                contours.append(cur)
            cur = None
    return contours

def rasterize(contours):
    """Even-odd rasterization onto a 16x16 grid.

    Font units: upem=64 == 16 px, so pixel (col,row) center maps to
    (col*4+2, (15-row)*4+2) in font units (y flipped: TTF y-up).
    Returns 16 rows of u16 (bit15 = leftmost pixel), MSB-first like the
    existing screen_font8x16 (bit7 = leftmost of an 8px row).
    """
    rows = []
    for row in range(16):
        cy = (15 - row) * 4 + 2
        line = 0
        for col in range(16):
            cx = col * 4 + 2
            crossings = 0
            for pts in contours:
                n = len(pts)
                for i in range(n):
                    x1, y1 = pts[i]
                    x2, y2 = pts[(i + 1) % n]
                    if (y1 > cy) != (y2 > cy):
                        t = (cy - y1) / (y2 - y1)
                        xi = x1 + t * (x2 - x1)
                        if xi > cx:
                            crossings += 1
            if crossings & 1:
                line |= 0x8000 >> col
        rows.append(line)
    return rows

def glyph_bytes(rows):
    """16x16 u16 rows -> 32 bytes (16 rows x 2, MSB first)."""
    out = bytearray()
    for r in rows:
        out.append((r >> 8) & 0xFF)
        out.append(r & 0xFF)
    return bytes(out)

# --------------------------------------------------------------------- main

def main():
    font = TTFont(UNIFONT)
    print("[gen-font] unifont: %s (upem=%d)"
          % (UNIFONT, font["head"].unitsPerEm))

    hanzi = gb2312_enumerate(range(16, 88))
    symbols = gb2312_enumerate(range(1, 10))
    print("[gen-font] GB2312 hanzi entries: %d (expect 6763)" % len(hanzi))
    print("[gen-font] GB2312 symbol entries: %d (expect 682)" % len(symbols))
    if len(hanzi) != 6763:
        sys.exit("FATAL: hanzi count %d != 6763" % len(hanzi))
    if len(symbols) != 682:
        sys.exit("FATAL: symbol count %d != 682" % len(symbols))

    missing = []
    glyphs = []          # hanzi: compact, in zone/wei order
    for zone, wei, cp in hanzi:
        contours = load_contours(font, cp)
        if contours is None:
            missing.append((zone, wei, cp))
            continue
        glyphs.append(glyph_bytes(rasterize(contours)))
    sym_glyphs = []
    for zone, wei, cp in symbols:
        contours = load_contours(font, cp)
        if contours is None:
            missing.append((zone, wei, cp))
            continue
        sym_glyphs.append(glyph_bytes(rasterize(contours)))
    if missing:
        sys.exit("FATAL: %d glyphs missing from unifont, e.g. %r"
                 % (len(missing), missing[:8]))
    print("[gen-font] rendered %d hanzi + %d symbols, all non-missing"
          % (len(glyphs), len(sym_glyphs)))

    empty = [i for i, g in enumerate(glyphs) if not any(g)]
    print("[gen-font] blank hanzi bitmaps: %d %s"
          % (len(empty), ["%d-%d" % (hanzi[i][0], hanzi[i][1]) for i in empty]))

    # zone slot -> compact index
    hz_index = [0xFFFF] * (72 * 94)
    for i, (zone, wei, _cp) in enumerate(hanzi):
        hz_index[(zone - 16) * 94 + (wei - 1)] = i
    sy_index = [0xFFFF] * (9 * 94)
    for i, (zone, wei, _cp) in enumerate(symbols):
        sy_index[(zone - 1) * 94 + (wei - 1)] = i

    # unicode -> compact index (bit15 = symbol table)
    u2g = sorted([(cp, i) for i, (_z, _w, cp) in enumerate(hanzi)]
                 + [(cp, i | 0x8000) for i, (_z, _w, cp) in enumerate(symbols)])
    if len(u2g) != 7445 or len(set(cp for cp, _ in u2g)) != 7445:
        sys.exit("FATAL: unicode table has %d entries (expect 7445 unique)"
                 % len(u2g))

    # ------------------------------------------------------------- emit C
    def c_array_u8(name, data, per):
        lines = ["const u8 %s[%d][%d] = {" % (name, len(data) // per, per)]
        for i in range(0, len(data), per):
            row = data[i:i + per]
            lines.append("    {" + ",".join("0x%02X" % b for b in row) + "},")
        lines.append("};")
        return "\n".join(lines)

    def c_array_u16(name, values, per=16):
        lines = ["const u16 %s[%d] = {" % (name, len(values))]
        for i in range(0, len(values), per):
            chunk = values[i:i + per]
            lines.append("    " + ",".join("0x%04X" % v for v in chunk) + ",")
        lines.append("};")
        return "\n".join(lines)

    hdr = """/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10-chinese_support
 * File: chinese/chinese_font_data.c
 * Purpose: embedded 16x16 GB2312 bitmap font (AUTO-GENERATED - DO NOT EDIT).
 *
 * Regenerate with: python3 tools/gen_chinese_font.py
 * Source: GNU Unifont (unifont.otf), rasterized exactly from its
 * rectangle outlines (no anti-aliasing, bit7 = leftmost pixel, 16 rows
 * x 2 bytes per glyph, 32 bytes per glyph).
 *
 * Layout:
 *   chinese_font_glyphs[6763][32]   hanzi, GB2312 zones 16..87 compact order
 *   chinese_font_symbols[682][32]   symbols, GB2312 zones 1..9 compact order
 *   chinese_font_zone_index[6768]   (zone-16)*94+(wei-1) -> hanzi idx, 0xFFFF=gap
 *   chinese_font_symbol_index[846]  (zone-1)*94+(wei-1) -> symbol idx, 0xFFFF=gap
 *   chinese_font_uto_gb[7445]       sorted {unicode, idx}; idx bit15 = symbol
 */
#include "../kernel/types.h"
"""

    parts = [hdr]
    parts.append("\n#define CHINESE_FONT_HANZI_COUNT 6763\n")
    parts.append("#define CHINESE_FONT_SYMBOL_COUNT 682\n\n")
    parts.append(c_array_u8("chinese_font_glyphs", b"".join(glyphs), 32))
    parts.append("\n\n")
    parts.append(c_array_u8("chinese_font_symbols", b"".join(sym_glyphs), 32))
    parts.append("\n\n")
    parts.append(c_array_u16("chinese_font_zone_index", hz_index, 16))
    parts.append("\n\n")
    parts.append(c_array_u16("chinese_font_symbol_index", sy_index, 16))
    parts.append("\n\n")
    flat = []
    for cp, idx in u2g:
        flat += [cp, idx]
    parts.append(c_array_u16("chinese_font_uto_gb", flat, 8))
    parts.append("\n\n")
    # compact index -> unicode (bit15 of value set = symbol table entry)
    idx2u = []
    for _z, _w, cp in hanzi:
        idx2u.append(cp)
    for _z, _w, cp in symbols:
        idx2u.append(cp | 0x8000)
    parts.append(c_array_u16("chinese_font_idx_to_unicode", idx2u, 16))
    parts.append("\n")

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as f:
        f.write("\n".join(parts))
    print("[gen-font] wrote %s (%d bytes)"
          % (OUT, os.path.getsize(OUT)))

    # spot check: print a few known glyphs as ASCII art for eyeballing
    def show(cp, label):
        contours = load_contours(font, cp)
        rows = rasterize(contours)
        print("[gen-font] %s U+%04X" % (label, cp))
        for r in rows:
            print("  " + "".join("#" if r & (0x8000 >> c) else "."
                                 for c in range(16)))
    for cp, label in ((0x4E2D, "zhong"), (0x56FD, "guo"), (0x6C49, "han"),
                      (0x5B57, "zi")):
        show(cp, label)

if __name__ == "__main__":
    main()
