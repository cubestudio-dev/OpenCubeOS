/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP06c-installer_gui
 * File: ocse/ocse_gui.c
 * Purpose: the OCSE framebuffer front-end: four hardware-check rows,
 * the disk identification panel and three buttons (command-line
 * install / language / re-check). Input: PS/2 aux mouse + USB mouse
 * + I2C touchpad (pointer, tap = click); the Ctrl+Alt+F1/F2 hotkeys
 * are handled by ocse_main.c and checked between interactions.
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
#include "driver_input_ps2mouse.h"
#include "driver_input_keyboard.h"
#include "core_sched.h"
#include "lib_string.h"
#include <stdint.h>

/* Hotkey request flag, set by the keyboard handler in ocse_main.c. */
extern int g_ocse_hotkey_req;

static int g_mx = 400, g_my = 300;      /* pointer position          */
static int g_btn = 0, g_prev_btn = 0;   /* current/previous buttons */
/* #113 fix: press LATCH. The click decision used to sample the live
 * button state per poll; the WP06f full-panel redraw that runs between
 * polls is slower than a human click under TCG, so the press and the
 * release packets drained in the SAME batch, the live 0->1 transition
 * was never observed and every real click was silently dropped (hover
 * and the `click x y` geometry probe were unaffected). The latch
 * OR-accumulates "a button-down arrived" across the whole drain batch
 * so the click survives regardless of poll latency. */
static int g_btn_latch = 0;
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

/* WP-06c: the 4th button enters the install wizard (620,400
 * 150x52 - right of the disk panel, clear of the bottom row). */
#define OCSE_NBTN 4
static const ocse_button_t g_buttons[OCSE_NBTN] = {
    {  40, 516, 300, 52, OCSE_ACT_GOTO_CL,     OCSE_T_BTN_CLI   },
    { 360, 516, 190, 52, OCSE_ACT_RECHECK,     OCSE_T_BTN_CHECK  },
    { 570, 516, 190, 52, OCSE_ACT_TOGGLE_LANG, OCSE_T_BTN_LANG   },
    { 620, 400, 150, 52, OCSE_ACT_WIZARD,      OCSE_T_BTN_INSTALL},
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

/* Public for the OCSECL `click <x> <y>` self-check: the QEMU
 * monitor cannot inject USB mouse events (the v1.0.0 vendored USB
 * stack never had a QEMU-side event injection path - verified against
 * the v1.0.0 tree), so the battery proves the button geometry and
 * action mapping through the same hit_test() the pointer path uses. */
int ocse_gui_hit_test(u32 x, u32 y) {
    for (int i = 0; i < OCSE_NBTN; i++) {
        const ocse_button_t* b = &g_buttons[i];
        if (x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h)
            return b->act;
    }
    return OCSE_ACT_NONE;
}

/* OCSECL `click <x> <y>` self-check: feed the point through the SAME
 * hit_test() the pointer path uses and NAME the action, so the T4
 * battery can prove the button geometry without injecting pointer
 * events (the QEMU monitor cannot reach the vendored v1.0.0 USB
 * stack). Lives next to hit_test so the two can never drift. */
int ocse_gui_click_probe(int x, int y) {
    static const char* const names[] = {
        "none", "goto-cl", "toggle-lang", "recheck", "goto-gui",
        "wizard",
    };
    int act = ocse_gui_hit_test((u32)x, (u32)y);
    const char* name = (act >= 0 && act < (int)(sizeof(names) /
                        sizeof(names[0]))) ? names[act] : "?";
    char nx[12], ny[12];
    u64_to_str((u64)x, nx);
    u64_to_str((u64)y, ny);
    ocse_out("click at (");
    ocse_out(nx);
    ocse_out(",");
    ocse_out(ny);
    ocse_out("): action=");
    ocse_outln(name);
    return 0;
}

static void draw_pointer(void) {
    /* WP06f: the standard arrow sprite (white outline + black fill,
     * transparent background) - replaces the three-fill_rect block
     * arrow that read as a random blob. */
    screen_fb_draw_cursor((u32)g_mx, (u32)g_my);
}

void ocse_gui_draw(void) {
    /* WP06f: the panel renders into the RAM back buffer first (when
     * attached); screen_fb_present() publishes the changed rows in one
     * pass - the blank-then-repaint flash between states is gone. */
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
        int hover = ocse_gui_hit_test((u32)g_mx, (u32)g_my) == b->act;
        screen_fb_fill_rect(b->x, b->y, b->w, b->h,
                            hover ? COL_BTN_HOV : COL_BTN);
        screen_fb_fill_rect(b->x, b->y, b->w, 2, 0x00000000);
        gui_text(b->x + 16, b->y + 18, 0xFFF4F1EC,
                 i == 2 ? ocse_lang_name(!ocse_i18n_lang())
                        : ocse_tr(b->key));
    }
    gui_text(40, 580, COL_SUB, ocse_tr(OCSE_T_GUI_HINT));
    draw_pointer();
    screen_fb_present();
}

void ocse_gui_init(void) {
    g_mx = 400; g_my = 300; g_btn = g_prev_btn = g_btn_latch = 0;
    g_need_draw = 1;
    ocse_gui_draw();
}

/* Poll pointer inputs once; returns OCSE_ACT_* on click. */
static void clamp_pointer(void) {
    const screen_fb_info_t* fb = screen_fb_get_info();
    if (fb->width == 0 || fb->height == 0) return;
    if (g_mx < 0) g_mx = 0;
    if (g_my < 0) g_my = 0;
    if ((u32)g_mx >= fb->width)  g_mx = (int)fb->width - 1;
    if ((u32)g_my >= fb->height) g_my = (int)fb->height - 1;
}

static int poll_pointer(void) {
    /* PS/2 aux mouse (WP06c: the wizard's third pointer source, now
     * also on the main panel - QEMU's default pointing device is a
     * PS/2 mouse, and a machine without USB HID still gets a
     * pointer). Y grows upward on the aux port. */
    ps2mouse_event_t mv;
    while (driver_input_ps2mouse_read_event(&mv)) {
        g_mx += mv.dx;
        g_my -= mv.dy;
        if (mv.buttons & 1) { g_btn |= 1; g_btn_latch = 1; } else g_btn &= ~1;
        clamp_pointer();
        g_need_draw = 1;
    }
    driver_usb_mouse_event_t ev;
    while (driver_usb_mouse_read_event(&ev)) {
        g_mx += ev.dx;
        g_my += ev.dy;
        if (ev.buttons & 1) { g_btn |= 1; g_btn_latch = 1; } else g_btn &= ~1;
        clamp_pointer();
        g_need_draw = 1;
    }
    touchpad_event_t te;
    /* v1.0.0 contract: 0 = event returned, -1 = empty (the USB
     * mouse ring is the other way round - 1 = event). Live-caught
     * in the T4 battery: `while (get_event(&te))` spun forever on
     * the -1 empty case and froze the GUI loop. */
    while (driver_input_touchpad_get_event(&te) == 0) {
        if (te.type == TPE_MOVE) {
            g_mx += te.dx;
            g_my += te.dy;
            clamp_pointer();
            g_need_draw = 1;
        } else if (te.type == TPE_TAP) {
            g_btn |= 1;             /* tap = press; released next poll */
            g_btn_latch = 1;
            g_need_draw = 1;
        }
    }
    if (g_btn_latch && !g_prev_btn) {   /* a press arrived = click */
        g_prev_btn = 1;                 /* one click per press gesture */
        g_btn_latch = 0;
        return ocse_gui_hit_test((u32)g_mx, (u32)g_my);
    }
    g_btn_latch = 0;                    /* batch fully evaluated */
    if (!g_btn) g_prev_btn = 0;         /* release seen: re-arm */
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
            g_btn = 0; g_prev_btn = 0; g_btn_latch = 0;
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
