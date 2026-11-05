/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP06g3-ocse_enc_fix
 * File: ocire/ocire_cn_font_data.h
 * Purpose: declarations for the GENERATED ocire_cn_font_data.c
 * (ocire.mk copies ocse/chinese/chinese_font_data.c and rewrites its
 * includes; the data layout is documented there). */
#ifndef OCIRE_CN_FONT_DATA_H
#define OCIRE_CN_FONT_DATA_H

#include "ocire_types.h"

#define CHINESE_FONT_HANZI_COUNT  6763
#define CHINESE_FONT_SYMBOL_COUNT 682
#define CHINESE_FONT_UTO_COUNT    7445

extern const u8 chinese_font_glyphs[CHINESE_FONT_HANZI_COUNT][32];
extern const u8 chinese_font_symbols[CHINESE_FONT_SYMBOL_COUNT][32];
extern const u16 chinese_font_zone_index[6768];
extern const u16 chinese_font_symbol_index[846];
extern const u16 chinese_font_uto_gb[2 * CHINESE_FONT_UTO_COUNT]; /* flat {u,i} pairs */

#endif /* OCIRE_CN_FONT_DATA_H */
