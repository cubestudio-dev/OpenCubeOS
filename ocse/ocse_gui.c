/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP06a-ocse
 * File: ocse/ocse_gui.c
 * Purpose: the OCSE framebuffer front-end: four hardware-check rows,
 * the disk identification panel and three buttons (command-line
 * install / language / re-check). Input: USB mouse + I2C touchpad
 * (pointer, tap = click); the Ctrl+Alt+F1/F2 hotkeys are handled by
 * ocse_main.c and checked between interactions.
 */
#include "ocse_gui.h"
#include "ocse_i18n.h"
#include "ocse_detect.h"
#include "ocse_disk.h"
#include "screen_fb.h"
#include "screen_font.h"
#include "render_cn.h"
#include "encoding.h"
#include "driver_usb_hid.h"
#include "driver_input_i2chid.h"
#include "driver_input_keyboard.h"
#include "core_sched.h"
#include "lib_string.h"
#include <stdint.h>

/* Hotkey request flag, set by the keyboard handler in ocse_main.c. */
extern int g_ocse_hotkey_req;

static int g_mx = 400, g_my = 300;      /* pointer position          */
static int g_btn = 0, g_prev_btn = 0;   /* current/previous buttons */
static int g_need_draw = 1;

/* Palette (avoiding blue per house style). */
#define COL_BG      0xFFF4F1EC
#define COL_BAR     0xFF26221C
#define COL_PANEL   0xFFE8E2D8
#define COL_TEXT    0xFF26221C
#define COL_SUB     0xFF6B6459
#define COL_OK      0xFF1E7B34
#define COL_BAD     0xFFB3261E
#define COL_BTN     0xFF3E3830
#define COL_BTN_HOV 0xFF5A5248
#define COL_CURSOR  0xFF161412
#define COL_WARN    0xFFB3261E

typedef struct { u32 x, y, w, h; int act; int key; } ocse_button_t;

#define OCSE_NBTN 3
static const ocse_button_t g_buttons[OCSE_NBTN] = {
    {  40, 516, 300, 52, OCSE_ACT_GOTO_CL,     OCSE_T_BTN_CLI   },
    { 360, 516, 190, 52, OCSE_ACT_RECHECK,     OCSE_T_BTN_CHECK  },
    { 570, 516, 190, 52, OCSE_ACT_TOGGLE_LANG, OCSE_T_BTN_LANG   },
};

/* ---- text renderer: UTF-8 -> ASCII cell (8x16) or CJK cell (16x16) */
static void gui_text(u32 x, u32 y, u32 color, const char* s) {
    while (*s) {
        u32 cp = 0, used = 0;
        if (encoding_utf8_decode((const u8*)s, 6, &cp, &used) != 0 || used == 0) {
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

/* Draw one check row. */
static void draw_checks(void) {
    gui_text(48, 132, COL_TEXT, ocse_tr(OCSE_T_HW_HEAD));
    for (int i = 0; i < OCSE_CHECKS; i++) {
        u32 y = 168 + (u32)i * 44;
        u32 col = g_ocse_checks[i].pass ? COL_OK : COL_BAD;
        screen_fb_fill_rect(48, y, 28, 28, col);
        /* OK cross / bad dash, 2px */
        if (g_ocse_checks[i].pass) {
            screen_fb_fill_rect(54, y + 12, 16, 4, 0xFFFFFFFF);
            screen_fb_fill_rect(61, y + 5, 4, 18, 0xFFFFFFFF);
        } else {
            screen_fb_fill_rect(54, y + 12, 16, 4, 0xFFFFFFFF);
        }
        gui_text(92, y + 6, COL_TEXT, ocse_tr(OCSE_T_CHECK_CPU + i));
        gui_text(340, y + 6, col, ocse_tr(g_ocse_checks[i].pass
                                              ? OCSE_T_PASS : OCSE_T_FAIL));
        gui_text(430, y + 6, COL_SUB, g_ocse_checks[i].detail);
    }
}

/* Draw the disk panel. */
static void draw_disks(void) {
    u32 y = 356;
    gui_text(48, y, COL_TEXT, ocse_tr(OCSE_T_DISK_HEAD));
    y += 36;
    if (g_ocse_ndisks == 0) {
        gui_text(48, y, COL_WARN, ocse_tr(OCSE_T_NO_DISK));
        return;
    }
    for (int d = 0; d < g_ocse_ndisks && d < 4; d++) {
        ocse_disk_t* dk = &g_ocse_disks[d];
        char line[96], n[14];
        strcpy(line, dk->name);
        strcat(line, dk->table_kind == 2 ? " [GPT]" :
                     dk->table_kind == 1 ? " [MBR]" : " [no table]");
        strcat(line, " ");
        u64_to_str(dk->mb, n); strcat(line, n); strcat(line, " MiB:");
        int np = 0;
        for (int p = 0; p < dk->nparts && np < 4; p++) {
            if (np) strcat(line, ",");
            strcat(line, ocse_fs_name(dk->parts[p].fs));
            np++;
        }
        if (dk->space_low) {
            strcat(line, "  (");
            strcat(line, ocse_tr(OCSE_T_SPACE_LOW));
            strcat(line, ")");
        }
        gui_text(48, y, dk->space_low ? COL_WARN : COL_TEXT, line);
        y += 28;
    }
}

static int hit_test(u32 x, u32 y) {
    for (int i = 0; i < OCSE_NBTN; i++) {
        const ocse_button_t* b = &g_buttons[i];
        if (x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h)
            return b->act;
    }
    return OCSE_ACT_NONE;
}

static void draw_pointer(void) {
    /* block arrow: shaft + head */
    screen_fb_fill_rect((u32)g_mx, (u32)g_my, 3, 14, COL_CURSOR);
    screen_fb_fill_rect((u32)g_mx + 3, (u32)g_my + 2, 3, 9, COL_CURSOR);
    screen_fb_fill_rect((u32)g_mx + 6, (u32)g_my + 4, 2, 5, COL_CURSOR);
}

void ocse_gui_draw(void) {
    const screen_fb_info_t* fb = screen_fb_get_info();
    screen_fb_fill_rect(0, 0, fb->width, fb->height, COL_BG);
    screen_fb_fill_rect(0, 0, fb->width, 64, COL_BAR);
    gui_text(40, 12, 0xFFF4F1EC, ocse_tr(OCSE_T_TITLE));
    gui_text(40, 34, 0xFFB8B0A4, ocse_tr(OCSE_T_SUBTITLE));
    gui_text(40, 76, COL_SUB, ocse_tr(OCSE_T_LANG_NOW));
    draw_checks();
    screen_fb_fill_rect(40, 340, fb->width - 80, 2, COL_PANEL);
    draw_disks();
    for (int i = 0; i < OCSE_NBTN; i++) {
        const ocse_button_t* b = &g_buttons[i];
        int hover = hit_test((u32)g_mx, (u32)g_my) == b->act;
        screen_fb_fill_rect(b->x, b->y, b->w, b->h,
                            hover ? COL_BTN_HOV : COL_BTN);
        screen_fb_fill_rect(b->x, b->y, b->w, 2, 0x00000000);
        gui_text(b->x + 16, b->y + 18, 0xFFF4F1EC,
                 i == 2 ? ocse_lang_name(!ocse_i18n_lang())
                        : ocse_tr(b->key));
    }
    gui_text(40, 580, COL_SUB, ocse_tr(OCSE_T_GUI_HINT));
    draw_pointer();
}

void ocse_gui_init(void) {
    g_mx = 400; g_my = 300; g_btn = g_prev_btn = 0;
    g_need_draw = 1;
    ocse_gui_draw();
}

/* Poll pointer inputs once; returns OCSE_ACT_* on click. */
static int poll_pointer(void) {
    driver_usb_mouse_event_t ev;
    while (driver_usb_mouse_read_event(&ev)) {
        g_mx += ev.dx;
        g_my += ev.dy;
        if (ev.buttons & 1) g_btn |= 1; else g_btn &= ~1;
        g_need_draw = 1;
    }
    touchpad_event_t te;
    while (driver_input_touchpad_get_event(&te)) {
        if (te.type == TPE_MOVE) {
            g_mx += te.dx;
            g_my += te.dy;
            g_need_draw = 1;
        } else if (te.type == TPE_TAP) {
            g_btn |= 1;             /* tap = press; released next poll */
            g_need_draw = 1;
        }
    }
    if (g_btn && !g_prev_btn) {     /* rising edge = click */
        g_prev_btn = g_btn;
        return hit_test((u32)g_mx, (u32)g_my);
    }
    g_prev_btn = g_btn;
    return OCSE_ACT_NONE;
}

int ocse_gui_loop(void) {
    for (;;) {
        /* the hotkey (Ctrl+Alt+F1/F2) handler in ocse_main.c sets the
         * request; hand control back so the mode switch happens. */
        if (g_ocse_hotkey_req)
            return OCSE_ACT_NONE;
        while (driver_input_keyboard_has_key())
            (void)driver_input_keyboard_getch();   /* GUI ignores keys */
        int act = poll_pointer();
        if (act != OCSE_ACT_NONE) {
            g_btn = 0; g_prev_btn = 0;
            if (act != OCSE_ACT_NONE)
                ocse_gui_draw();
            return act;
        }
        if (g_need_draw) {
            g_need_draw = 0;
            ocse_gui_draw();
        }
        core_sched_yield();
    }
}
