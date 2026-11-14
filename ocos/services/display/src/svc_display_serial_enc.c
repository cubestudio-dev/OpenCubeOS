/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP06g3-ocse_enc_fix
 * File: services/display/serial_enc.c
 * Purpose: COM1 serial tee with configurable output encoding.
 *
 * GBK mode semantics: ASCII passes through untouched; a complete
 * UTF-8 sequence is accumulated, decoded once, and re-encoded as
 * GB2312 (1-2 bytes).  Unmappable codepoints and broken sequences
 * degrade to '?' - never silence, never a hang.  CR/LF handling
 * matches the legacy tee ('\n' -> '\r\n').
 *
 * Concurrency note: the hook may run with interrupts on (the console
 * mirror path).  A multi-byte UTF-8 sequence straddling two console
 * writes is handled by the accumulator state machine - the same
 * discipline console_cn_putcw applies for its irqsave windows; here
 * the worst case of an interleaved foreign byte is a '?' + restart,
 * which cannot desync the stream. */
#include "svc_display_serial_enc.h"
#include "svc_encoding_core.h"
#include "core_console_screen.h"
#include "core_lib_string.h"

#define COM1 0x3F8

static inline void serial_enc_outb(u16 p, u8 v) {
    __asm__ volatile("outb %0, %1" :: "a"(v), "Nd"(p));
}
static inline u8 serial_enc_inb(u16 p) {
    u8 v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(p)); return v;
}
static void serial_enc_putc(char c) {
    while ((serial_enc_inb(COM1 + 5) & 0x20) == 0) ;
    serial_enc_outb(COM1, (u8)c);
}

/* Output encoding: 0 = UTF-8 (default - the legacy byte-exact behavior),
 * 1 = GBK (for the GBK-default Chinese serial-terminal ecosystem). */
static int g_enc_gbk = 0;

/* UTF-8 sequence accumulator (GBK mode only). */
static u8  g_acc[4];
static u32 g_acc_len  = 0;   /* bytes accumulated so far   */
static u32 g_acc_need = 0;   /* total length once the lead byte is seen */

void serial_enc_init(void) {
    g_enc_gbk  = 0;
    g_acc_len  = 0;
    g_acc_need = 0;
}

int serial_enc_set(const char *name) {
    if (!name) return -1;
    if (name[0] == 'u' && name[1] == 't' && name[2] == 'f' &&
        name[3] == '8' && name[4] == 0) {
        g_enc_gbk = 0;
        g_acc_len = 0; g_acc_need = 0;
        return 0;
    }
    if (name[0] == 'g' && name[1] == 'b' && name[2] == 'k' && name[3] == 0) {
        g_enc_gbk = 1;
        g_acc_len = 0; g_acc_need = 0;
        return 0;
    }
    return -1;
}

const char *serial_enc_get_name(void) {
    return g_enc_gbk ? "gbk" : "utf8";
}

/* Feed one byte in GBK mode through the accumulator. */
static void serial_enc_feed_gbk(u8 ch) {
    if (ch < 0x80) {                       /* ASCII: straight through */
        serial_enc_putc((char)ch);
        return;
    }
    if (g_acc_len == 0) {
        /* lead byte: compute the expected sequence length */
        if      ((ch & 0xE0) == 0xC0) g_acc_need = 2;
        else if ((ch & 0xF0) == 0xE0) g_acc_need = 3;
        else if ((ch & 0xF8) == 0xF0) g_acc_need = 4;
        else { serial_enc_putc('?'); return; }   /* stray continuation */
        g_acc[0] = ch;
        g_acc_len = 1;
        return;
    }
    if ((ch & 0xC0) != 0x80) {
        /* broken sequence: drop what we had, restart on this byte */
        g_acc_len = 0; g_acc_need = 0;
        serial_enc_putc('?');
        serial_enc_feed_gbk(ch);
        return;
    }
    if (g_acc_len < 4) g_acc[g_acc_len] = ch;
    g_acc_len++;
    if (g_acc_len < g_acc_need) return;    /* sequence still building */

    /* complete sequence: decode once, re-encode as GB2312 */
    u32 cp = 0, consumed = 0;
    u32 need = g_acc_need;
    g_acc_len = 0; g_acc_need = 0;
    if (encoding_utf8_decode(g_acc, need, &cp, &consumed) != 0 ||
        consumed != need) {
        serial_enc_putc('?');
        return;
    }
    u8 gb[2]; u32 produced = 0;
    if (encoding_gb2312_encode(cp, gb, sizeof(gb), &produced) != 0 ||
        produced == 0) {
        serial_enc_putc('?');              /* unmappable: degrade, never hang */
        return;
    }
    for (u32 i = 0; i < produced; ++i) serial_enc_putc((char)gb[i]);
}

int serial_enc_hook(void *ctx, u8 ch) {
    (void)ctx;
    if (ch == '\n') serial_enc_putc('\r');
    if (g_enc_gbk) {
        serial_enc_feed_gbk(ch);
    } else {
        serial_enc_putc((char)ch);
    }
    return 0;
}

/* ---- the `serial_encoding` command body (L1SVC_DISP_SERIAL_ENC) -----
 * System-command strings stay English (system-visible strings are
 * English; the encoding names themselves are the terminology). */
int disp_cmd_serial_encoding(const char *args) {
    if (!args || !args[0]) {
        screen_console_puts("serial encoding: ");
        screen_console_puts(serial_enc_get_name());
        screen_console_puts("\n");
        return 0;
    }
    /* trim the first token */
    char tok[8];
    int n = 0;
    const char *p = args;
    while (*p == ' ') p++;
    while (p[n] && p[n] != ' ' && n < 7) { tok[n] = p[n]; n++; }
    tok[n] = 0;
    if (serial_enc_set(tok) == 0) {
        screen_console_puts("serial encoding: ");
        screen_console_puts(tok);
        screen_console_puts("\n");
        return 0;
    }
    screen_console_puts("usage: serial_encoding [utf8|gbk] "
                        "(no argument shows the current encoding)\n");
    return 1;
}
