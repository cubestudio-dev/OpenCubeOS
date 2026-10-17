/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10-chinese_support
 * File: chinese/tui_chinese.h
 * Purpose: TUI Chinese layer - UTF-8 windows/printing, CJK-aware
 *          menus/forms, and the pinyin IME input line.
 *
 * tui_cell_t already stores u32 codepoints and tui_print decodes
 * UTF-8, so a CJK glyph occupies a two-cell slot automatically. This
 * layer adds the convenience entry points the applications use and
 * the IME.
 *
 * Driver-API style extension (the numbered L1 set stays closed at
 * 1-138).
 */
#ifndef OC_TUI_CHINESE_H
#define OC_TUI_CHINESE_H

#include "../kernel/types.h"
#include "../tui/tui.h"

/* Init: render_chinese_init + tui UTF-8 path ready. Idempotent. */
int tui_chinese_init(void);

/* Create a window with a UTF-8 (Chinese) title. Mirrors
 * tui_window_new + tui_window_title. Returns NULL on bad args. */
tui_window_t* tui_chinese_window(const char* utf8_title,
                                 int y, int x, int h, int w,
                                 int bordered);

/* UTF-8 print to the focused window (accepts mixed CJK/ASCII). */
int tui_chinese_print(const char* utf8);

/* UTF-8 print at a window-relative position. */
int tui_chinese_print_at(int y, int x, const char* utf8);

/* Console columns a UTF-8 string occupies (CJK = 2 per glyph).
 * Used by the applications for right-alignment inside windows. */
u32 tui_chinese_str_cols(const char* utf8);

/* Run a menu whose items have UTF-8 (Chinese) names. Selection keys:
 * Up/Down/Enter/q-Esc. Returns the picked index or -1 (cancelled). */
int tui_chinese_menu(tui_window_t* win, tui_menu_item_t* items,
                     int count);

/* Run a form whose field labels hold UTF-8 (Chinese) text. Editing is
 * ASCII; existing CJK content in a field renders correctly and the
 * cursor lands after the last glyph. Returns 0 (confirmed) or -1. */
int tui_chinese_form(tui_window_t* win, tui_field_t* fields, int count);

/* Pinyin IME (GB2312 level-1, 3755 chars / 396 syllables).
 * Type ASCII letters to build a syllable; candidates render on the
 * prompt row; digits 1-9 pick, Space = candidate 1, Backspace
 * deletes the last letter, Enter commits the composed text,
 * Esc cancels. `ascii_mode` starts disabled. The composed UTF-8 text
 * lands in buf (NUL-terminated). Returns the byte length, or -1 on
 * cancel, -2 on bad args. */
int tui_chinese_input(tui_window_t* win, const char* prompt,
                      char* buf, int cap);

/* Direct ASCII line input: never enters pinyin composition.  Same
 * window contract as tui_chinese_input (prompt row at the window
 * bottom, Enter commits, Esc cancels).  Returns the byte length,
 * or -1 on cancel, -2 on bad args. */
int tui_chinese_input_ascii(tui_window_t* win, const char* prompt,
                            char* buf, int cap);

/* Full-screen pinyin composition pad: the launcher menu's user-facing
 * IME entry (type letters, digits 1-9 pick, space = first candidate,
 * Enter commits to the pad, Esc leaves).  Returns 0 or -1. */
int tui_cn_input_app(void);

/* fix3: the Chinese TUI launcher (hardware config / wireless / system
 * info / Chinese input menu).  Shared by `tui-cn` and by `tui` when
 * shell.lang=zh (see shell/shell_lang.h).  Returns 0 or 1. */
int tui_cn_launcher(void);

/* IME state for status display. */
u32 tui_chinese_ime_syllables(void);   /* 396 */
u32 tui_chinese_ime_chars(void);       /* 3755 */

/* IME test hook: resolve a pinyin prefix to its candidate list without
 * keyboard interaction. Returns the candidate count (0 = no match),
 * fills out[0..n) with Unicode codepoints. */
int tui_chinese_ime_test(const char* pinyin, u32* out, int cap);

#endif /* OC_TUI_CHINESE_H */
