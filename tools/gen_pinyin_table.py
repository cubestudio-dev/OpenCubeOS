#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 cubestudio-dev <cubestudio@qq.com>
# Open Cube OS - WP-10-chinese_support pinyin table generator.
#
# Builds the pinyin IME data for GB2312 level-1 hanzi (3755 chars,
# pinyin-ordered = frequency-ordered) from the pypinyin package:
#   - pinyin_syllable_pool[]   deduped tone-free syllable string pool
#   - pinyin_syllable_off[]    u16 offset of each syllable into the pool
#   - pinyin_syllable_count
#   - pinyin_char_map[3755]    {u16 unicode, u16 syllable_idx}, sorted by
#                              unicode for binary search; multi-reading
#                              chars take pypinyin's default reading
#
# Output: chinese/pinyin_data.c (committed, AUTO-GENERATED).
# Regenerate: python3 tools/gen_pinyin_table.py

import os
import sys

try:
    import pypinyin
except ImportError:
    sys.path.insert(0, "/home/z/.local/lib/python3.13/site-packages")
    import pypinyin

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                   "chinese", "pinyin_data.c")


def gb2312_level1():
    """GB2312 zones 16..55 = 3755 level-1 hanzi (pinyin/freq order)."""
    out = []
    for zone in range(16, 56):
        for wei in range(1, 95):
            try:
                ch = bytes([zone + 0xA0, wei + 0xA0]).decode("gb2312")
            except UnicodeDecodeError:
                continue
            out.append(ord(ch))
    return out


def main():
    chars = gb2312_level1()
    if len(chars) != 3755:
        sys.exit("FATAL: level-1 count %d != 3755" % len(chars))

    syllables = []
    syll_idx = {}
    entries = []          # (unicode, syllable_idx) in level-1 order
    for cp in chars:
        ch = chr(cp)
        py = pypinyin.pinyin(ch, style=pypinyin.Style.NORMAL,
                             errors="ignore")
        syl = py[0][0] if py and py[0] else ""
        if not syl:
            sys.exit("FATAL: no pinyin for U+%04X %s" % (cp, ch))
        if syl not in syll_idx:
            syll_idx[syl] = len(syllables)
            syllables.append(syl)
        entries.append((cp, syll_idx[syl]))

    entries.sort()   # by unicode for the binary-search map
    print("[gen-pinyin] level-1 chars: %d" % len(entries))
    print("[gen-pinyin] distinct syllables: %d" % len(syllables))

    pool = "".join(syllables).encode("ascii")
    offs = []
    acc = 0
    for s in syllables:
        offs.append(acc)
        acc += len(s)

    def c_u8_array(name, data, per=20):
        lines = ["const u8 %s[%d] = {" % (name, len(data))]
        for i in range(0, len(data), per):
            row = data[i:i + per]
            lines.append("    " + ",".join("0x%02X" % b for b in row) + ",")
        lines.append("};")
        return "\n".join(lines)

    def c_u16_array(name, values, per=16):
        lines = ["const u16 %s[%d] = {" % (name, len(values))]
        for i in range(0, len(values), per):
            chunk = values[i:i + per]
            lines.append("    " + ",".join("0x%04X" % v for v in chunk) + ",")
        lines.append("};")
        return "\n".join(lines)

    hdr = """/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10-chinese_support
 * File: chinese/pinyin_data.c
 * Purpose: pinyin IME tables (AUTO-GENERATED - DO NOT EDIT).
 *
 * Regenerate with: python3 tools/gen_pinyin_table.py
 * Source: pypinyin, tone-free readings of GB2312 level-1 hanzi
 * (zones 16..55, 3755 chars, pinyin order = frequency order).
 *
 * Layout:
 *   pinyin_syllable_pool[]  tone-free syllable strings, NUL-free pool
 *   pinyin_syllable_off[]   u16 start offset of syllable i in the pool;
 *                           syllable i ends at off[i+1] (pool end for last)
 *   pinyin_char_map[]       {u16 unicode, u16 syllable_idx} pairs sorted
 *                           by unicode (binary-searchable), 3755 pairs
 */
#include "../kernel/types.h"
"""
    parts = [hdr]
    parts.append("\n#define PINYIN_SYLLABLE_COUNT %d\n" % len(syllables))
    parts.append(c_u8_array("pinyin_syllable_pool", pool))
    parts.append("\n\n")
    parts.append(c_u16_array("pinyin_syllable_off", offs + [len(pool)]))
    parts.append("\n\n")
    flat = []
    for cp, idx in entries:
        flat += [cp, idx]
    parts.append(c_u16_array("pinyin_char_map", flat, 8))
    parts.append("\n")

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as f:
        f.write("\n".join(parts))
    print("[gen-pinyin] wrote %s (%d bytes)" % (OUT, os.path.getsize(OUT)))

if __name__ == "__main__":
    main()
