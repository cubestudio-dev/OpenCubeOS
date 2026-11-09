/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10-chinese_support
 * File: chinese/render_cn.h
 * Purpose: Chinese-aware rendering: pluggable font engine registry and
 *          the mixed-width glyph renderer.
 *
 * Width model: the console cell stays 8x16 (OC_FONT_CELL_W/H).  A CJK
 * glyph is 16x16 pixels and occupies TWO console cells (cursor units),
 * an ASCII glyph stays 8x16 in one cell.  render_chinese_width()
 * reports the pixel width; render_chinese_cols() reports the cell
 * width (1 or 2) that console/lineedit math uses.
 *
 * The renderer registered here ("cn-8x16-16x16") keeps the exact
 * default drawing path for cp < 0x100 and adds CJK glyphs for cp >=
 * 0x100, so switching renderers never changes ASCII output.
 *
 * Driver-API style extension (the numbered L1 set stays closed at
 * 1-138).
 */
#ifndef OC_RENDER_CN_H
#define OC_RENDER_CN_H

#include "../kernel/types.h"
#include "../drivers/display/screen_font.h"

#define CN_FONT_MAX 8   /* pluggable font engine slots */

/* A pluggable font: resolves a codepoint to a packed bitmap.
 * get_bitmap returns CHINESE_FONT_GLYPH_BYTES (32) for 16x16 fonts or
 * 16 bytes for 8x16 fonts (rows*2 for 16 wide), NULL when cp is not
 * covered. */
typedef struct cn_font {
    const char* name;
    /* Return the glyph bitmap for cp, or NULL when not covered. */
    const u8* (*get_bitmap)(struct cn_font* self, u32 cp);
    u32 glyph_w;      /* 8 or 16 */
    u32 glyph_h;      /* 16      */
    u32 priority;     /* higher wins the per-cp selection race */
    int in_use;
} cn_font_t;

/* -------- font engine registry (interfaces per WP spec section 7.4) -- */

/* Register a font. Copies the descriptor into the registry slot.
 * Returns slot id >= 0, -1 when full, -2 on bad args. */
int font_register(const cn_font_t* f);

/* Unregister by name. Returns 0, or -3 when not found. */
int font_unregister(const char* name);

/* Pick the font that covers cp: among registered fonts whose
 * get_bitmap(self, cp) is non-NULL, the highest priority wins. Returns
 * the descriptor (borrowed pointer) or NULL when nothing covers cp. */
cn_font_t* font_select(u32 cp);

/* Get the bitmap for cp through the selected font. Returns NULL when
 * nothing covers cp. */
const u8* font_get_bitmap(u32 cp);

/* Metrics of the font that covers cp: glyph_w/glyph_h in pixels.
 * Returns 0 on success, -1 when nothing covers cp, -2 on NULL out. */
int font_get_metrics(u32 cp, u32* w, u32* h);

/* Number of registered fonts (status/diag). */
int font_count(void);

/* -------- renderer extension (WP spec section 7.3) ------------------- */

/* Init: chinese_font_init + register the embedded GB2312 font in the
 * font registry + make the mixed-width renderer active for the
 * console. Idempotent. Returns 0 on success. */
int render_chinese_init(void);

/* Draw one codepoint at PIXEL (px, py): 8x16 for ASCII, 16x16 for CJK.
 * Missing glyphs draw as a hollow box. */
int render_chinese_char(u32 cp, u32 px, u32 py, u32 fg, u32 bg);

/* Render a UTF-8 string at pixel position, advancing px by the width
 * of each glyph. Returns the number of codepoints drawn (control
 * chars are skipped and count as drawn). */
int render_chinese_string(const char* utf8, u32 px, u32 py, u32 fg, u32 bg);

/* Pixel width of cp: 8 for ASCII/unknown, 16 for CJK. */
u32 render_chinese_width(u32 cp);

/* Cell (console column) width of cp: 1 or 2. */
u32 render_chinese_cols(u32 cp);

/* Cursor move by ONE CHARACTER (not cell): dir<0 left, >0 right.
 * Returns the new cursor column, or -2 on bad args. */
int render_chinese_cursor_move(int dir);

/* Backspace one CHARACTER at the console cursor: erases both cells of
 * a CJK glyph. Returns the new cursor column, or -2 on bad args. */
int render_chinese_backspace(void);

/* The mixed-width renderer instance (8x16 cell; CJK spans 2 cells).
 * Activate with screen_renderer_set_active(&cn_renderer). */
extern screen_renderer_t cn_renderer;

/* Draw the glyph used when a codepoint has no font coverage. */
void cn_draw_missing_box(u32 x, u32 y, u32 w, u32 h, u32 fg, u32 bg);

/* Draw any codepoint at PIXEL (x, y) through the font registry
 * (8x16 or 16x16 per coverage). Used by console_cn and TUI fb backends. */
void cn_draw_glyph_px(u32 x, u32 y, u32 cp, u32 fg, u32 bg);

/* Record the cell-width of the last character written through the cn
 * write path so render_chinese_backspace erases the right span. */
void cn_note_write_cols(u32 cols);

#endif /* OC_RENDER_CN_H */
