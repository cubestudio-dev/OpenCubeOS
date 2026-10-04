/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-02
 * File: kernel/console_in.c
 * Purpose: Console input line editor + L1 input hooks.
 */
#include "screen_console_in.h"
#include "driver_input_keyboard.h"
#include "screen_console.h"
#include "lib_string.h"

#define OC_CONS_IN_BUF_LEN 256

static char g_line[OC_CONS_IN_BUF_LEN];
static int  g_line_len = 0;
static int  g_line_ready = 0;  /* 1 when g_line contains a complete line */

/* L1 input hook chain. */
#define OC_CONS_HOOK_LEN 4
static screen_console_in_hook_fn g_hooks[OC_CONS_HOOK_LEN];

void screen_console_in_init(void) {
    g_line_len = 0;
    g_line_ready = 0;
    memset(g_hooks, 0, sizeof(g_hooks));
    memset(g_line, 0, sizeof(g_line));
}

int screen_console_in_register_hook(screen_console_in_hook_fn fn) {
    if (!fn) return -1;
    for (int i = 0; i < OC_CONS_HOOK_LEN; i++) {
        if (g_hooks[i] == NULL) { g_hooks[i] = fn; return 0; }
    }
    return -2;
}

int screen_console_in_unregister_hook(screen_console_in_hook_fn fn) {
    if (!fn) return -1;
    for (int i = 0; i < OC_CONS_HOOK_LEN; i++) {
        if (g_hooks[i] == fn) {
            g_hooks[i] = NULL;
            for (int j = i; j + 1 < OC_CONS_HOOK_LEN; j++) {
                g_hooks[j] = g_hooks[j+1];
                g_hooks[j+1] = NULL;
            }
            return 0;
        }
    }
    return -2;
}

void screen_console_in_inject(const char *text) {
    /* BUG-024 FIX: Inject text into the keyboard queue so that
     * screen_console_in_readline sees it as if the user typed it.
     * Old code only echoed the text and ran hooks, but never
     * wrote to g_line — so readline never processed the injected
     * text as a command. Now we push each character (including
     * ENTER) into the keyboard queue. */
    if (!text) return;
    int len = 0;
    while (text[len]) len++;
    for (int j = 0; j < OC_CONS_HOOK_LEN; j++) {
        if (g_hooks[j]) {
            g_hooks[j](text, len);
        }
    }
    /* Push each character into the keyboard queue. */
    for (int i = 0; i < len; i++) {
        char c = text[i];
        u16 key;
        if (c == '\n' || c == '\r') key = OC_KEY_ENTER;
        else if (c == '\t') key = OC_KEY_TAB;
        else if (c == 0x7F || c == 0x08) key = OC_KEY_BACKSPACE;
        else if (c == 0x03) key = OC_KEY_CTRL_C;
        else if (c >= 0x20 && c < 0x7F) key = (u16)c;
        else continue;
        driver_input_keyboard_inject(key);
    }
}

static void finish_line(void) {
    /* P2-09 FIX: defensive bounds check on the line buffer index. Even
     * though the pump's per-keystroke cases clamp g_line_len below
     * OC_CONS_IN_BUF_LEN - 1, finish_line() is called from inject
     * paths and from any future caller that may have bypassed the per-
     * keystroke guard. Without this clamp, `g_line[g_line_len] = 0`
     * could write past the end of the static buffer. */
    if (g_line_len < 0) g_line_len = 0;
    if (g_line_len >= OC_CONS_IN_BUF_LEN) g_line_len = OC_CONS_IN_BUF_LEN - 1;
    g_line[g_line_len] = 0;
    /* Run L1 hooks. */
    for (int i = 0; i < OC_CONS_HOOK_LEN; i++) {
        if (g_hooks[i]) {
            if (g_hooks[i](g_line, g_line_len)) {
                /* L1 consumed it - reset and don't mark ready. */
                g_line_len = 0;
                return;
            }
        }
    }
    g_line_ready = 1;
}

int screen_console_in_pump(void) {
    if (g_line_ready) return 1;
    int k = driver_input_keyboard_getch();
    if (k < 0) return 0;

    switch (k) {
        case OC_KEY_ENTER:
            screen_console_putc('\n');
            finish_line();
            return g_line_ready ? 1 : 0;
        case OC_KEY_BACKSPACE:
            if (g_line_len > 0) {
                g_line_len--;
                screen_console_putc('\b');
            }
            return 0;
        case OC_KEY_CTRL_C:
            /* Cancel current line. */
            screen_console_puts("^C\n");
            g_line_len = 0;
            g_line[0] = 0;
            g_line_ready = 1;  /* deliver empty line as cancellation */
            return 1;
        case OC_KEY_TAB:
            if (g_line_len < OC_CONS_IN_BUF_LEN - 1) {
                g_line[g_line_len++] = '\t';
                screen_console_putc('\t');
            }
            return 0;
        default:
            if (k >= 0x20 && k < 0x7F && g_line_len < OC_CONS_IN_BUF_LEN - 1) {
                g_line[g_line_len++] = (char)k;
                screen_console_putc((char)k);
            }
            /* WP-09-FIX BUG-015: characters beyond the 255-char line
             * buffer are still dropped, but with the keyboard ring now at
             * 1024 entries the ENTER byte reliably arrives, so readline
             * returns (previously a pasted long line could lose the
             * ENTER byte to ring overflow and hang the shell forever). */
            return 0;
    }
}

int screen_console_in_readline(char *buf, int size) {
    /* P0-2 FIX: guard against size <= 0. Previously `len = size - 1` with
     * size==0 produced len=-1, then memcpy with (usize)-1 copied ~2^64
     * bytes and buf[-1]=0 wrote out of bounds. */
    if (!buf || size <= 0) return 0;
    /* Pump until a line is ready.
     * WP-09-FIX BUG-008: drain the buffered characters before hlt-waiting.
     * The old loop hlt-ed after every single character, so a shell that
     * only gets scheduled once per starvation-guard window consumed just
     * one buffered keystroke per turn — an 8-char command took 8 turns.
     * Draining first keeps one full line per scheduling window. */
    while (!g_line_ready) {
        int pumped = 0;
        while (!g_line_ready && screen_console_in_pump()) pumped = 1;
        if (g_line_ready) break;
        if (pumped) continue;   /* buffer had data; drain more next spin */
        /* Enable interrupts so keyboard IRQs fire while we wait. */
        __asm__ volatile("sti");
        __asm__ volatile("hlt");
        screen_console_in_pump();
    }
    int len = g_line_len;
    if (len >= size) len = size - 1;
    memcpy(buf, g_line, len);
    buf[len] = 0;
    /* Reset for next line. */
    g_line_len = 0;
    g_line_ready = 0;
    return len;
}
