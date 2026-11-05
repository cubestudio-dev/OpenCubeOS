/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP06g3-ocse_enc_fix
 * File: ocire/ocire_cn.c
 * Purpose: the OCIRE Chinese glyph lookup (task 4.8).
 *
 * The bitmap data lives in ocire_cn_font_data.c - GENERATED at build
 * time by ocire.mk from ocse/chinese/chinese_font_data.c (the WP06f
 * regenerated GNU Unifont rasterization; see that file's header and
 * tools/gen_chinese_font.py).  Layout (identical to OCSE's):
 *   chinese_font_glyphs[6763][32]        hanzi, zones 16..87
 *   chinese_font_symbols[682][32]        symbols, zones 1..9
 *   chinese_font_zone_index[6768]        (zone-16)*94+(wei-1) -> idx
 *   chinese_font_symbol_index[846]       (zone-1)*94+(wei-1) -> idx
 *   chinese_font_uto_gb[7445][2]         sorted {unicode, idx};
 *                                       idx bit15 = symbol table
 *
 * The lookup is the flat binary search over the sorted pair table
 * (the same discipline as both kernels' chinese_font.c), plus the
 * WP06g3 em-dash fold: U+2014 (what real-world text carries) maps
 * onto the table's U+2015 (where the Python gb2312 codec put the
 * 0xA1AA em-dash slot). */
#include "ocire_cn.h"
#include "ocire_cn_font_data.h"

const u8* ocire_cn_bitmap(u32 cp) {
    if (cp < 0x80) return 0;               /* ASCII: caller's own font */
    /* WP06g3-4.8 em-dash fold (see chinese_font.c in both kernels) */
    if (cp == 0x2014) cp = 0x2015;
    u32 lo = 0, hi = CHINESE_FONT_UTO_COUNT;
    while (lo < hi) {
        u32 mid = (lo + hi) / 2;
        u16 key = (u16)chinese_font_uto_gb[2 * mid];
        if (key == cp) {
            u16 idx = (u16)chinese_font_uto_gb[2 * mid + 1];
            if (idx & 0x8000) {
                u32 i = (u32)(idx & 0x7FFF);
                if (i >= CHINESE_FONT_SYMBOL_COUNT) return 0;
                return chinese_font_symbols[i];
            }
            if (idx >= CHINESE_FONT_HANZI_COUNT) return 0;
            return chinese_font_glyphs[idx];
        }
        if (key < cp) lo = mid + 1; else hi = mid;
    }
    return 0;
}
