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
#include "core_lib_config.h"    /* WP-runtime-hardening 4.5: persistence */
#include "core_i18n_res.h"     /* WP-runtime-hardening 4.4: get_text */

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
        /* WP-runtime-hardening 4.5: persist (best effort - the config
         * store may be the ramfs fallback in live environments) */
        lib_config_write("serial.encoding", "utf8");
        return 0;
    }
    if (name[0] == 'g' && name[1] == 'b' && name[2] == 'k' && name[3] == 0) {
        g_enc_gbk = 1;
        g_acc_len = 0; g_acc_need = 0;
        lib_config_write("serial.encoding", "gbk");
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

/* ---- the `serial-probe` wizard op (L1SVC_DISP_SER_PROBE, spec 4.5) --
 * The user's serial TERMINAL decoding is the thing we cannot see from
 * inside the machine - the wizard shows one labeled sample in each
 * encoding and asks which one reads correctly:
 *
 *   serial-probe emit  - two labeled samples RAW on COM1 (bypassing
 *                        the tee hook: sample 1 is UTF-8 bytes, sample
 *                        2 is the same text transcoded to GBK) + the
 *                        labels on the VGA console
 *   serial-probe set 1 - encoding = utf8 (persisted)
 *   serial-probe set 2 - encoding = gbk  (persisted)
 *   serial-probe       - status
 * The console-visible strings go through get_text (the 4.4 resource);
 * the raw samples are byte-exact by definition. */
static const char k_probe_sample[] =
    "\xe4\xb8\xb2\xe5\x8f\xa3\xe7\xbc\x96\xe7\xa0\x81"
    "\xe6\xb5\x8b\xe8\xaf\x95 123 OK";   /* 串口编码测试 123 OK */

static void probe_raw(const char* s) {
    while (*s)
        serial_enc_putc(*s++);
}

int disp_cmd_serial_probe(const char *args) {
    if (!args || !args[0]) {
        screen_console_puts("serial-probe: ");
        screen_console_puts(serial_enc_get_name());
        screen_console_puts("\n");
        return 0;
    }
    if (args[0] == 'e' && args[1] == 'm' && args[2] == 'i' &&
        args[3] == 't' && (args[4] == 0 || args[4] == ' ')) {
        /* labels on both channels; the SAMPLES only raw on COM1 so
         * the tee cannot re-transcode what we are measuring */
        probe_raw("\r\n");
        probe_raw("serial-probe [1] UTF-8: ");
        probe_raw(k_probe_sample);
        probe_raw("\r\n");
        probe_raw("serial-probe [2] GBK:   ");
        /* transcode the sample to GBK, byte by byte, raw on the wire */
        const u8* p = (const u8*)k_probe_sample;
        while (*p) {
            if (*p < 0x80) {
                serial_enc_putc((char)*p++);
                continue;
            }
            u32 cp = 0, consumed = 0;
            u8 gb[2]; u32 produced = 0;
            if (encoding_utf8_decode(p, 4, &cp, &consumed) != 0) {
                serial_enc_putc('?');
                p++;
                continue;
            }
            if (encoding_gb2312_encode(cp, gb, sizeof(gb), &produced) != 0 ||
                produced == 0) {
                serial_enc_putc('?');
            } else {
                for (u32 i = 0; i < produced; ++i)
                    serial_enc_putc((char)gb[i]);
            }
            p += consumed;
        }
        probe_raw("\r\n");
        /* the console (VGA) side: labels + the readable UTF-8 sample */
        screen_console_puts(get_text(MSG_SER_ENC_PROBE_UTF8));
        screen_console_puts(" ");
        screen_console_puts(k_probe_sample);
        screen_console_puts("\n");
        screen_console_puts(get_text(MSG_SER_ENC_PROBE_GBK));
        screen_console_puts(" ");
        screen_console_puts(k_probe_sample);
        screen_console_puts("\n");
        screen_console_puts(get_text(MSG_SER_ENC_PROBE_ASK));
        screen_console_puts("\n");
        return 0;
    }
    if (args[0] == 's' && args[1] == 'e' && args[2] == 't' &&
        (args[3] == ' ' || args[3] == 0)) {
        const char* pick = args[3] ? args + 4 : args + 3;
        while (*pick == ' ') pick++;
        if (pick[0] == '1' || (pick[0] == 'u' && pick[1] == 't')) {
            serial_enc_set("utf8");
            screen_console_puts(get_text(MSG_SER_ENC_PROBE_SAVED));
            screen_console_puts(" (utf8)\n");
            return 0;
        }
        if (pick[0] == '2' || (pick[0] == 'g' && pick[1] == 'b')) {
            serial_enc_set("gbk");
            screen_console_puts(get_text(MSG_SER_ENC_PROBE_SAVED));
            screen_console_puts(" (gbk)\n");
            return 0;
        }
    }
    screen_console_puts("usage: serial-probe emit | set 1|2 | (status)\n");
    return 1;
}
