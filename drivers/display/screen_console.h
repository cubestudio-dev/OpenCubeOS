/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-01
 * File: kernel/console.h
 * Purpose: Text-mode console built on top of the framebuffer + renderer.
 *
 * The console owns:
 *   - a character grid (cols ? rows, derived from fb size / cell size)
 *   - a software cursor (block style, toggled by console_show_cursor)
 *   - a software scroll (rows pushed off the top are lost)
 *   - foreground/background colors packed as framebuffer-native u32 pixels
 *
 * It also exposes an OUTPUT HOOK for L1 extensions: every character the
 * console emits is forwarded to l1_ext_console_hook(ch) before being
 * drawn. This lets L1 tee console output to a serial port, a log buffer,
 * or a network sink.
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
