/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-tui-input-encoding-hardening 4.3
 * File: ocos/kernel/core_tty_utf8.c
 * Purpose: the UTF-8 TTY stream decoder - see core_tty_utf8.h.
 *
 * Why this module exists (the defect it fixes): ush's line editor
 * echoes each key event with a ONE-BYTE write(1, &c, 1); a pasted
 * or IME-committed CJK glyph arrives as 3-4 such writes.  The
 * console path (sys_write -> console_cn_puts_enc) decoded each
 * call INDEPENDENTLY: a lone lead byte is ENC_E_TRUNC, the
 * error branch painted it as a raw Latin-1 cell, and a hanzi
 * echoed as three solid blocks on the frame buffer (the serial
 * tee passed the bytes through, so serial terminals looked fine
 * while the VGA console was mojibake until a full-line redraw).
 *
 * The TTY here carries the incomplete trailing sequence ACROSS
 * writes: E4 (pending) + B8 (pending) + AD (complete) renders ONE
 * 16px glyph through console_cn_putcw.  A foreign byte (ASCII or a
 * new lead) flushes the pending bytes as raw cells first, so an
 * interrupted or corrupt stream degrades exactly like the old
 * one-shot decoder - never a hang, never silence.
 *
 * Width/wrap: console_cn_putcw already advances by the per-codepoint
 * display width (render_chinese_cols) and wraps a wide glyph to the
 * next line BEFORE drawing it - the TTY inherits both behaviors and
 * adds no width logic of its own (the width authority stays in the
 * renderer, mirrored by ush's u8_cols).
 */
#include "core_tty_utf8.h"
#include "svc_display_cn.h"      /* console_cn_putcw */
#include "core_console_screen.h" /* screen_console_putc (raw fallback) */

/* pending sequence state (single console stream; the sys_write path
 * wraps every chunk in an irqsave window, so the state is only
 * touched with interrupts off - same discipline as the 512-byte
 * console chunking in svc_sysapi_syscall.c) */
static u8  t_acc[4];
static u32 t_acc_len  = 0;   /* bytes accumulated               */
static u32 t_acc_need = 0;   /* full length once the lead is in */

static void tty_raw(u8 b) {
    /* the exact fallback the one-shot decoder used: an undecodable
     * byte reaches the stock console (and its serial hook) raw */
    screen_console_putc((char)b);
}

static void tty_flush_pending_raw(void) {
    for (u32 i = 0; i < t_acc_len; i++)
        tty_raw(t_acc[i]);
    t_acc_len = 0;
    t_acc_need = 0;
}

void core_tty_utf8_reset(void) {
    t_acc_len = 0;
    t_acc_need = 0;
}

static void tty_byte(u8 b) {
    if (t_acc_len > 0) {
        /* mid-sequence */
        if ((b & 0xC0) == 0x80) {
            if (t_acc_len < 4) t_acc[t_acc_len] = b;
            t_acc_len++;
            if (t_acc_len >= t_acc_need) {
                /* sequence complete: decode + render one codepoint */
                u32 cp = 0;
                u32 n = t_acc_need;
                cp = t_acc[0] & (n == 2 ? 0x1F : (n == 3 ? 0x0F : 0x07));
                for (u32 i = 1; i < n; i++)
                    cp = (cp << 6) | (t_acc[i] & 0x3F);
                /* RFC 3629 validity (mirrors encoding_utf8_decode):
                 * overlong / surrogate / out-of-range -> raw flush */
                int bad = 0;
                if (n == 2 && cp < 0x80) bad = 1;
                if (n == 3 && cp < 0x800) bad = 1;
                if (n == 4 && cp < 0x10000) bad = 1;
                if (cp >= 0xD800 && cp <= 0xDFFF) bad = 1;
                if (cp > 0x10FFFF) bad = 1;
                if (bad) {
                    tty_flush_pending_raw();
                    return;
                }
                t_acc_len = 0;
                t_acc_need = 0;
                console_cn_putcw(cp);
            }
            return;
        }
        /* interrupted by a non-continuation byte: flush what we had
         * as raw cells, then process this byte fresh */
        tty_flush_pending_raw();
        /* fall through to the idle path below */
    }
    /* idle */
    if (b < 0x80) {
        console_cn_putcw(b);      /* ASCII fast path (stock putc inside) */
        return;
    }
    if ((b & 0xE0) == 0xC0)     { t_acc_need = 2; }
    else if ((b & 0xF0) == 0xE0) { t_acc_need = 3; }
    else if ((b & 0xF8) == 0xF0) { t_acc_need = 4; }
    else {
        tty_raw(b);              /* stray continuation / 0xF8+ lead */
        return;
    }
    t_acc[0] = b;
    t_acc_len = 1;
}

void core_tty_utf8_write(const u8 *s, u32 len) {
    if (!s) return;
    for (u32 i = 0; i < len; i++)
        tty_byte(s[i]);
}
