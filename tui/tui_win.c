/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10-tui
 * File: tui/tui_win.c
 * Purpose: Window system: creation, move/resize, borders, scrolling,
 *          subwindows and the panel-ordered repaint.  A window owns a
 *          w*h cell buffer; refresh paints it (clipped to the
 *          terminal and to the parent window for subwindows).
 */
#include "tui.h"
#include "../core/lib/lib_string.h"
#include "../core/mem/mem_heap.h"

#define TUI_MAX_WINDOWS 16

static tui_window_t g_windows[TUI_MAX_WINDOWS];
static int g_window_count;

const tui_border_t TUI_BORDER_SINGLE = { 0x2502, 0x2502, 0x2500, 0x2500,
                                         0x250C, 0x2510, 0x2514, 0x2518 };
const tui_border_t TUI_BORDER_DOUBLE = { 0x2551, 0x2551, 0x2550, 0x2550,
                                         0x2554, 0x2557, 0x255A, 0x255D };
const tui_border_t TUI_BORDER_ASCII  = { '|', '|', '-', '-', '+', '+', '+', '+' };

void tui_getbegyx(tui_window_t *win, int *y, int *x) {
    if (!win) return;
    if (y) *y = win->y;
    if (x) *x = win->x;
}

void tui_getmaxyx(tui_window_t *win, int *y, int *x) {
    if (!win) return;
    if (y) *y = win->h;
    if (x) *x = win->w;
}

tui_window_t *tui_window_get(int id) {
    if (id < 0 || id >= g_window_count) return (void *)0;
    return &g_windows[id];
}

int tui_window_count(void) { return g_window_count; }

static void win_fill(tui_window_t *w) {
    for (int i = 0; i < w->w * w->h; i++) {
        w->cells[i].ch = (u32)(w->bkgd >> 16);   /* bg fill char */
        w->cells[i].attr = (tui_attr_t)(w->bkgd & 0xFFFF);
    }
}

tui_window_t *tui_window_new(int y, int x, int h, int w) {
    if (!tui_started()) return (void *)0;
    if (h <= 0 || w <= 0) return (void *)0;
    int cols, rows;
    tui_term_size(&cols, &rows);
    if (x < 0 || y < 0 || x + w > cols || y + h > rows) return (void *)0;
    /* Reuse a freed slot first.  tui_window_free marks the slot dead
     * (id = -1) without shrinking the table; without reuse the 16-slot
     * pool drained after a handful of page visits and every later
     * tui_window_new returned NULL - apps then dropped back to the
     * shell and the TUI could not be started again.  Reuse keeps
     * existing window objects at stable addresses (no compacting). */
    int slot = -1;
    for (int i = 0; i < g_window_count; i++) {
        if (g_windows[i].id < 0) { slot = i; break; }
    }
    if (slot < 0) {
        if (g_window_count >= TUI_MAX_WINDOWS) return (void *)0;
        slot = g_window_count;
        g_window_count++;
    }
    tui_window_t *win = &g_windows[slot];
    memset(win, 0, sizeof(*win));
    win->id = slot;
    win->x = x; win->y = y; win->w = w; win->h = h;
    win->visible = 1;
    win->attr = tui_default_attr();
    win->bkgd = ((u32)' ' << 16) | (u32)tui_default_attr();
    win->cells = (tui_cell_t *)kmalloc((u64)w * (u64)h * sizeof(tui_cell_t));
    if (!win->cells) {
        win->id = -1;
        if (slot == g_window_count - 1) g_window_count--;
        return (void *)0;
    }
    win_fill(win);
    return win;
}

void tui_window_free(tui_window_t *win) {
    if (!win) return;
    /* panels holding this window are dropped by their own code */
    if (win->cells) kfree(win->cells);
    win->cells = (void *)0;
    win->id = -1;
}

/* compact the window table after a free (ids are indexes) */
void tui_window_compact(void) {
    int out = 0;
    for (int i = 0; i < g_window_count; i++) {
        if (g_windows[i].id >= 0) {
            if (out != i) g_windows[out] = g_windows[i];
            g_windows[out].id = out;
            out++;
        }
    }
    g_window_count = out;
}

/* Release every window (shutdown / defensive init): frees the cell
 * buffers and empties the pool so the next session starts clean. */
void tui_windows_reset(void) {
    for (int i = 0; i < g_window_count; i++) {
        if (g_windows[i].cells) {
            kfree(g_windows[i].cells);
            g_windows[i].cells = (void *)0;
        }
        g_windows[i].id = -1;
    }
    g_window_count = 0;
    tui_text_reset();   /* the focus/status caches point into the pool */
    tui_panels_reset(); /* fix2: zombie panels survived sessions (their
                         * windows were freed under them) */
}

int tui_window_move(tui_window_t *win, int y, int x) {
    if (!win || !tui_started()) return -1;
    int cols, rows;
    tui_term_size(&cols, &rows);
    if (x < 0 || y < 0 || x + win->w > cols || y + win->h > rows) return -1;
    win->x = x;
    win->y = y;
    return 0;
}

int tui_window_resize(tui_window_t *win, int h, int w) {
    if (!win || !tui_started()) return -1;
    int cols, rows;
    tui_term_size(&cols, &rows);
    if (h <= 0 || w <= 0) return -1;
    if (win->x + w > cols || win->y + h > rows) return -1;
    tui_cell_t *nc = (tui_cell_t *)kmalloc((u64)w * (u64)h * sizeof(tui_cell_t));
    if (!nc) return -1;
    for (int yy = 0; yy < h; yy++)
        for (int xx = 0; xx < w; xx++) {
            if (yy < win->h && xx < win->w) nc[yy * w + xx] = win->cells[yy * win->w + xx];
            else {
                nc[yy * w + xx].ch = (u32)(win->bkgd >> 16);
                nc[yy * w + xx].attr = (tui_attr_t)(win->bkgd & 0xFFFF);
            }
        }
    kfree(win->cells);
    win->cells = nc;
    win->w = w;
    win->h = h;
    if (win->curs_x >= w) win->curs_x = w - 1;
    if (win->curs_y >= h) win->curs_y = h - 1;
    return 0;
}

int tui_window_clear(tui_window_t *win) {
    if (!win) return -1;
    win_fill(win);
    win->curs_x = 0;
    win->curs_y = 0;
    return 0;
}

int tui_window_title(tui_window_t *win, const char *title) {
    if (!win || !title) return -1;
    int i = 0;
    for (; i < 39 && title[i]; i++) win->title[i] = title[i];
    win->title[i] = 0;
    return 0;
}

int tui_window_border(tui_window_t *win, const tui_border_t *b) {
    if (!win) return -1;
    if (!b) { win->bordered = !win->bordered; return win->bordered; }
    /* write the frame into the cell buffer */
    tui_attr_t a = win->attr;
    for (int xx = 0; xx < win->w; xx++) {
        win->cells[0 * win->w + xx] = (tui_cell_t){ b->ts, a };
        win->cells[(win->h - 1) * win->w + xx] = (tui_cell_t){ b->bs, a };
    }
    for (int yy = 0; yy < win->h; yy++) {
        win->cells[yy * win->w] = (tui_cell_t){ b->ls, a };
        win->cells[yy * win->w + win->w - 1] = (tui_cell_t){ b->rs, a };
    }
    win->cells[0] = (tui_cell_t){ b->tl, a };
    win->cells[win->w - 1] = (tui_cell_t){ b->tr, a };
    win->cells[(win->h - 1) * win->w] = (tui_cell_t){ b->bl, a };
    win->cells[win->h * win->w - 1] = (tui_cell_t){ b->br, a };
    /* title centered on the top edge */
    if (win->title[0]) {
        int tlen = 0;
        while (win->title[tlen]) tlen++;
        int start = (win->w - tlen) / 2;
        if (start < 1) start = 1;
        for (int i = 0; i < tlen && start + 1 + i < win->w - 1; i++)
            win->cells[start + i] = (tui_cell_t){ (u32)win->title[i], a };
    }
    win->bordered = 1;
    return 0;
}

int tui_window_scrollok(tui_window_t *win, int on) {
    if (!win) return -1;
    win->scroll_enabled = on ? 1 : 0;
    return 0;
}

int tui_window_scroll(tui_window_t *win, int lines) {
    if (!win || lines <= 0) return -1;
    if (!win->scroll_enabled) return -1;
    for (int n = 0; n < lines; n++) {
        for (int yy = 0; yy < win->h - 1; yy++)
            memcpy(&win->cells[yy * win->w], &win->cells[(yy + 1) * win->w],
                       (usize)win->w * sizeof(tui_cell_t));
        for (int xx = 0; xx < win->w; xx++) {
            win->cells[(win->h - 1) * win->w + xx].ch = (u32)(win->bkgd >> 16);
            win->cells[(win->h - 1) * win->w + xx].attr = (tui_attr_t)(win->bkgd & 0xFFFF);
        }
    }
    return 0;
}

tui_window_t *tui_window_subwin(tui_window_t *parent, int y, int x, int h, int w) {
    if (!parent) return (void *)0;
    if (y < 0 || x < 0 || y + h > parent->h || x + w > parent->w) return (void *)0;
    tui_window_t *sw = tui_window_new(parent->y + y, parent->x + x, h, w);
    if (sw) sw->parent = parent;
    return sw;
}

int tui_window_touch(tui_window_t *win) {
    if (!win) return -1;
    /* full repaint of this window happens on the next refresh */
    return tui_window_refresh(win);
}

int tui_window_refresh(tui_window_t *win) {
    if (!win || !win->visible || !win->cells) return -1;
    int cols, rows;
    tui_term_size(&cols, &rows);
    int py = 0, px = 0, ph = rows, pw = cols;
    if (win->parent && win->parent->cells) {
        py = win->parent->y;
        px = win->parent->x;
        ph = win->parent->h;
        pw = win->parent->w;
    }
    for (int yy = 0; yy < win->h; yy++) {
        int sy = win->y + yy;
        if (sy < py || sy >= py + ph) continue;
        for (int xx = 0; xx < win->w; xx++) {
            int sx = win->x + xx;
            if (sx < px || sx >= px + pw) continue;
            tui_cell_t *c = &win->cells[yy * win->w + xx];
            tui_backend_putc_at(sx, sy, c->ch, c->attr);
        }
    }
    tui_backend_flush();
    return 0;
}
