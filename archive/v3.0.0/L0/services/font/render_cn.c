/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10-chinese_support
 * File: chinese/render_cn.c
 * Purpose: Mixed-width (8px ASCII / 16px CJK) renderer + font registry.
 */
#include "render_cn.h"
#include "chinese_font.h"
#include "encoding.h"
#include "screen_fb.h"
#include "screen_console.h"
#include "core_sync.h"
#include "lib_string.h"

u32 render_chinese_width(u32 cp);   /* fix2 forward decl: missing-box width */

/* ---------------------------------------------------------------- fonts */

static cn_font_t g_fonts[CN_FONT_MAX];
static spinlock_t g_fontreg_lock;

static int fontreg_ready;

static void fontreg_init(void) {
    if (fontreg_ready) return;
    spin_init(&g_fontreg_lock);
    fontreg_ready = 1;
}

int font_register(const cn_font_t* f) {
    if (!f || !f->name || !f->get_bitmap || f->glyph_w == 0 || f->glyph_h == 0) {
        return -2;
    }
    fontreg_init();
    spin_lock(&g_fontreg_lock);
    for (int i = 0; i < CN_FONT_MAX; ++i) {
        if (!g_fonts[i].in_use) {
            g_fonts[i] = *f;
            g_fonts[i].in_use = 1;
            spin_unlock(&g_fontreg_lock);
            return i;
        }
        if (strcmp(g_fonts[i].name, f->name) == 0) {
            /* re-register same name: replace in place */
            g_fonts[i] = *f;
            g_fonts[i].in_use = 1;
            spin_unlock(&g_fontreg_lock);
            return i;
        }
    }
    spin_unlock(&g_fontreg_lock);
    return -1;
}

int font_unregister(const char* name) {
    if (!name) return -2;
    fontreg_init();
    spin_lock(&g_fontreg_lock);
    for (int i = 0; i < CN_FONT_MAX; ++i) {
        if (g_fonts[i].in_use && strcmp(g_fonts[i].name, name) == 0) {
            g_fonts[i].in_use = 0;
            g_fonts[i].name = NULL;
            spin_unlock(&g_fontreg_lock);
            return 0;
        }
    }
    spin_unlock(&g_fontreg_lock);
    return -3;
}

int font_count(void) {
    fontreg_init();
    spin_lock(&g_fontreg_lock);
    int n = 0;
    for (int i = 0; i < CN_FONT_MAX; ++i) {
        if (g_fonts[i].in_use) n++;
    }
    spin_unlock(&g_fontreg_lock);
    return n;
}

cn_font_t* font_select(u32 cp) {
    fontreg_init();
    cn_font_t* best = NULL;
    spin_lock(&g_fontreg_lock);
    for (int i = 0; i < CN_FONT_MAX; ++i) {
        if (!g_fonts[i].in_use) continue;
        if (g_fonts[i].get_bitmap(&g_fonts[i], cp) == NULL) continue;
        if (!best || g_fonts[i].priority > best->priority) best = &g_fonts[i];
    }
    spin_unlock(&g_fontreg_lock);
    return best;
}

const u8* font_get_bitmap(u32 cp) {
    cn_font_t* f = font_select(cp);
    if (!f) return NULL;
    return f->get_bitmap(f, cp);
}

int font_get_metrics(u32 cp, u32* w, u32* h) {
    if (!w || !h) return -2;
    cn_font_t* f = font_select(cp);
    if (!f) return -1;
    *w = f->glyph_w;
    *h = f->glyph_h;
    return 0;
}

/* ------------------------------------------------- embedded GB2312 font */

static const u8* gb2312_font_get_bitmap(cn_font_t* self, u32 cp) {
    (void)self;
    if (cp < 0x100) return NULL;   /* ASCII is owned by the ASCII font */
    return chinese_font_get_bitmap_unicode(0, cp);
}

static cn_font_t gb2312_cn_font = {
    .name     = "gb2312-16x16",
    .get_bitmap = gb2312_font_get_bitmap,
    .glyph_w  = CHINESE_FONT_GLYPH_W,
    .glyph_h  = CHINESE_FONT_GLYPH_H,
    .priority = 10,
    .in_use   = 0,
};

/* ASCII font descriptor wrapping the built-in screen_font8x16 table. */
static const u8* ascii_font_get_bitmap(cn_font_t* self, u32 cp) {
    (void)self;
    if (cp >= 0x100) return NULL;
    return screen_font8x16[cp & 0xFF];
}

static cn_font_t ascii_cn_font = {
    .name     = "ascii-8x16",
    .get_bitmap = ascii_font_get_bitmap,
    .glyph_w  = OC_FONT_CELL_W,
    .glyph_h  = OC_FONT_CELL_H,
    .priority = 0,
    .in_use   = 0,
};

/* -------------------------------------------------------------- drawing */

void cn_draw_missing_box(u32 x, u32 y, u32 w, u32 h, u32 fg, u32 bg) {
    screen_fb_fill_rect(x, y, w, h, bg);
    if (h >= 2 && w >= 2) {
        screen_fb_fill_rect(x, y, w, 1, fg);
        screen_fb_fill_rect(x, y + h - 1, w, 1, fg);
        screen_fb_fill_rect(x, y, 1, h, fg);
        screen_fb_fill_rect(x + w - 1, y, 1, h, fg);
    }
}

static void blit_8x16(u32 x, u32 y, const u8* glyph, u32 fg, u32 bg) {
    const screen_fb_info_t* fb = screen_fb_get_info();
    for (u32 row = 0; row < 16; ++row) {
        u8 bits = glyph[row];
        u32 ypix = y + row;
        if (ypix >= fb->height) break;
        u8* rowptr = fb->addr + (u64)ypix * fb->pitch;
        for (u32 col = 0; col < 8; ++col) {
            u32 xpix = x + col;
            if (xpix >= fb->width) break;
            *(u32*)(rowptr + (u64)xpix * 4) =
                (bits & (0x80 >> col)) ? fg : bg;
        }
    }
}

static void blit_16x16(u32 x, u32 y, const u8* glyph, u32 fg, u32 bg) {
    const screen_fb_info_t* fb = screen_fb_get_info();
    for (u32 row = 0; row < 16; ++row) {
        u16 bits = (u16)((glyph[row * 2] << 8) | glyph[row * 2 + 1]);
        u32 ypix = y + row;
        if (ypix >= fb->height) break;
        u8* rowptr = fb->addr + (u64)ypix * fb->pitch;
        for (u32 col = 0; col < 16; ++col) {
            u32 xpix = x + col;
            if (xpix >= fb->width) break;
            *(u32*)(rowptr + (u64)xpix * 4) =
                (bits & (0x8000 >> col)) ? fg : bg;
        }
    }
}

/* Shared glyph entry: resolves cp through the font registry. */
void cn_draw_glyph_px(u32 x, u32 y, u32 cp, u32 fg, u32 bg) {
    cn_font_t* f = font_select(cp);
    const u8* bits = f ? f->get_bitmap(f, cp) : NULL;
    if (!f || !bits) {
        /* Missing glyph: the hollow box width always matches the
         * advance render_chinese_width() reports (fix2: an 8px advance
         * with a 16px box let the next glyph overwrite the box's right
         * half). */
        u32 w = render_chinese_width(cp);
        cn_draw_missing_box(x, y, w, OC_FONT_CELL_H, fg, bg);
        return;
    }
    if (f->glyph_w >= 16) {
        blit_16x16(x, y, bits, fg, bg);
    } else {
        blit_8x16(x, y, bits, fg, bg);
    }
}

static void cn_draw_glyph(u32 x, u32 y, u32 cp, u32 fg_pixel, u32 bg_pixel) {
    cn_draw_glyph_px(x, y, cp, fg_pixel, bg_pixel);
}

screen_renderer_t cn_renderer = {
    .draw_glyph = cn_draw_glyph,
    .cell_w     = OC_FONT_CELL_W,
    .cell_h     = OC_FONT_CELL_H,
    .name       = "cn-8x16-16x16",
};

int render_chinese_init(void) {
    chinese_font_init();
    fontreg_init();
    if (!ascii_cn_font.in_use) font_register(&ascii_cn_font);
    if (!gb2312_cn_font.in_use) font_register(&gb2312_cn_font);
    screen_renderer_set_active(&cn_renderer);
    return 0;
}

int render_chinese_char(u32 cp, u32 px, u32 py, u32 fg, u32 bg) {
    cn_draw_glyph_px(px, py, cp, fg, bg);
    return 0;
}

int render_chinese_string(const char* utf8, u32 px, u32 py, u32 fg, u32 bg) {
    if (!utf8) return -2;
    int drawn = 0;
    u32 x = px;
    const u8* p = (const u8*)utf8;
    while (*p) {
        u32 cp, consumed;
        if (encoding_utf8_decode(p, 4, &cp, &consumed) != 0) {
            /* invalid byte: draw as missing box for one cell */
            cn_draw_missing_box(x, py, OC_FONT_CELL_W, OC_FONT_CELL_H, fg, bg);
            x += OC_FONT_CELL_W;
            p += 1;
            drawn++;
            continue;
        }
        u32 w = render_chinese_width(cp);
        cn_draw_glyph_px(x, py, cp, fg, bg);
        x += w;
        p += consumed;
        drawn++;
        if (x + 16 > screen_fb_get_info()->width) break;
    }
    return drawn;
}

/* fix2: codepoints with no registered font still need a self-consistent
 * width - cn_draw_glyph_px draws a 16px-wide hollow box for missing
 * CJK-range codepoints, so the advance must be 16px there too (before
 * this fix the box was drawn 16px wide but the advance stayed 8px and
 * the next glyph overwrote the box's right half). CJK ranges follow
 * Unicode East Asian Width (Wide/Fullwidth):
 *   0x2E80-0x9FFF  CJK radicals..Unified Ideographs + Hangul
 *   0xAC00-0xD7A3  Hangul syllables (inside the range above already)
 *   0xF900-0xFAFF  CJK compatibility ideographs
 *   0xFF00-0xFF60  fullwidth forms
 *   0x20000-0x2FFFD / 0x30000-0x3FFFD  supplementary planes */
static int cp_is_cjk_range(u32 cp) {
    return (cp >= 0x2E80 && cp <= 0x9FFF) ||
           (cp >= 0xF900 && cp <= 0xFAFF) ||
           (cp >= 0xFF00 && cp <= 0xFF60) ||
           (cp >= 0x20000 && cp <= 0x2FFFD) ||
           (cp >= 0x30000 && cp <= 0x3FFFD);
}

u32 render_chinese_width(u32 cp) {
    if (cp < 0x100) return OC_FONT_CELL_W;
    cn_font_t* f = font_select(cp);
    if (f && f->glyph_w >= 16) return 16;
    if (!f && cp_is_cjk_range(cp)) return 16;   /* missing-box width */
    return OC_FONT_CELL_W;
}

u32 render_chinese_cols(u32 cp) {
    return render_chinese_width(cp) / OC_FONT_CELL_W;
}

int render_chinese_cursor_move(int dir) {
    screen_console_t* con = screen_console_get();
    if (!con) return -2;
    if (dir == 0) return (int)con->cur_x;
    u32 cols = con->cols;
    i32 x = (i32)con->cur_x;
    if (dir < 0) {
        x -= 1;   /* step over the continuation cell of a CJK pair */
        if (x < 0) x = 0;
    } else {
        x += 1;
        if ((u32)x >= cols) x = (i32)cols - 1;
    }
    screen_console_move_cursor((u32)x, con->cur_y);
    return (int)con->cur_x;
}

/* Width of the last character written through the cn write path
 * (cn_putcw / cn_puts_utf8). The pure-ASCII putc path leaves it at 1
 * via the reset below. Backspace erases exactly that many cells. */
static u32 g_last_write_cols = 1;

void cn_note_write_cols(u32 cols) {
    g_last_write_cols = (cols == 0) ? 1 : cols;
}

int render_chinese_backspace(void) {
    u32 cols = g_last_write_cols;
    for (u32 i = 0; i < cols; ++i) screen_console_putc('\b');
    for (u32 i = 0; i < cols; ++i) screen_console_putc(' ');
    for (u32 i = 0; i < cols; ++i) screen_console_putc('\b');
    g_last_write_cols = 1;
    return (int)screen_console_get()->cur_x;
}
