/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10-chinese_support
 * File: chinese/console_cn.h
 * Purpose: Console encoding state + wide-character write path.
 *
 * The stock console (screen_console.c) is byte oriented and is NOT
 * modified. This layer adds:
 *   - a console encoding state (ASCII / GB2312 / UTF-8)
 *   - screen-console-level wide writes: putcw(cp), puts_utf8(s),
 *     puts_gb2312(s, len)
 *   - character-unit cursor math (cols per codepoint)
 *
 * Wide writes mirror the stock putc behavior for control chars, run
 * the same L1 console hook (UTF-8 encoded, one byte per call, so a
 * serial tee receives the full stream), and scroll identically.
 *
 * Driver-API style extension (the numbered L1 set stays closed at
 * 1-138).
 */
#ifndef OC_CONSOLE_CN_H
#define OC_CONSOLE_CN_H

#include "../core/types.h"
#include "encoding.h"

#define CONSOLE_CN_ENC_MAX 16

/* Init the encoding state (default UTF-8) and make the cn renderer
 * active. Idempotent. */
int console_cn_init(void);

/* Set the console encoding: one of encoding_kind_t (ENC_MIXED is
 * rejected). Returns 0 or -2. */
int console_cn_set_encoding(encoding_kind_t enc);

/* Current console encoding. */
encoding_kind_t console_cn_get_encoding(void);

/* Name of an encoding kind ("ascii" / "gb2312" / "utf-8" / "mixed"). */
const char* console_cn_encoding_name(encoding_kind_t enc);

/* Write ONE codepoint to the console: ASCII goes through the stock
 * putc path; CJK is drawn 16x16 across two cells and advances the
 * cursor by two columns. Control chars behave like putc. */
void console_cn_putcw(u32 cp);

/* Write a string honoring the console encoding: UTF-8 mode decodes
 * UTF-8, GB2312 mode decodes GB2312 (ASCII subset included), ASCII
 * mode writes raw bytes. */
void console_cn_puts(const char* s);

/* Write an explicit-encoding byte string. */
void console_cn_puts_enc(const u8* s, u32 len, encoding_kind_t enc);

/* Move the console cursor by ONE CHARACTER left/right (CJK counts as
 * one). dir<0 left, >0 right. Returns the new column. */
int console_cn_cursor_move(int dir);

/* Erase the last written character (CJK-aware via the cn write-path
 * width record). Returns the new column. */
int console_cn_backspace(void);

/* Column count a string will occupy when written with the current
 * encoding (control chars count 0). Used by lineedit alignment. */
u32 console_cn_display_cols(const char* s, u32 bytes);

/* Column count of ONE codepoint (1 or 2). */
u32 console_cn_cp_cols(u32 cp);

#endif /* OC_CONSOLE_CN_H */
