/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS 3.0.0-WP04
 * File: services/display/ime_engine.c
 * Purpose: the pinyin IME engine, revived in G11b verbatim from
 * v2.2.0 chinese/tui_chinese.c (IME section).  The bodies below are
 * the v2.2.0 code carried over mechanically; only the storage class
 * of ime_match()/ime_candidates() changed (file-static -> exported,
 * see ime_engine.h).  The TUI-side compose pad stays in the TUI
 * service (revived in G11c) and calls into this engine.
 */
#include "ime_engine.h"
#include "lib_string.h"

/* Generated tables (pinyin_data.c). */
extern const u8  pinyin_syllable_pool[];
extern const u16 pinyin_syllable_off[];
extern const u16 pinyin_char_map[2 * PINYIN_CHAR_COUNT];

static void syllable_str(int idx, char* out, int cap) {
    u16 start = pinyin_syllable_off[idx];
    u16 end = pinyin_syllable_off[idx + 1];
    int n = 0;
    for (u16 i = start; i < end && n + 1 < cap; i++) {
        out[n++] = (char)pinyin_syllable_pool[i];
    }
    out[n] = 0;
}

/* Longest syllable matching the prefix buf[0..len): returns syllable
 * index or -1. Exact match wins; otherwise the first syllable that
 * starts with the typed prefix. */
int ime_match(const char* buf, int len) {
    int exact = -1, prefix = -1;
    for (int i = 0; i < PINYIN_SYLLABLE_COUNT; i++) {
        char s[8];
        syllable_str(i, s, sizeof(s));
        int sl = (int)strlen(s);
        if (sl == len && strncmp(s, buf, (usize)len) == 0) {
            exact = i;
            break;
        }
        if (prefix < 0 && sl > len &&
            strncmp(s, buf, (usize)len) == 0) {
            prefix = i;
        }
    }
    return exact >= 0 ? exact : prefix;
}

/* Collect up to 9 candidates for a syllable: level-1 chars in GB2312
 * order (that order is frequency-ordered, so the first candidates are
 * the common ones). */
int ime_candidates(int syl, u32* out, int cap) {
    int n = 0;
    for (int i = 0; i < PINYIN_CHAR_COUNT && n < cap; i++) {
        if ((int)pinyin_char_map[2 * i + 1] == syl) {
            out[n++] = pinyin_char_map[2 * i];
        }
    }
    return n;
}

u32 tui_chinese_ime_syllables(void) { return PINYIN_SYLLABLE_COUNT; }
u32 tui_chinese_ime_chars(void) { return PINYIN_CHAR_COUNT; }

int tui_chinese_ime_test(const char* pinyin, u32* out, int cap) {
    if (!pinyin || !out || cap <= 0) return -2;
    int len = (int)strlen(pinyin);
    if (len == 0 || len > 6) return -2;
    int m = ime_match(pinyin, len);
    if (m < 0) return 0;
    return ime_candidates(m, out, cap);
}
