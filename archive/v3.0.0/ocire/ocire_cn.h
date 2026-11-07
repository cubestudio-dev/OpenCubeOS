/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP06g3-ocse_enc_fix
 * File: ocire/ocire_cn.h
 * Purpose: the OCIRE Chinese support (task 4.8).
 *
 * WP06g3: the recovery environment had ZERO Chinese capability (an
 * 8x16 ASCII font, no UTF-8 decode, an English-only shell) - the
 * "OCIRE mojibake" was missing capability, not broken bytes.  This
 * module brings the GB2312 glyph tables (the same generated data the
 * OCSE environment ships) behind a Unicode-codepoint lookup, and the
 * console renders wide glyphs through it. */
#ifndef OCIRE_CN_H
#define OCIRE_CN_H

#include "ocire_types.h"

/* Glyph lookup by Unicode codepoint.  Returns the 32-byte 16x16
 * bitmap (16 rows x 2 bytes, bit7 = leftmost), or NULL when the
 * codepoint has no glyph (the caller degrades to '?'). */
const u8* ocire_cn_bitmap(u32 cp);

#endif /* OCIRE_CN_H */
