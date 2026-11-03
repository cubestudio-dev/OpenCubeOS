/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP06c-installer_gui
 * File: ocse/ocse_wzctl.c
 * Purpose: the wizard widget toolkit - see ocse_wzctl.h. Every byte of
 * mutable state sits in .ocsework (NOLOAD, loader-zeroed).
 */
#include "ocse_wzctl.h"
#include "ocse_i18n.h"
#include "screen_fb.h"
#include "screen_font.h"
#include "render_cn.h"
#include "encoding.h"
#include "driver_input_keyboard.h"
#include "driver_input_ps2mouse.h"
#include "driver_usb_hid.h"
#include "driver_input_i2chid.h"
#include "core_sched.h"
#include "lib_string.h"
#include <stdint.h>

/* ---- work zone: EVERYTHING below is in .ocsework ---- */
#define WZ_WORK __attribute__((section(".ocsework")))

typedef struct {
    u8  kind;
    u8  visible;
    u8  mask;               /* input: render '*' (password)         */
    i32 x, y, w, h;
    int text_key;           /* i18n key (drawn before raw)          */
    const char* raw;        /* dynamic text pointer (ASCII/UTF-8)   */
    int action;             /* button action code                   */
    int sel, nrows;         /* list                                 */
    int len, cur;           /* input                                */
    int pct;                /* progress                             */
    union {
        char rows[WZ_LIST_ROWS][WZ_ROW_LEN];
        char buf[WZ_INPUT_LEN];
    } u;
} wz_widget_t;

static WZ_WORK wz_widget_t g_w[WZ_MAX_WIDGETS];
static WZ_WORK int g_wn;                    /* widgets in use        */
static WZ_WORK int g_focus;                 /* focused widget id     */
static WZ_WORK int g_default;               /* Enter button id       */
static WZ_WORK int g_mx, g_my;              /* pointer               */
static WZ_WORK int g_btn, g_prev_btn;
static WZ_WORK int g_need_draw;

/* ---- palette (matches ocse_gui.c: no blue, warm greys) ---- */
#define COL_BG      0xFFF4F1EC
#define COL_BAR     0xFF26221C
#define COL_TEXT    0xFF26221C
#define COL_SUB     0xFF6B6459
#define COL_BTN     0xFF3E3830
#define COL_BTN_HOV 0xFF5A5248
#define COL_BOX     0xFFE8E2D8
#define COL_EDGE    0xFFB9B0A2
#define COL_SEL     0xFFD7CFBF
#define COL_OK      0xFF1E7B34
#define COL_WARN    0xFFB3261E
#define COL_CURSOR  0xFF161412
#define COL_FOCU    0xFF8A6D1F      /* focus ring: warm amber          */

/* ==================================================================
 * text renderer (UTF-8 -> 8x16 ASCII cell / 16x16 CJK cell), the
 * same glyph rules as ocse_gui.c's gui_text.
 * ================================================================== */
static void wz_text(u32 x, u32 y, u32 color, const char* s) {
    while (*s) {
        u32 cp = 0, used = 0;
        if (encoding_utf8_decode((const u8*)s, 6, &cp, &used) != 0
            || used == 0) {
            s++;
            continue;
        }
        const u8* g;
        u32 gw;
        if (cp < 0x80) {
            g = screen_font8x16[cp & 0xFF];
            gw = 8;
        } else {
            g = font_get_bitmap(cp);
            if (!g) {
                cp = '?';
                g = screen_font8x16['?'];
                gw = 8;
            } else {
                gw = 16;
            }
        }
        u32 stride = gw / 8;
        for (u32 r = 0; r < 16; r++) {
            const u8* row = g + r * stride;
            for (u32 b = 0; b < stride; b++) {
                u8 bits = row[b];
                for (u32 k = 0; k < 8; k++)
                    if (bits & (0x80u >> k))
                        screen_fb_put_pixel(x + b * 8 + k, y + r, color);
            }
        }
        s += used;
        x += gw + 1;
    }
}

/* visible text of a widget: the i18n key first, then the raw. */
static const char* w_text(const wz_widget_t* w) {
    if (w->raw) return w->raw;
    return ocse_tr(w->text_key);
}

static int focusable(const wz_widget_t* w) {
    return w->visible &&
           (w->kind == WZ_BUTTON || w->kind == WZ_LIST ||
            w->kind == WZ_INPUT);
}

/* ==================================================================
 * pool
 * ================================================================== */
void wz_reset(void) {
    for (int i = 0; i < WZ_MAX_WIDGETS; i++) {
        g_w[i].kind = 0;
        g_w[i].visible = 0;
        g_w[i].mask = 0;
        g_w[i].x = g_w[i].y = g_w[i].w = g_w[i].h = 0;
        g_w[i].text_key = 0;
        g_w[i].raw = 0;
        g_w[i].action = 0;
        g_w[i].sel = g_w[i].nrows = 0;
        g_w[i].len = g_w[i].cur = 0;
        g_w[i].pct = 0;
        for (int r = 0; r < WZ_LIST_ROWS; r++) g_w[i].u.rows[r][0] = 0;
    }
    g_wn = 0;
    g_focus = -1;
    g_default = -1;
    g_mx = 400; g_my = 300;
    g_btn = g_prev_btn = 0;
    g_need_draw = 1;
}

static int wz_alloc(void) {
    if (g_wn >= WZ_MAX_WIDGETS) return -1;
    return g_wn++;
}

int wz_add_label(int x, int y, int text_key, const char* raw) {
    int id = wz_alloc();
    if (id < 0) return -1;
    g_w[id].kind = WZ_LABEL;
    g_w[id].visible = 1;
    g_w[id].x = x; g_w[id].y = y; g_w[id].w = 0; g_w[id].h = 18;
    g_w[id].text_key = text_key;
    g_w[id].raw = raw;
    return id;
}

int wz_add_button(int x, int y, int w, int h,
                  int text_key, const char* raw, int action) {
    int id = wz_alloc();
    if (id < 0) return -1;
    g_w[id].kind = WZ_BUTTON;
    g_w[id].visible = 1;
    g_w[id].x = x; g_w[id].y = y; g_w[id].w = w; g_w[id].h = h;
    g_w[id].text_key = text_key;
    g_w[id].raw = raw;
    g_w[id].action = action;
    return id;
}

int wz_add_list(int x, int y, int w, int h) {
    int id = wz_alloc();
    if (id < 0) return -1;
    g_w[id].kind = WZ_LIST;
    g_w[id].visible = 1;
    g_w[id].x = x; g_w[id].y = y; g_w[id].w = w; g_w[id].h = h;
    return id;
}

int wz_add_input(int x, int y, int w, int text_key, const char* raw) {
    int id = wz_alloc();
    if (id < 0) return -1;
    g_w[id].kind = WZ_INPUT;
    g_w[id].visible = 1;
    g_w[id].x = x; g_w[id].y = y; g_w[id].w = w; g_w[id].h = 26;
    g_w[id].text_key = text_key;      /* caption label key */
    g_w[id].raw = raw;
    return id;
}

int wz_add_progress(int x, int y, int w, int h) {
    int id = wz_alloc();
    if (id < 0) return -1;
    g_w[id].kind = WZ_PROGRESS;
    g_w[id].visible = 1;
    g_w[id].x = x; g_w[id].y = y; g_w[id].w = w; g_w[id].h = h;
    return id;
}

/* ---- setters ---- */
void wz_set_raw(int id, const char* text) {
    if (id < 0 || id >= g_wn) return;
    g_w[id].raw = text;
}

void wz_set_visible(int id, int vis) {
    if (id < 0 || id >= g_wn) return;
    g_w[id].visible = (u8)(vis ? 1 : 0);
}

void wz_set_progress(int id, int pct) {
    if (id < 0 || id >= g_wn) return;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    g_w[id].pct = pct;
}

void wz_list_clear(int id) {
    if (id < 0 || id >= g_wn) return;
    g_w[id].nrows = 0;
    g_w[id].sel = 0;
    for (int r = 0; r < WZ_LIST_ROWS; r++) g_w[id].u.rows[r][0] = 0;
}

void wz_list_add(int id, const char* row) {
    if (id < 0 || id >= g_wn) return;
    if (g_w[id].nrows >= WZ_LIST_ROWS) return;
    int r = g_w[id].nrows++;
    strncpy(g_w[id].u.rows[r], row, WZ_ROW_LEN - 1);
    g_w[id].u.rows[r][WZ_ROW_LEN - 1] = 0;
}

void wz_list_set_sel(int id, int row) {
    if (id < 0 || id >= g_wn) return;
    if (row < 0) row = 0;
    if (row >= g_w[id].nrows) row = g_w[id].nrows - 1;
    if (row < 0) row = 0;
    g_w[id].sel = row;
}

int wz_list_sel(int id) {
    if (id < 0 || id >= g_wn) return -1;
    return g_w[id].nrows ? g_w[id].sel : -1;
}

const char* wz_list_row(int id, int row) {
    if (id < 0 || id >= g_wn) return 0;
    if (row < 0 || row >= g_w[id].nrows) return 0;
    return g_w[id].u.rows[row];
}

int wz_list_count(int id) {
    if (id < 0 || id >= g_wn) return 0;
    return g_w[id].nrows;
}

const char* wz_input_text(int id) {
    if (id < 0 || id >= g_wn) return 0;
    return g_w[id].u.buf;
}

void wz_input_set(int id, const char* text) {
    if (id < 0 || id >= g_wn) return;
    strncpy(g_w[id].u.buf, text ? text : "", WZ_INPUT_LEN - 1);
    g_w[id].u.buf[WZ_INPUT_LEN - 1] = 0;
    g_w[id].len = (int)strlen(g_w[id].u.buf);
    g_w[id].cur = g_w[id].len;
}

void wz_input_mask(int id, int on) {
    if (id < 0 || id >= g_wn) return;
    g_w[id].mask = (u8)(on ? 1 : 0);
}

void wz_set_default(int id) {
    if (id < 0 || id >= g_wn) return;
    if (g_w[id].kind != WZ_BUTTON) return;
    g_default = id;
}

int wz_focus_id(void) {
    return g_focus;
}

void wz_focus_set(int id) {
    if (id < 0 || id >= g_wn) return;
    if (!focusable(&g_w[id])) return;
    g_focus = id;
}

void wz_focus_next(void) {
    if (g_wn == 0) return;
    int n = 0;
    int i = g_focus;
    while (n < WZ_MAX_WIDGETS) {
        i = (i + 1) % g_wn;
        if (focusable(&g_w[i])) { g_focus = i; return; }
        n++;
    }
}

/* ==================================================================
 * drawing
 * ================================================================== */
/* frame via 4 fills (the vendored FB API has no outline primitive). */
static void wz_frame(u32 x, u32 y, u32 w, u32 h, u32 color) {
    screen_fb_fill_rect(x, y, w, 2, color);
    screen_fb_fill_rect(x, y + h - 2, w, 2, color);
    screen_fb_fill_rect(x, y, 2, h, color);
    screen_fb_fill_rect(x + w - 2, y, 2, h, color);
}

static void draw_pointer(void) {
    screen_fb_fill_rect((u32)g_mx, (u32)g_my, 3, 14, COL_CURSOR);
    screen_fb_fill_rect((u32)g_mx + 3, (u32)g_my + 2, 3, 9, COL_CURSOR);
    screen_fb_fill_rect((u32)g_mx + 6, (u32)g_my + 4, 2, 5, COL_CURSOR);
}

static void draw_widget(const wz_widget_t* w, int id) {
    if (!w->visible) return;
    switch (w->kind) {
    case WZ_LABEL:
        wz_text((u32)w->x, (u32)w->y, COL_TEXT, w_text(w));
        break;
    case WZ_BUTTON: {
        int hover = (u32)g_mx >= (u32)w->x &&
                    (u32)g_mx < (u32)(w->x + w->w) &&
                    (u32)g_my >= (u32)w->y &&
                    (u32)g_my < (u32)(w->y + w->h);
        screen_fb_fill_rect((u32)w->x, (u32)w->y, (u32)w->w, (u32)w->h,
                            hover ? COL_BTN_HOV : COL_BTN);
        if (id == g_focus)
            wz_frame((u32)w->x - 2, (u32)w->y - 2,
                     (u32)w->w + 4, (u32)w->h + 4, COL_FOCU);
        if (id == g_default)
            wz_frame((u32)w->x - 1, (u32)w->y - 1,
                     (u32)w->w + 2, (u32)w->h + 2, COL_OK);
        /* center the label */
        const char* t = w_text(w);
        u32 tw = 0;
        for (const char* p = t; *p; ) {
            u32 cp, used;
            if (encoding_utf8_decode((const u8*)p, 6, &cp, &used) != 0
                || used == 0) { p++; tw += 9; continue; }
            tw += (cp < 0x80 ? 8 : 16) + 1;
            p += used;
        }
        u32 tx = (u32)w->x + ((u32)w->w > tw ? ((u32)w->w - tw) / 2 : 0);
        wz_text(tx, (u32)w->y + (u32)w->h / 2 - 8, 0xFFF4F1EC, t);
        break;
    }
    case WZ_LIST: {
        screen_fb_fill_rect((u32)w->x, (u32)w->y, (u32)w->w, (u32)w->h,
                            COL_BOX);
        wz_frame((u32)w->x, (u32)w->y, (u32)w->w, (u32)w->h,
                 id == g_focus ? COL_FOCU : COL_EDGE);
        for (int r = 0; r < w->nrows; r++) {
            u32 ry = (u32)w->y + 4 + (u32)r * 22;
            if (ry + 18 > (u32)(w->y + w->h)) break;
            if (r == w->sel) {
                screen_fb_fill_rect((u32)w->x + 2, (u32)ry - 2,
                                    (u32)w->w - 4, 22, COL_SEL);
            }
            wz_text((u32)w->x + 8, ry, COL_TEXT, w->u.rows[r]);
        }
        break;
    }
    case WZ_INPUT: {
        if (w->text_key >= 0)
            wz_text((u32)w->x, (u32)w->y, COL_SUB, w_text(w));
        u32 by = (u32)w->y + 20;
        screen_fb_fill_rect((u32)w->x, by, (u32)w->w, 24, COL_BOX);
        wz_frame((u32)w->x, by, (u32)w->w, 24,
                 id == g_focus ? COL_FOCU : COL_EDGE);
        /* masked or plain */
        char shown[WZ_INPUT_LEN * 3 + 1];
        if (w->mask) {
            int n = (int)strlen(w->u.buf);
            if (n > WZ_INPUT_LEN) n = WZ_INPUT_LEN;
            for (int i = 0; i < n; i++) shown[i] = '*';
            shown[n] = 0;
        } else {
            strncpy(shown, w->u.buf, sizeof(shown) - 1);
            shown[sizeof(shown) - 1] = 0;
        }
        wz_text((u32)w->x + 6, by + 4, COL_TEXT, shown);
        if (id == g_focus) {
            /* caret at the cursor cell */
            u32 cx = (u32)w->x + 6;
            for (int i = 0; i < w->cur && i < WZ_INPUT_LEN; i++)
                cx += 9;
            screen_fb_fill_rect(cx, by + 4, 2, 16, COL_CURSOR);
        }
        break;
    }
    case WZ_PROGRESS: {
        screen_fb_fill_rect((u32)w->x, (u32)w->y, (u32)w->w, (u32)w->h,
                            COL_BOX);
        wz_frame((u32)w->x, (u32)w->y, (u32)w->w, (u32)w->h, COL_EDGE);
        if (w->pct > 0) {
            u32 fw = ((u32)w->w - 4) * (u32)w->pct / 100;
            screen_fb_fill_rect((u32)w->x + 2, (u32)w->y + 2,
                                fw, (u32)w->h - 4, COL_OK);
        }
        break;
    }
    default:
        break;
    }
}

void wz_draw(void) {
    const screen_fb_info_t* fb = screen_fb_get_info();
    screen_fb_fill_rect(0, 0, fb->width, fb->height, COL_BG);
    screen_fb_fill_rect(0, 0, fb->width, 48, COL_BAR);
    wz_text(24, 14, 0xFFF4F1EC, ocse_tr(OCSE_W_TITLE));
    for (int i = 0; i < g_wn; i++)
        draw_widget(&g_w[i], i);
    draw_pointer();
}

void wz_repaint(void) {
    wz_draw();
    g_need_draw = 0;
}

void wz_touch(void) {
    g_need_draw = 1;
}

/* ==================================================================
 * hit testing + pointer pump
 * ================================================================== */
static int hit_test(int wx, int wy) {
    for (int i = g_wn - 1; i >= 0; i--) {
        const wz_widget_t* w = &g_w[i];
        if (!w->visible || w->kind == WZ_LABEL || w->kind == WZ_PROGRESS)
            continue;
        /* input: the caption area focuses too */
        if (w->kind == WZ_INPUT) {
            if (wx >= w->x && wx < w->x + w->w &&
                wy >= w->y && wy < w->y + 44)
                return i;
            continue;
        }
        if (wx >= w->x && wx < w->x + w->w &&
            wy >= w->y && wy < w->y + w->h)
            return i;
    }
    return -1;
}

static void clamp_pointer(void) {
    const screen_fb_info_t* fb = screen_fb_get_info();
    if (fb->width == 0 || fb->height == 0) return;
    if (g_mx < 0) g_mx = 0;
    if (g_my < 0) g_my = 0;
    if ((u32)g_mx >= fb->width)  g_mx = (int)fb->width - 1;
    if ((u32)g_my >= fb->height) g_my = (int)fb->height - 1;
}

/* Click dispatch: buttons return their action; lists select the row;
 * inputs take the focus. */
static int dispatch_click(int id) {
    wz_widget_t* w = &g_w[id];
    if (w->kind == WZ_BUTTON)
        return w->action;
    if (w->kind == WZ_LIST) {
        int row = (g_my - w->y - 4) / 22;
        if (row < 0) row = 0;
        if (row >= w->nrows) row = w->nrows - 1;
        if (row >= 0) w->sel = row;
        g_focus = id;
        g_need_draw = 1;
        return WZ_ACT_NONE;
    }
    if (w->kind == WZ_INPUT) {
        g_focus = id;
        g_need_draw = 1;
        return WZ_ACT_NONE;
    }
    return WZ_ACT_NONE;
}

int wz_poll_click(void) {
    /* PS/2 aux mouse */
    ps2mouse_event_t ev;
    while (driver_input_ps2mouse_read_event(&ev)) {
        g_mx += ev.dx;
        g_my -= ev.dy;              /* PS/2 y grows upward */
        if (ev.buttons & 1) g_btn |= 1; else g_btn &= ~1;
        clamp_pointer();
        g_need_draw = 1;
    }
    /* USB mouse (returns 1 = event; see the T4 battery lesson) */
    driver_usb_mouse_event_t ue;
    while (driver_usb_mouse_read_event(&ue)) {
        g_mx += ue.dx;
        g_my += ue.dy;
        if (ue.buttons & 1) g_btn |= 1; else g_btn &= ~1;
        clamp_pointer();
        g_need_draw = 1;
    }
    /* I2C touchpad (v1.0.0 contract: 0 = event, -1 = empty) */
    touchpad_event_t te;
    while (driver_input_touchpad_get_event(&te) == 0) {
        if (te.type == TPE_MOVE) {
            g_mx += te.dx;
            g_my += te.dy;
            clamp_pointer();
            g_need_draw = 1;
        } else if (te.type == TPE_TAP) {
            g_btn |= 1;
            g_need_draw = 1;
        }
    }
    if (g_btn && !g_prev_btn) {
        g_prev_btn = g_btn;
        int id = hit_test(g_mx, g_my);
        if (id >= 0) {
            int act = dispatch_click(id);
            if (act != WZ_ACT_NONE) { g_btn = 0; g_prev_btn = 0; }
            return act;
        }
    }
    g_prev_btn = g_btn;
    return WZ_ACT_NONE;
}

/* ==================================================================
 * keyboard
 * ================================================================== */
int wz_handle_key(int key) {
    if (key == OC_KEY_TAB) {
        wz_focus_next();
        g_need_draw = 1;
        return WZ_ACT_NONE;
    }
    if (key == '\r' || key == '\n' || key == OC_KEY_ENTER) {
        if (g_default >= 0 && g_w[g_default].visible)
            return g_w[g_default].action;
        return WZ_ACT_NONE;
    }
    if (g_focus < 0 || g_focus >= g_wn) return WZ_ACT_NONE;
    wz_widget_t* w = &g_w[g_focus];
    if (w->kind == WZ_LIST) {
        if (key == OC_KEY_UP && w->sel > 0) {
            w->sel--; g_need_draw = 1;
        } else if (key == OC_KEY_DOWN && w->sel + 1 < w->nrows) {
            w->sel++; g_need_draw = 1;
        }
        return WZ_ACT_NONE;
    }
    if (w->kind == WZ_INPUT) {
        if (key == OC_KEY_BACKSPACE) {
            if (w->cur > 0) {
                for (int i = w->cur - 1; i < w->len; i++)
                    w->u.buf[i] = w->u.buf[i + 1];
                w->cur--; w->len--;
                g_need_draw = 1;
            }
            return WZ_ACT_NONE;
        }
        if (key == OC_KEY_LEFT && w->cur > 0) { w->cur--; return WZ_ACT_NONE; }
        if (key == OC_KEY_RIGHT && w->cur < w->len) { w->cur++; return WZ_ACT_NONE; }
        if (key >= 0x20 && key < 0x7F && w->len + 1 < WZ_INPUT_LEN) {
            for (int i = w->len; i > w->cur; i--)
                w->u.buf[i] = w->u.buf[i - 1];
            w->u.buf[w->cur++] = (char)key;
            w->len++;
            w->u.buf[w->len] = 0;
            g_need_draw = 1;
        }
        return WZ_ACT_NONE;
    }
    return WZ_ACT_NONE;
}
