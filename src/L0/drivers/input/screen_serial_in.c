/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-02
 * File: drivers/display/screen_serial_in.c
 * Purpose: COM1 serial RX -> keyboard queue bridge.
 *
 * 3.0.0-WP04-fix1 (bug 3): a real ANSI CSI decoder. A terminal sends
 * the arrow / Home / End / Delete navigation keys as multi-byte
 * sequences (ESC [ A, ESC [ 3 ~, ...). The old bridge pushed the ESC,
 * the '[' and the final byte as three unrelated keys, so every ring-3
 * line editor received "[A"-style garbage instead of OC_KEY_UP and
 * the navigation keys looked dead. The bridge now pre-decodes the
 * common sequences into the OC_KEY_* space BEFORE injection:
 *   ESC [ A/B/C/D   -> Up / Down / Right / Left
 *   ESC [ H / F     -> Home / End        (xterm)
 *   ESC [ 1~/7~ ... -> Home, 2~ Ins, 3~ Del, 4~/8~ End, 5~ PgUp, 6~ PgDn
 *   ESC O A/B/C/D   -> arrows in application-cursor mode (SS3)
 *   ESC [ < ... M/m -> xterm SGR mouse report: passed through RAW so
 *                      the TUI mouse decoder still sees the full
 *                      sequence byte-for-byte (its own CSI parser).
 *   lone ESC        -> OC_KEY_ESC_KEY (0x9C), the same code the PS/2
 *                      path produces for the physical key, followed by
 *                      reprocessing the next byte in idle state.
 * Sequences the decoder does not know are swallowed whole so a
 * terminal's stray escape burst can never poison the command line.
 */
#include "screen_serial_in.h"
#include "driver_input_keyboard.h"
#include "arch_irq.h"
#include "arch_idt.h"

static inline u8 inb(u16 p) { u8 v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline void outb(u16 p, u8 v) { __asm__ volatile("outb %0, %1" :: "a"(v), "Nd"(p)); }

#define COM1 0x3F8

/* CSI decoder state (persists across IRQs: a sequence may straddle
 * FIFO bursts). States:
 *   0 idle | 1 saw ESC | 2 in CSI | 3 CSI param | 4 SS3 (ESC O) | 5 raw passthrough */
static u8  g_csi_state;
static u8  g_csi_buf[16];
static u8  g_csi_len;

void screen_serial_in_init(void) {
    /* Enable FIFO + trigger at 1 byte. */
    outb(COM1 + 2, 0xC7);
    /* Enable received-data-available IRQ (bit 0 of IER). */
    outb(COM1 + 1, 0x01);
    /* 3.0.0-WP04-fix1: CSI decoder starts idle. */
    g_csi_state = 0;
    g_csi_len = 0;
    /* IRQ4 will now fire on each byte received. */
    arch_irq_register_handler(4, screen_serial_in_irq_handler, NULL);
}

/* Map a finished CSI/SS3 body to an OC_KEY_* code. Returns 0 when the
 * sequence is recognized-but-unmapped (swallow) and -1 when unknown
 * (also swallow; the line must never see sequence fragments). */
static int csi_finalize(const u8 *b, u8 n) {
    if (n == 0) return -1;
    u8 fin = b[n - 1];
    /* No-param finals: arrows + Home/End. */
    if (n == 1) {
        switch (fin) {
            case 'A': return OC_KEY_UP;
            case 'B': return OC_KEY_DOWN;
            case 'C': return OC_KEY_RIGHT;
            case 'D': return OC_KEY_LEFT;
            case 'H': return OC_KEY_HOME;
            case 'F': return OC_KEY_END;
            default:  return -1;
        }
    }
    /* Tilde finals with a numeric parameter. */
    if (fin == '~' && n >= 2) {
        u8 p = b[0];
        switch (p) {
            case '1': case '7': return OC_KEY_HOME;
            case '2':           return OC_KEY_INS;
            case '3':           return OC_KEY_DEL;
            case '4': case '8': return OC_KEY_END;
            case '5':           return OC_KEY_PGUP;
            case '6':           return OC_KEY_PGDN;
            default:            return -1;
        }
    }
    return -1;
}

void screen_serial_in_irq_handler(void *ctx, arch_irq_frame_t *f) {
    (void)ctx; (void)f;
    /* Check IIR: bit 0 = 0 means interrupt pending. */
    u8 iir = inb(COM1 + 2);
    if (iir & 0x01) return;  /* no interrupt */

    /* Drain all available bytes (FIFO may have multiple).
     * BUG-0323 (A7-18): the drain used to be open-ended - a stuck or
     * absent UART whose LSR data-ready bit reads 1 forever (0xFF from a
     * nonexistent port) spun this IRQ handler for eternity and hung the
     * whole machine. Bound the burst; anything left waits for the next
     * IRQ (the FIFO trigger re-arms). */
    int drained = 0;
    while (drained < 32 && (inb(COM1 + 5) & 0x01)) {
        drained++;
        u8 b = inb(COM1);

        /* ---- 3.0.0-WP04-fix1: CSI/SS3 pre-decode ---- */
        if (g_csi_state == 5) {
            /* raw passthrough (xterm SGR mouse): forward until the
             * final M/m byte, then back to idle. */
            u16 key = (b >= 0x80) ? (u16)b + 0x100 : (u16)b;
            driver_input_keyboard_inject(key);
            if (b == 'M' || b == 'm') g_csi_state = 0;
            continue;
        }
        if (g_csi_state == 1) {           /* saw ESC */
            if (b == '[') { g_csi_state = 2; g_csi_len = 0; continue; }
            if (b == 'O') { g_csi_state = 4; g_csi_len = 0; continue; }
            /* Not a sequence opener: deliver the physical Esc code and
             * reprocess this byte from idle. */
            driver_input_keyboard_inject(OC_KEY_ESC_KEY);
            g_csi_state = 0;
            /* fall through to the idle handling of b below */
        } else if (g_csi_state == 2 || g_csi_state == 3) {
            /* in CSI: first param byte decides mouse passthrough. */
            if (g_csi_state == 2 && b == '<') {
                /* SGR mouse: replay the consumed prefix raw (the TUI
                 * mouse decoder wants the full original sequence),
                 * then forward everything up to the final byte. */
                driver_input_keyboard_inject(0x1B);
                driver_input_keyboard_inject((u16)'[');
                driver_input_keyboard_inject((u16)'<');
                g_csi_state = 5;
                continue;
            }
            if ((b >= '0' && b <= '9') || b == ';' || b == '?') {
                if (g_csi_len < sizeof(g_csi_buf)) g_csi_buf[g_csi_len++] = b;
                if (g_csi_len >= sizeof(g_csi_buf)) g_csi_state = 0; /* overflow: drop */
                else g_csi_state = 3;
                continue;
            }
            /* final byte */
            if (b >= 0x40 && b <= 0x7E) {
                if (g_csi_len < sizeof(g_csi_buf)) g_csi_buf[g_csi_len] = b;
                else { g_csi_state = 0; continue; }   /* overflow: drop */
                int key = csi_finalize(g_csi_buf, g_csi_len + 1);
                g_csi_state = 0;
                if (key > 0) driver_input_keyboard_inject((u16)key);
                /* <= 0: swallowed (unknown or intentionally unbound) */
                continue;
            }
            /* garbage inside a sequence: drop the whole thing */
            g_csi_state = 0;
            continue;
        } else if (g_csi_state == 4) {    /* SS3: one final byte */
            g_csi_state = 0;
            if (b >= 'A' && b <= 'D') {
                static const u16 ss3_arrows[4] = {
                    OC_KEY_UP, OC_KEY_DOWN, OC_KEY_RIGHT, OC_KEY_LEFT
                };
                driver_input_keyboard_inject(ss3_arrows[b - 'A']);
            }
            continue;  /* unknown SS3 pair swallowed */
        }

        /* ---- idle: legacy single-byte mapping (unchanged) ---- */
        u16 key;
        switch (b) {
            case '\r': case '\n':
                key = OC_KEY_ENTER;
                break;
            case 0x7F: case 0x08:
                key = OC_KEY_BACKSPACE;
                break;
            case 0x03: /* Ctrl+C */
                key = OC_KEY_CTRL_C;
                break;
            case 0x04: /* Ctrl+D */
                key = OC_KEY_CTRL_D;
                break;
            case 0x1B:
                g_csi_state = 1;   /* arm the sequence detector */
                continue;
            case '\t':
                key = OC_KEY_TAB;
                break;
            default:
                if (b >= 0x20 && b < 0x7F) {
                    key = (u16)b;
                } else if (b >= 0x01 && b < 0x20) {
                    /* WP-10-wp08fix1: pass the remaining Ctrl+letter codes
                     * (0x01=A ... 0x1A=Z) straight through - the kernel
                     * line editor and the editors bind them (Ctrl+A/E/U/
                     * K/W, Ctrl+O save, Ctrl+X exit, Ctrl+K del line...).
                     * A real terminal sends exactly these bytes. */
                    key = (u16)b;
                } else if (b >= 0x80) {
                    /* WP-10-chinese_support: 8-bit bytes are UTF-8
                     * continuation/lead bytes (pasted CJK text). They
                     * must not collide with the OC_KEY_* special range
                     * (0x80..0x9B), so inject them biased by +0x100;
                     * the line editor strips the bias before storing. */
                    key = (u16)b + 0x100;
                } else {
                    continue;
                }
        }
        /* Push directly into the keyboard queue via the internal API.
         * We mark the modifiers as 0 since serial doesn't carry mods. */
        /* Reuse the keyboard IRQ handler's queue by calling a helper. */
        driver_input_keyboard_inject(key);
    }
}
