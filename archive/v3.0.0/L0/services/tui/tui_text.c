/* SPDX-License-Identifier: Apache-2.0
 * [3.0.0-WP04 G11c: revived verbatim from v2.2.0 tui/tui_text.c; include
 *  paths flattened to the WP-03 -I layout, shell_lang->disp_lang and
 *  shell_execute*->tui_exec* renamed (see tui_exec.h).]
 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10-tui
 * File: tui/tui_text.c
 * Purpose: Text output on the current window: addch/addstr/printw,
 *          attribute stack, color pair application, cursor control
 *          and the shared status line.
 */
#include "tui.h"
#include "screen_console.h"
#include "lib_string.h"
#include "encoding.h"
#include "render_cn.h"

/* The current window: tui_window_focus() switches it.  The single
 * focus matches the ncurses model where waddstr goes to a window; the
 * applications here drive one full-screen window plus panels. */
static tui_window_t *g_focus;

tui_window_t *tui_window_focus(void) { return g_focus; }

tui_window_t *tui_window_focus_set(tui_window_t *win) {
    tui_window_t *old = g_focus;
    g_focus = win;
    return old;
}

static tui_window_t *cur(void) { return g_focus; }

/* Cached one-line status window (created lazily by tui_status_line).
 * File scope so tui_text_reset() can drop the pointer when the window
 * pool is torn down - a stale pointer here would make the next
 * session paint into freed cells. */
static tui_window_t *g_status_win;

/* Drop every cached window pointer (pool teardown / session restart).
 * All tui_text print paths null-guard g_focus, so callers after a
 * reset are no-ops until a new window takes focus. */
void tui_text_reset(void) {
    g_focus = (void *)0;
    g_status_win = (void *)0;
}

int tui_move(int y, int x) {
    tui_window_t *w = cur();
    if (!w) return -1;
    if (y < 0 || x < 0 || y >= w->h || x >= w->w) return -1;
    w->curs_y = y;
    w->curs_x = x;
    return 0;
}

void tui_getyx(int *y, int *x) {
    tui_window_t *w = cur();
    if (!w) return;
    if (y) *y = w->curs_y;
    if (x) *x = w->curs_x;
}

int tui_set_attr(tui_attr_t attr) {
    tui_window_t *w = cur();
    if (!w) return -1;
    w->attr = attr;
    return 0;
}

int tui_attron(tui_attr_t attr) {
    tui_window_t *w = cur();
    if (!w) return -1;
    w->attr |= attr;
    return 0;
}

int tui_attroff(tui_attr_t attr) {
    tui_window_t *w = cur();
    if (!w) return -1;
    w->attr &= ~attr;
    return 0;
}

int tui_set_color(int fg, int bg) {
    return tui_set_attr((tui_attr_t)TUI_MAKE_ATTR(fg, bg) |
                        (cur() ? TUI_ATTR_FLAGS(cur()->attr) : 0));
}

int tui_addch(u32 ch) {
    tui_window_t *w = cur();
    if (!w || !w->cells) return -1;
    if (ch == '\n') {
        w->curs_x = 0;
        w->curs_y++;
        if (w->curs_y >= w->h) {
            if (w->scroll_enabled) {
                tui_window_scroll(w, 1);
                w->curs_y = w->h - 1;
            } else {
                w->curs_y = w->h - 1;
                return 0;
            }
        }
        return 0;
    }
    if (ch == '\r') { w->curs_x = 0; return 0; }
    if (ch == '\t') { w->curs_x = (w->curs_x + 8) & ~7; if (w->curs_x >= w->w) w->curs_x = w->w - 1; return 0; }
    /* WP-10-chinese_support: a CJK codepoint is TWO cells wide. The
     * glyph itself lands in the left cell; the right cell gets the
     * TUI_WIDE_CONT marker so renderers skip it. */
    u32 wide = (ch >= 0x100 && ch != TUI_WIDE_CONT) ? 1 : 0;
    if (w->curs_x + (wide ? 1u : 0u) >= (u32)w->w) {
        w->curs_x = 0;
        w->curs_y++;
        if (w->curs_y >= w->h) {
            if (w->scroll_enabled) tui_window_scroll(w, 1);
            w->curs_y = w->h - 1;
        }
    }
    w->cells[w->curs_y * w->w + w->curs_x].ch = ch;
    w->cells[w->curs_y * w->w + w->curs_x].attr = w->attr;
    w->curs_x++;
    if (wide && w->curs_x < w->w) {
        w->cells[w->curs_y * w->w + w->curs_x].ch = TUI_WIDE_CONT;
        w->cells[w->curs_y * w->w + w->curs_x].attr = w->attr;
        w->curs_x++;
    }
    return 0;
}

int tui_print(const char *s) {
    if (!s) return -1;
    const u8 *p = (const u8 *)s;
    while (*p) {
        /* WP-10-chinese_support: decode UTF-8 so a CJK codepoint lands
         * in ONE cell pair. Pure-ASCII bytes keep the exact old path
         * (single-byte addch, one cell each). */
        if (*p < 0x80) {
            if (tui_addch((u32)*p) != 0) return -1;
            p++;
            continue;
        }
        u32 cp, consumed;
        if (encoding_utf8_decode(p, 4, &cp, &consumed) == 0) {
            if (tui_addch(cp) != 0) return -1;
            p += consumed;
        } else {
            /* Invalid byte: draw a one-cell missing box (U+25A1). */
            if (tui_addch(0x25A1) != 0) return -1;
            p++;
        }
    }
    return 0;
}

int tui_print_at(int y, int x, const char *s) {
    if (tui_move(y, x) != 0) return -1;
    return tui_print(s);
}

/* minimal printf for printw: %s %d %u %x %c %% (kernel has no vfprintf;
 * u64_to_str from lib_string does the number formatting) */
#include "lib_string.h"

int tui_printw(const char *fmt, ...) {
    if (!fmt) return -1;
    /* walk varargs through built-in va_list (gnu11, freestanding) */
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') { tui_addch((u32)(u8)*p); continue; }
        p++;
        if (*p == 0) break;
        char num[24];
        if (*p == 's') {
            const char *s = __builtin_va_arg(ap, const char *);
            tui_print(s ? s : "(null)");
        } else if (*p == 'd') {
            long long v = (long long)__builtin_va_arg(ap, int);
            if (v < 0) { tui_addch('-'); v = -v; }
            u64_to_str((u64)v, num);
            tui_print(num);
        } else if (*p == 'u') {
            u64_to_str((u64)__builtin_va_arg(ap, unsigned int), num);
            tui_print(num);
        } else if (*p == 'x') {
            static const char hexd[] = "0123456789abcdef";
            u32 v = __builtin_va_arg(ap, unsigned int);
            int hi = 28;
            while (hi >= 0 && (v >> hi) == 0 && hi > 0) hi -= 4;
            for (; hi >= 0; hi -= 4)
                tui_addch((u32)hexd[(v >> hi) & 0xF]);
        } else if (*p == 'c') {
            tui_addch((u32)__builtin_va_arg(ap, int));
        } else if (*p == '%') {
            tui_addch('%');
        } else {
            tui_addch('%');
            tui_addch((u32)(u8)*p);
        }
    }
    __builtin_va_end(ap);
    return 0;
}

int tui_curs_set(int visibility) {
    /* visibility: 0 hidden, 1 normal (2 = very visible is clamped) */
    if (visibility < 0 || visibility > 2) return -1;
    extern void tui_backend_cursor(int visible);
    tui_backend_cursor(visibility ? 1 : 0);
    return 0;
}

void tui_status_line(const char *fmt, ...) {
    int cols, rows;
    tui_term_size(&cols, &rows);
    tui_window_t *saved = cur();
    if (!g_status_win) {
        g_status_win = tui_window_new(rows - 1, 0, 1, cols);
        if (!g_status_win) return;
    }
    tui_window_focus_set(g_status_win);
    tui_window_clear(g_status_win);
    tui_set_attr((tui_attr_t)TUI_MAKE_ATTR(TUI_COLOR_BLACK, TUI_COLOR_AQUA) |
                 TUI_ATTR_BOLD);
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    /* render into the one-line window through a bounded printw */
    char line[128];
    int n = 0;
    for (const char *p = fmt; *p && n < 120; p++) {
        if (*p != '%') { line[n++] = *p; continue; }
        p++;
        if (*p == 's') {
            const char *s = __builtin_va_arg(ap, const char *);
            while (s && *s && n < 120) line[n++] = *s++;
        } else if (*p == 'd' || *p == 'u') {
            u64 v = (u64)__builtin_va_arg(ap, int);
            if (*p == 'd' && (long)v < 0) v = (u64)(-(long)v);
            char num[24];
            u64_to_str(v, num);
            for (int i = 0; num[i] && n < 120; i++) line[n++] = num[i];
        } else if (*p == '%') {
            line[n++] = '%';
        }
    }
    __builtin_va_end(ap);
    line[n] = 0;
    tui_print(line);
    tui_window_refresh(g_status_win);
    tui_window_focus_set(saved);
}
