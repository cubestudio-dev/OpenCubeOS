/* SPDX-License-Identifier: Apache-2.0
 * [3.0.0-WP04 G11c: revived verbatim from v2.2.0 tui/tui_input.c; include
 *  paths flattened to the WP-03 -I layout, shell_lang->disp_lang and
 *  shell_execute*->tui_exec* renamed (see tui_exec.h).]
 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10-tui
 * File: tui/tui_input.c
 * Purpose: Input: blocking / timeout / nodelay character reads from
 *          the keyboard queue, OC_KEY_* -> TUI KEY_* mapping, a real
 *          ANSI CSI decoder for xterm-style functional keys and SGR
 *          mouse reports arriving over the COM1 bridge, and the
 *          touchpad mouse event path.
 */
#include "tui.h"
#include "driver_input_keyboard.h"
#include "driver_input_i2chid.h"
#include "core_timer.h"
#include "core_sched.h"

static int g_nodelay;
static int g_mouse_enabled;
static tui_mouse_event_t g_mouse_pending;
static int g_mouse_pending_valid;

void tui_nodelay(int on) { g_nodelay = on ? 1 : 0; }

int tui_mouse_enable(int on) {
    g_mouse_enabled = on ? 1 : 0;
    /* fix2: actually toggle SGR mouse reporting on the wire when the
     * console is an xterm-class serial terminal (the flag flip used to
     * be the whole implementation - mouse support could never work). */
    tui_core_serial_mouse(on);
    return 0;
}

/* Translate a raw keyboard byte/special key into a TUI key code. */
static int map_key(int c) {
    if (c == 0) return TUI_KEY_NONE;
    if (c < 0x20) {
        switch (c) {
        case 0x08: return TUI_KEY_BACKSPACE;
        case 0x09: return TUI_KEY_TAB;
        case 0x0A: case 0x0D: return TUI_KEY_ENTER;
        case 0x1B: return TUI_KEY_ESC;
        default:   return TUI_KEY_CTRL_A + (c - 1);
        }
    }
    if (c >= 0x80) {
        if (c >= OC_KEY_F1 && c <= OC_KEY_F1 + 11) return TUI_KEY_F(c - OC_KEY_F1 + 1);
        switch (c) {
        case OC_KEY_ESC_KEY: return TUI_KEY_ESC;   /* physical Esc (PS/2) */
        case OC_KEY_UP:     return TUI_KEY_UP;
        case OC_KEY_DOWN:   return TUI_KEY_DOWN;
        case OC_KEY_LEFT:   return TUI_KEY_LEFT;
        case OC_KEY_RIGHT:  return TUI_KEY_RIGHT;
        case OC_KEY_HOME:   return TUI_KEY_HOME;
        case OC_KEY_END:    return TUI_KEY_END;
        case OC_KEY_PGUP:   return TUI_KEY_PGUP;
        case OC_KEY_PGDN:   return TUI_KEY_PGDN;
        case OC_KEY_INS:    return TUI_KEY_INSERT;
        case OC_KEY_DEL:    return TUI_KEY_DELETE;
        default:            return TUI_KEY_NONE;   /* unmapped special */
        }
    }
    return c;                       /* printable ASCII as-is */
}

/* ---- ANSI CSI decode (bytes already through the keyboard queue) ---- */

#define CSI_MAX 16

typedef struct {
    int active;            /* saw ESC (sequence armed)              */
    int saw_lbrack;        /* saw the '[' that makes it a real CSI   */
    int priv;              /* saw '?'                                */
    int nparams;
    int params[CSI_MAX];
    int params_set[CSI_MAX];
    int final;
} csi_state_t;

static csi_state_t g_csi;
/* fix2: a lone ESC (serial terminals send exactly one 0x1B byte for
 * the Esc key) used to arm the CSI decoder and then be SWALLOWED by
 * the first following key - the user had to press Esc twice. The key
 * that follows a non-CSI ESC is stashed here and delivered first. */
static int g_csi_stash = TUI_KEY_NONE;

static void csi_reset(void) {
    g_csi.active = 0;
    g_csi.saw_lbrack = 0;
    g_csi.priv = 0;
    g_csi.nparams = 0;
    g_csi.final = 0;
    for (int i = 0; i < CSI_MAX; i++) { g_csi.params[i] = 0; g_csi.params_set[i] = 0; }
}

/* Feed one byte into the decoder; returns a TUI key when a sequence
 * completes, TUI_KEY_NONE otherwise, or a positive literal byte. */
static int csi_feed_inner(int c) {
    if (!g_csi.active) {
        if (c == 0x1B) { csi_reset(); g_csi.active = 1; return TUI_KEY_NONE; }
        return map_key(c);
    }
    /* Two ESC bytes in a row: the first one was a real Esc key press
     * (xterm delivers ESC ESC the same way), so hand it to the app and
     * restart the sequence with the new opener. */
    if (c == 0x1B && g_csi.nparams == 0 && !g_csi.priv && g_csi.final == 0) {
        csi_reset();
        g_csi.active = 1;
        return TUI_KEY_ESC;
    }
    if (!g_csi.saw_lbrack) {
        /* ANSI: a control sequence is ESC [ <params> <final>.  Until the
         * '[' arrives NOTHING is a CSI - every other byte is a plain key
         * press.  Without this, "ESC ESC q" (leave a page, then quit)
         * fed the q to the sequence finalizer, which swallowed it as an
         * unknown CSI and the app hung waiting for a key that was never
         * delivered.
         * fix2: deliver the pending ESC first and stash the mapped key
         * for the next call - a single Esc press (one 0x1B byte on the
         * serial bridge) now reaches the app immediately instead of
         * being eaten by the next keystroke. */
        if (c == '[' && !g_csi.priv) { g_csi.saw_lbrack = 1; return TUI_KEY_NONE; }
        g_csi.active = 0;
        g_csi_stash = map_key(c);
        return TUI_KEY_ESC;
    }
    if (c == '?') { g_csi.priv = 1; return TUI_KEY_NONE; }
    if (c >= '0' && c <= '9') {
        if (g_csi.nparams < CSI_MAX) {
            g_csi.params[g_csi.nparams] = g_csi.params[g_csi.nparams] * 10 + (c - '0');
            g_csi.params_set[g_csi.nparams] = 1;
        }
        return TUI_KEY_NONE;
    }
    if (c == ';') {
        if (g_csi.nparams < CSI_MAX - 1) g_csi.nparams++;
        return TUI_KEY_NONE;
    }
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '~') {
        g_csi.final = c;
        int p0 = g_csi.params_set[0] ? g_csi.params[0] : 1;
        int p1 = g_csi.params_set[1] ? g_csi.params[1] : 1;
        int p2 = g_csi.params_set[2] ? g_csi.params[2] : 0;
        csi_state_t st = g_csi;
        csi_reset();
        /* SGR mouse report: ESC [ < b ; x ; y (M or m) */
        if (st.priv && (st.final == 'M' || st.final == 'm') && st.nparams >= 2) {
            /* note: the '<' sets priv; params hold b/x/y */
            tui_mouse_event_t ev;
            int b = p0;
            ev.x = p1 - 1;
            ev.y = p2 - 1;
            ev.modifiers = (b & 4 ? 4 : 0) | (b & 2 ? 1 : 0) | (b & 8 ? 2 : 0);
            if (b & 64) {           /* 64/65 wheel */
                ev.button = 3;
                ev.action = (b & 1) ? TUI_BUTTON_WHEEL_DOWN : TUI_BUTTON_WHEEL_UP;
            } else {
                ev.button = b & 3;
                ev.action = (st.final == 'M') ? TUI_BUTTON_PRESSED
                                              : TUI_BUTTON_RELEASED;
            }
            if (g_mouse_enabled) {
                g_mouse_pending = ev;
                g_mouse_pending_valid = 1;
                return TUI_KEY_MOUSE;
            }
            return TUI_KEY_NONE;
        }
        switch (st.final) {
        case 'A': return TUI_KEY_UP;
        case 'B': return TUI_KEY_DOWN;
        case 'C': return TUI_KEY_RIGHT;
        case 'D': return TUI_KEY_LEFT;
        case 'H': return TUI_KEY_HOME;
        case 'F': return TUI_KEY_END;
        case 'Z': return TUI_KEY_TAB;      /* shift-tab arrives as CSI Z */
        case '~':
            switch (p0) {
            case 1: case 7: return TUI_KEY_HOME;
            case 2: return TUI_KEY_INSERT;
            case 3: return TUI_KEY_DELETE;
            case 4: case 8: return TUI_KEY_END;
            case 5: return TUI_KEY_PGUP;
            case 6: return TUI_KEY_PGDN;
            case 11: case 15: return TUI_KEY_F1;
            case 12: case 17: return TUI_KEY_F2;
            case 13: case 18: return TUI_KEY_F3;
            case 14: case 19: return TUI_KEY_F4;
            case 20: return TUI_KEY_F5;
            case 21: return TUI_KEY_F6;
            case 23: return TUI_KEY_F7;
            case 24: return TUI_KEY_F8;
            case 25: return TUI_KEY_F9;
            case 26: return TUI_KEY_F10;
            case 28: return TUI_KEY_F11;
            case 29: return TUI_KEY_F12;
            default: return TUI_KEY_NONE;
            }
        default:
            return TUI_KEY_NONE;           /* unknown CSI: swallow */
        }
    }
    /* ESC [ < ... (xterm SGR mouse prefix) */
    if (c == '<') { g_csi.priv = 1; return TUI_KEY_NONE; }
    /* anything else inside CSI: ignore */
    return TUI_KEY_NONE;
}

/* fix2 wrapper: when the previous call delivered a pending ESC and
 * stashed the following key, hand the stash out first and fold the new
 * byte into the stash for the call after that. This keeps sequences
 * intact across the boundary ("ESC x ESC [ A" still delivers
 * ESC, x, UP) and makes a single Esc press reach the app. */
static int csi_feed(int c) {
    if (g_csi_stash != TUI_KEY_NONE) {
        int k = g_csi_stash;
        g_csi_stash = csi_feed_inner(c);
        return k;
    }
    return csi_feed_inner(c);
}

/* ---- public reads ---- */

int tui_getch_timeout(int ms) {
    u64 deadline = core_timer_now_ms() + (u64)(ms < 0 ? 0 : ms);
    for (;;) {
        if (g_mouse_pending_valid && g_mouse_enabled) {
            /* mouse events are delivered once; getch returns the
             * marker and the event is fetched with tui_mouse_get */
            return TUI_KEY_MOUSE;
        }
        int c = driver_input_keyboard_getch();
        if (c > 0) {
            /* real keycode.  The keyboard queue returns -1 when empty
             * (never 0 for "no key"), so the test must be c > 0: a
             * c != 0 check would feed -1 into the CSI decoder, whose
             * map_key maps it to CTRL_A-1 == -1 and turned every
             * tui_getch into a non-blocking instant -1 return (pages
             * then exited on the first poll and the menu spun without
             * ever reaching the yield below). */
            int k = csi_feed(c);
            if (k != TUI_KEY_NONE) return k;
            continue;
        }
        /* WP-10-tui-qa: flush the fix2 stash once the queue is dry.
         * The stash holds a COMPLETE keystroke (the byte that followed
         * a lone ESC); it used to sit until the NEXT byte arrived, so
         * after any Esc press every subsequent plain key lagged one
         * behind: a serial user pressing Esc then '2' saw the '2' act
         * only when '3' was pressed.  Flushing on the empty queue (and
         * only then - a mid-CSI stash is still consumed by the next
         * CSI byte through csi_feed) delivers the key exactly once,
         * right after the Esc it followed.  The "ESC x ESC [ A"
         * fix2 sequence is unaffected: while bytes remain queued the
         * stash is still handed out by csi_feed itself. */
        if (g_csi_stash != TUI_KEY_NONE) {
            int k = g_csi_stash;
            g_csi_stash = TUI_KEY_NONE;
            return k;
        }
        if (g_nodelay) return -1;
        if (ms >= 0 && core_timer_now_ms() >= deadline) return -1;
        /* 3.0.0-WP04 G11c: the v2.2.0 core_sched_yield() breather is
         * FORBIDDEN inside the ring-3 service dispatch frame (the
         * int 0x80 SVC path this TUI now runs on) - switching tasks
         * from that frame leaves interrupts off (same lesson as
         * ime_console_compose in disp_cmds_cn.c).
         * Park on sti;hlt instead: any interrupt (PIT tick, keyboard
         * IRQ) wakes the loop, so the timeout polls stay live. */
        __asm__ volatile("sti; hlt");
    }
}

int tui_getch(void) {
    return tui_getch_timeout(-1);          /* -1 = wait indefinitely */
}

int tui_mouse_get(tui_mouse_event_t *out) {
    if (!out) return -1;
    if (!g_mouse_pending_valid) return -1;
    *out = g_mouse_pending;
    g_mouse_pending_valid = 0;
    return 0;
}

/* Touchpad path: real I2C-HID events (QEMU has no model; on hardware
 * without the device this reports no-event honestly). */
int tui_mouse_poll_touchpad(void) {
    touchpad_event_t ev;
    if (driver_input_touchpad_get_event(&ev) != 0) return -1;
    if (!g_mouse_enabled) return 0;
    tui_mouse_event_t m;
    m.button = 0;
    m.action = TUI_BUTTON_PRESSED;
    m.modifiers = 0;
    switch (ev.type) {
    default:
        m.x = ev.x;
        m.y = ev.y;
        break;
    }
    g_mouse_pending = m;
    g_mouse_pending_valid = 1;
    return 0;
}

/* Session reset: the CSI decoder and the mouse pending flag are module
 * state that must not leak into the next TUI session - a sequence left
 * active by the previous session swallowed the next session's plain
 * keystrokes (the 'q' page exit was eaten as a bogus CSI final byte).
 * Called from tui_init/tui_shutdown. */
void tui_input_reset(void) {
    csi_reset();
    g_csi_stash = TUI_KEY_NONE;
    g_mouse_pending_valid = 0;
}

/* Test hook: raw-key mapping (OC_KEY_* -> TUI KEY_*). */
int tui_input_map_key(int c) { return map_key(c); }

/* Test hook: feed raw bytes through the same decoder (used by
 * tui-test to verify the CSI parser against real escape sequences). */
int tui_input_feed_raw(int c) {
    for (;;) {
        int k = csi_feed(c);
        if (k != TUI_KEY_NONE) return k;
        return -1;                         /* sequence still open */
    }
}
