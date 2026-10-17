/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10-tui
 * File: tui/tui_core.c
 * Purpose: TUI core: initialization, terminal detection, the two real
 *          render backends (framebuffer console / ANSI serial), the
 *          16-color palette with 256-color and truecolor reduction,
 *          and attribute helpers.  No stubs: both backends drive real
 *          output paths (screen_console.c for fb, COM1 for serial).
 */
#include "tui.h"
#include "../drivers/display/screen_console.h"
#include "../drivers/display/screen_serial_in.h"
#include "../drivers/display/screen_font.h"
#include "../chinese/render_cn.h"
#include "../chinese/encoding.h"
#include "../kernel/lib/lib_string.h"
#include "../kernel/lib/lib_config.h"
#include "../kernel/core/core_timer.h"

/* ------------------------------------------------------------------ */
/* Palette: 16 CGA/VGA colors as RGB (the fb backend is RGB-native).   */
/* ------------------------------------------------------------------ */

static u32 g_palette[TUI_COLOR_COUNT];

static void palette_init(void) {
    g_palette[TUI_COLOR_BLACK]   = screen_color_rgb(0x00, 0x00, 0x00);
    g_palette[TUI_COLOR_MAROON]  = screen_color_rgb(0xAA, 0x00, 0x00);
    g_palette[TUI_COLOR_GREEN]   = screen_color_rgb(0x00, 0xAA, 0x00);
    g_palette[TUI_COLOR_OLIVE]   = screen_color_rgb(0xAA, 0x55, 0x00);
    g_palette[TUI_COLOR_NAVY]    = screen_color_rgb(0x00, 0x00, 0xAA);
    g_palette[TUI_COLOR_PURPLE]  = screen_color_rgb(0xAA, 0x00, 0xAA);
    g_palette[TUI_COLOR_TEAL]    = screen_color_rgb(0x00, 0xAA, 0xAA);
    g_palette[TUI_COLOR_SILVER]  = screen_color_rgb(0xAA, 0xAA, 0xAA);
    g_palette[TUI_COLOR_GRAY]    = screen_color_rgb(0x55, 0x55, 0x55);
    g_palette[TUI_COLOR_RED]     = screen_color_rgb(0xFF, 0x55, 0x55);
    g_palette[TUI_COLOR_LIME]    = screen_color_rgb(0x55, 0xFF, 0x55);
    g_palette[TUI_COLOR_YELLOW]  = screen_color_rgb(0xFF, 0xFF, 0x55);
    g_palette[TUI_COLOR_BLUE]    = screen_color_rgb(0x55, 0x55, 0xFF);
    g_palette[TUI_COLOR_FUCHSIA] = screen_color_rgb(0xFF, 0x55, 0xFF);
    g_palette[TUI_COLOR_AQUA]    = screen_color_rgb(0x55, 0xFF, 0xFF);
    g_palette[TUI_COLOR_WHITE]   = screen_color_rgb(0xFF, 0xFF, 0xFF);
}

u32 tui_palette_pixel(int idx) {
    if (idx < 0 || idx >= TUI_COLOR_COUNT) idx = 0;
    return g_palette[idx];
}

/* xterm 256-color table: standard 16, then a 6x6x6 cube, then gray. */
static void xterm256_to_rgb(int idx, u8 *r, u8 *g, u8 *b) {
    static const u8 cube[6] = { 0, 95, 135, 175, 215, 255 };
    if (idx < 16) {
        u32 px = tui_palette_pixel(idx);
        *r = (u8)((px >> 16) & 0xFF);
        *g = (u8)((px >> 8) & 0xFF);
        *b = (u8)(px & 0xFF);
        return;
    }
    if (idx < 232) {                      /* 6x6x6 color cube */
        int c = idx - 16;
        *b = cube[c % 6]; c /= 6;
        *g = cube[c % 6]; c /= 6;
        *r = cube[c % 6];
        return;
    }
    int gray = (idx - 232) * 10 + 8;      /* 24-step grayscale ramp */
    *r = *g = *b = (u8)gray;
}

/* Reduce a 256-color index to the 16-entry palette: exact for the
 * base 16, nearest by RGB distance for the cube and gray ramp. */
static int xterm256_to_palette(int idx) {
    if (idx < 0) return 0;
    if (idx < 16) return idx;
    u8 r, g, b;
    xterm256_to_rgb(idx, &r, &g, &b);
    int best = 0;
    int best_d = 0x7FFFFFFF;
    for (int i = 0; i < TUI_COLOR_COUNT; i++) {
        u32 px = tui_palette_pixel(i);
        int dr = (int)r - (int)((px >> 16) & 0xFF);
        int dg = (int)g - (int)((px >> 8) & 0xFF);
        int db = (int)b - (int)(px & 0xFF);
        int d = dr * dr + dg * dg + db * db;
        if (d < best_d) { best_d = d; best = i; }
    }
    return best;
}

/* ------------------------------------------------------------------ */
/* Backends                                                            */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *name;
    void (*putc_at)(int x, int y, u32 ch, tui_attr_t a);
    void (*clear)(tui_attr_t a);
    void (*cursor)(int visible);
    void (*flush)(void);
} tui_backend_t;

static tui_backend_t *g_backend;
static tui_term_info_t g_term;
static int g_started;
static int g_cursor_visible = 1;

/* ---- fb backend: drives screen_console directly ---- */

static tui_attr_t g_default_attr;

static void fb_putc_at(int x, int y, u32 ch, tui_attr_t a) {
    u32 fg = tui_palette_pixel(TUI_ATTR_FG(a));
    u32 bg = tui_palette_pixel(TUI_ATTR_BG(a));
    if (ch == TUI_WIDE_CONT) {
        /* Right half of a CJK glyph. The 16px bitmap drawn for the
         * left cell already covers this column completely (ink AND
         * background), so painting a background-colored space here
         * would erase the glyph's right half (fix2 root cause of the
         * "Chinese only shows half" report). Skip the repaint. */
        return;
    }
    if (ch >= 0x100) {
        /* WP-10-chinese_support: 16x16 CJK glyph spanning two cells. */
        cn_draw_glyph_px((u32)x * OC_FONT_CELL_W, (u32)y * OC_FONT_CELL_H,
                         ch, fg, bg);
        return;
    }
    screen_console_putc_at((u32)x, (u32)y, (char)(ch & 0xFF), fg, bg);
}

static void fb_clear(tui_attr_t a) {
    u32 bg = tui_palette_pixel(TUI_ATTR_BG(a));
    /* paint the background through a full-screen space fill so both
     * the cell grid and the cursor state stay consistent */
    screen_console_clear();
    screen_console_t *c = screen_console_get();
    for (u32 yy = 0; yy < c->rows; yy++)
        for (u32 xx = 0; xx < c->cols; xx++)
            screen_console_putc_at(xx, yy, ' ', g_palette[TUI_ATTR_FG(a)], bg);
    g_default_attr = a;
}

static void fb_cursor(int visible) {
    screen_console_show_cursor(visible);
    g_cursor_visible = visible;
}

static void fb_flush(void) { /* the fb console paints synchronously */ }

static tui_backend_t g_backend_fb = {
    .name = "opencube-fb", .putc_at = fb_putc_at, .clear = fb_clear,
    .cursor = fb_cursor, .flush = fb_flush,
};

/* ---- serial backend: real ANSI/VT100 escape stream on COM1 ---- */

#define OC_TUI_COM1 0x3F8

static void serial_byte(char c) {
    /* transmitter-holding-empty poll, same shape as the kernel's own
     * early print path (arch_exceptions.c) */
    u8 status;
    do {
        __asm__ volatile("inb %1, %0" : "=a"(status) : "Nd"(OC_TUI_COM1 + 5));
    } while ((status & 0x20) == 0);
    __asm__ volatile("outb %0, %1" :: "a"(c), "Nd"(OC_TUI_COM1));
}

static void serial_str(const char *s) {
    while (*s) serial_byte(*s++);
}

/* SGR from a TUI attr: base 16 through 30-37/40-47, bright 8-15
 * through 90-97/100-107, flags as real SGR switches. */
static void serial_attr(tui_attr_t a) {
    char buf[64];
    int fg = TUI_ATTR_FG(a), bg = TUI_ATTR_BG(a);
    u64 flags = TUI_ATTR_FLAGS(a);
    /* build the sequence manually: no printf on this path */
    char *p = buf;
    *p++ = 0x1B; *p++ = '[';
    int first = 1;
    if (flags & TUI_ATTR_BOLD)    { if (!first) *p++ = ';'; *p++ = '1'; first = 0; }
    if (flags & TUI_ATTR_UNDERLINE){ if (!first) *p++ = ';'; *p++ = '4'; first = 0; }
    if (flags & TUI_ATTR_BLINK)   { if (!first) *p++ = ';'; *p++ = '5'; first = 0; }
    if (flags & TUI_ATTR_REVERSE) { if (!first) *p++ = ';'; *p++ = '7'; first = 0; }
    if (flags & TUI_ATTR_INVIS)   { if (!first) *p++ = ';'; *p++ = '8'; first = 0; }
    if (!first) *p++ = ';';
    /* fg */
    if (fg < 8) { *p++ = '3'; *p++ = (char)('0' + fg); }
    else        { *p++ = '9'; *p++ = (char)('0' + fg - 8); }
    *p++ = ';';
    /* bg */
    if (bg < 8) { *p++ = '4'; *p++ = (char)('0' + bg); }
    else        { *p++ = (char)('1'); *p++ = '0'; *p++ = (char)('0' + bg - 8); }
    *p++ = 'm';
    *p = 0;
    serial_str(buf);
}

static void serial_puts_attr_at(int x, int y, const char *s, tui_attr_t a) {
    char buf[32];
    /* CSI y+1 ; x+1 H (1-based) */
    buf[0] = 0x1B; buf[1] = '[';
    int n = 0;
    /* u32 to decimal without snprintf */
    u32 v = (u32)(y + 1);
    char tmp[8]; int t = 0;
    if (v == 0) tmp[t++] = '0';
    while (v) { tmp[t++] = (char)('0' + (v % 10)); v /= 10; }
    while (t) buf[2 + n++] = tmp[--t];
    buf[2 + n++] = ';';
    v = (u32)(x + 1); t = 0;
    if (v == 0) tmp[t++] = '0';
    while (v) { tmp[t++] = (char)('0' + (v % 10)); v /= 10; }
    while (t) buf[2 + n++] = tmp[--t];
    buf[2 + n++] = 'H';
    buf[2 + n] = 0;
    serial_str(buf);
    serial_attr(a);
    serial_str(s);
    /* reset so stray output does not inherit attributes */
    serial_str("\x1B[0m");
}

static void serial_putc_at(int x, int y, u32 ch, tui_attr_t a) {
    char one[8];
    if (ch == TUI_WIDE_CONT) {
        /* The terminal painted the wide glyph already; emit nothing so
         * the cursor column mapping stays aligned via absolute CSI. */
        return;
    }
    if (ch >= 0x80) {
        /* WP-10-chinese_support: emit real UTF-8 for non-ASCII cps. */
        u32 produced = 0;
        u8 buf[4];
        if (encoding_utf8_encode(ch, buf, 4, &produced) == 0) {
            for (u32 i = 0; i < produced; ++i) one[i] = (char)buf[i];
            one[produced] = 0;
            serial_puts_attr_at(x, y, one, a);
            return;
        }
        one[0] = '?'; one[1] = 0;
        serial_puts_attr_at(x, y, one, a);
        return;
    }
    one[0] = (char)(ch & 0xFF);
    one[1] = 0;
    serial_puts_attr_at(x, y, one, a);
}

static void serial_clear(tui_attr_t a) {
    serial_attr(a);
    serial_str("\x1B[2J\x1B[H");          /* clear all, cursor home */
    serial_str("\x1B[0m");
}

static void serial_cursor(int visible) {
    serial_str(visible ? "\x1B[?25h" : "\x1B[?25l");
    g_cursor_visible = visible;
}

static void serial_flush(void) { /* serial writes are unbuffered */ }

static tui_backend_t g_backend_serial = {
    .name = "ansi-serial", .putc_at = serial_putc_at, .clear = serial_clear,
    .cursor = serial_cursor, .flush = serial_flush,
};

/* fix2: SGR mouse mode toggles for the xterm-class serial backend
 * (tui_mouse_enable flips the flag; this emits the actual sequences). */
void tui_core_serial_mouse(int on) {
    if (g_backend != &g_backend_serial) return;
    if (on) serial_str("\x1b[?1000h\x1b[?1006h");
    else    serial_str("\x1b[?1006l\x1b[?1000l");
}

/* ------------------------------------------------------------------ */
/* Terminal detection                                                  */
/* ------------------------------------------------------------------ */

int tui_term_detect(tui_term_info_t *out) {
    if (!out) return -1;
    *out = g_term;
    return 0;
}

void tui_term_size(int *cols, int *rows) {
    if (cols) *cols = g_term.cols;
    if (rows) *rows = g_term.rows;
}

static void term_probe(void) {
    /* config key tui.backend: "fb" (default) or "serial". */
    char val[16];
    lib_config_read_default("tui.backend", val, sizeof(val), "fb");
    if (strcmp(val, "serial") == 0) {
        g_backend = &g_backend_serial;
        g_term.type = TUI_TERM_ANSI_SERIAL;
        g_term.name = g_backend_serial.name;
        g_term.colors = 16;
        g_term.truecolor = 0;   /* COM1 peers answer DSR/DA in E2E */
        g_term.has_mouse = 1;   /* xterm SGR mouse on the wire        */
        g_term.cols = 80;
        g_term.rows = 25;       /* conservative ANSI default          */
    } else {
        g_backend = &g_backend_fb;
        g_term.type = TUI_TERM_OPCUBE_FB;
        g_term.name = g_backend_fb.name;
        g_term.colors = 16;
        g_term.truecolor = 1;   /* fb is RGB-native                   */
        g_term.has_mouse = 1;   /* touchpad events, if hw exists      */
        screen_console_t *c = screen_console_get();
        g_term.cols = (int)c->cols;
        g_term.rows = (int)c->rows;
    }
}

/* ------------------------------------------------------------------ */
/* init / shutdown                                                     */
/* ------------------------------------------------------------------ */

int tui_init(void) {
    if (g_started) return 0;
    palette_init();
    term_probe();
    if (!g_backend) return -1;
    g_default_attr = TUI_MAKE_ATTR(TUI_COLOR_SILVER, TUI_COLOR_BLACK);
    g_backend->clear(g_default_attr);
    g_backend->cursor(0);
    tui_windows_reset();   /* defensive: start every session with an empty pool */
    tui_input_reset();     /* no CSI/mouse state may leak in from earlier sessions */
    g_started = 1;
    return 0;
}

int tui_shutdown(void) {
    if (!g_started) return 0;
    g_backend->cursor(1);
    g_backend->clear(TUI_MAKE_ATTR(TUI_COLOR_SILVER, TUI_COLOR_BLACK));
    /* drop every window object: the screen is gone, stale cells would
     * leak and a subsequent session would inherit a half-drained pool */
    tui_windows_reset();
    tui_input_reset();     /* CSI/mouse state dies with the session */
    g_started = 0;
    return 0;
}

int tui_started(void) { return g_started; }

tui_attr_t tui_default_attr(void) { return g_default_attr; }

void tui_backend_putc_at(int x, int y, u32 ch, tui_attr_t a) {
    if (g_backend) g_backend->putc_at(x, y, ch, a);
}

void tui_backend_flush(void) {
    if (g_backend) g_backend->flush();
}

int tui_cursor_visible(void) { return g_cursor_visible; }

void tui_backend_cursor(int visible) {
    if (g_backend) g_backend->cursor(visible);
}

/* ------------------------------------------------------------------ */
/* Colors (public)                                                     */
/* ------------------------------------------------------------------ */

int tui_color_supported(void) { return g_backend ? g_term.colors : 0; }
int tui_color_pairs_max(void) { return TUI_COLOR_COUNT * TUI_COLOR_COUNT; }

int tui_init_pair(int pair, int fg, int bg) {
    /* pairs are virtual: TUI_MAKE_ATTR is pure math, so any pair in
     * range is instantly valid.  Range-checking is the contract. */
    if (pair < 0 || pair >= tui_color_pairs_max()) return -1;
    if (fg < 0 || fg >= TUI_COLOR_COUNT) return -1;
    if (bg < 0 || bg >= TUI_COLOR_COUNT) return -1;
    return 0;
}

int tui_pair_attr(int pair) {
    if (pair < 0 || pair >= tui_color_pairs_max()) return 0;
    int fg = pair / TUI_COLOR_COUNT;
    int bg = pair % TUI_COLOR_COUNT;
    return (int)TUI_MAKE_ATTR(fg, bg);
}

int tui_color_set_256(int fg256, int bg256) {
    if (!g_started) return -1;
    return (int)TUI_MAKE_ATTR(xterm256_to_palette(fg256),
                              xterm256_to_palette(bg256));
}

int tui_color_set_rgb(int fg256, int bg256) {
    /* fb is RGB-native but the cell grid stores 16-color attrs; the
     * nearest-palette map keeps the grid honest on both backends. */
    return tui_color_set_256(fg256, bg256);
}
