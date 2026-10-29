/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS 3.0.0-WP04
 * File: services/display/ime_engine.h
 * Purpose: the pinyin IME engine, revived in G11b verbatim from
 * v2.2.0 chinese/tui_chinese.c (IME section).
 *
 * WP-03 deleted this engine together with the whole input-method
 * user plane (and retired DISP ops 11/12); WP-04 G11b brings the
 * engine back so the `ime` / `ime-test` commands reach ring 3 again.
 * The public function names keep their v2.2.0 spelling
 * (tui_chinese_ime_*) - the G11c TUI revival links against them
 * unchanged.  ime_match()/ime_candidates() were file-static in
 * v2.2.0; they are exported here because both this engine and the
 * TUI compose pad (tui_chinese_input) consume them.
 *
 * Tables live next door in pinyin_data.c (generated).
 */
#ifndef OC_IME_ENGINE_H
#define OC_IME_ENGINE_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Syllable table sizes (generated with pinyin_data.c). */
#define PINYIN_SYLLABLE_COUNT 396
#define PINYIN_CHAR_COUNT     3755

/* IME state for status display. */
u32 tui_chinese_ime_syllables(void);   /* 396 */
u32 tui_chinese_ime_chars(void);       /* 3755 */

/* IME test hook: resolve a pinyin prefix to its candidate list without
 * keyboard interaction. Returns the candidate count (0 = no match),
 * fills out[0..n) with Unicode codepoints. */
int tui_chinese_ime_test(const char *pinyin, u32 *out, int cap);

/* Longest syllable matching the prefix buf[0..len): returns syllable
 * index or -1. Exact match wins; otherwise the first syllable that
 * starts with the typed prefix. (v2.2.0 file-static, exported for the
 * TUI compose pad in G11c.) */
int ime_match(const char *buf, int len);

/* Collect up to cap candidates for a syllable: level-1 chars in
 * GB2312 order (that order is frequency-ordered, so the first
 * candidates are the common ones). (v2.2.0 file-static, exported.) */
int ime_candidates(int syl, u32 *out, int cap);

#ifdef __cplusplus
}
#endif

#endif /* OC_IME_ENGINE_H */
