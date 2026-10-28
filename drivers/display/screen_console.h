/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-01
 * File: drivers/display/screen_console.h
 * Purpose: Text-mode console built on top of the framebuffer + renderer.
 *
 * The console owns:
 *   - a character grid (cols ? rows, derived from fb size / cell size)
 *   - a software cursor (block style, toggled by console_show_cursor)
 *   - a software scroll (rows pushed off the top are lost)
 *   - foreground/background colors packed as framebuffer-native u32 pixels
 *
 * It also owns the CONSOLE OUTPUT HOOK: every character the console
 * emits is forwarded to the installed hook before being drawn (3.0.0:
 * the hook storage moved down into the driver - it used to live behind
 * l1_ext_set_console_hook() in services/, an inverted dependency).
 * Consumers: the serial tee (boot), the klog capture stack (core_kcap)
 * and the CJK wide-cell path (console_cn).
 */
#ifndef OC_CONSOLE_H
#define OC_CONSOLE_H

#include "types.h"

typedef struct screen_console {
    u32  cols;
    u32  rows;
    u32  cur_x;       /* in cells */
    u32  cur_y;
    u32  fg_pixel;    /* foreground color, packed */
    u32  bg_pixel;    /* background color, packed */
    int  cursor_visible;
} screen_console_t;

/* Initialize the console against the active framebuffer + renderer. */
int  screen_console_init(void);

/* Reset the console: clear, home cursor, default colors. */
void screen_console_reset(u32 fg_pixel, u32 bg_pixel);

/* Cursor. */
void screen_console_show_cursor(int visible);
void screen_console_move_cursor(u32 x, u32 y);

/* Output. */
void screen_console_putc (char ch);          /* handles \n, \r, \t */
void screen_console_puts (const char* s);
void screen_console_clear(void);

/* ---- Console output hook (driver-owned since 3.0.0) ----------------
 * Called for every character BEFORE it is drawn.  Return 0 = let the
 * console draw the char normally; non-zero = the hook handled it
 * (teed it to serial, captured it, suppressed it) and the console must
 * not draw it.  Only ONE hook is installed at a time; stacking is the
 * hook's own business (see core_kcap.c for the capture stack).
 * 3.0.0-WP03: a hook consumer that temporarily swaps the hook (the
 * WP-01 self-test) MUST snapshot + restore it via
 * screen_console_get_hook(), so the boot-time serial tee (installed
 * through core_kcap) survives the swap. */
typedef int (*screen_console_hook_fn)(void* ctx, u8 ch);
typedef struct {
    screen_console_hook_fn fn;
    void                   *ctx;
} screen_console_hook_t;
void screen_console_set_hook(screen_console_hook_fn hook, void* ctx);
/* Snapshot the installed hook (fn+ctx) for a later exact restore. */
void screen_console_get_hook(screen_console_hook_t *out);
/* Invoke the installed hook (returns its verdict; 0 = not present). */
int  screen_console_call_hook(u8 ch);

/* WP-10-chinese_support: scroll up one cell-row without moving the
 * cursor (used by the cn wide-write path). */
void screen_console_scroll_up(void);

/* Direct cell access (used by L1 extensions / debug). */
void screen_console_putc_at(u32 x, u32 y, char ch, u32 fg_pixel, u32 bg_pixel);

/* Get a handle to the console state. */
screen_console_t* screen_console_get(void);

/* Color helpers - construct a u32 color from RGB. Wraps fb.h. */
u32 screen_color_rgb(u8 r, u8 g, u8 b);

/* Some pre-baked colors (RGB). */
#define OC_COLOR_BLACK   0x000000
#define OC_COLOR_WHITE   0xFFFFFF
#define OC_COLOR_RED     0xFF3030
#define OC_COLOR_GREEN   0x30FF70
#define OC_COLOR_YELLOW  0xE0E040
#define OC_COLOR_BLUE    0x4080FF
#define OC_COLOR_CYAN    0x40F0FF
#define OC_COLOR_MAGENTA 0xFF40FF
#define OC_COLOR_ORANGE  0xFFA040
#define OC_COLOR_GRAY    0x808080

#endif /* OC_CONSOLE_H */
