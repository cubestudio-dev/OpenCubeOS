/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-02
 * File: kernel/lib/lib_log.c
 *
 * WP-02: lib_log_stage() now uses real timestamps from core_timer_now_ms().
 * Before the PIT is up (timer not initialized), timestamps read 0 which
 * is correct - the boot log before timer init shows [00:00:00.000].
 */
#include "lib_log.h"
#include "screen_console.h"
#include "lib_string.h"
#include "core_timer.h"
#include "lib_klog.h"   /* WP-12: mirror boot lines into the klog ring */

/* ---- WP-10-tui: boot-log ring so the TUI log viewer (and any L1
 * consumer) can page through what lib_log printed, without changing
 * the console output at all.  64 lines cover every kmain stage. ---- */
#define OC_LOG_RING_LINES 64
#define OC_LOG_RING_CAP   96

static char    g_log_ring[OC_LOG_RING_LINES][OC_LOG_RING_CAP];
static int     g_log_ring_count;
static int     g_log_ring_head;

/* fix4: 0 = print everything, 1 = quiet (non-failure stage lines
 * suppressed; FAIL/ERROR/PANIC still get through) */
static int     g_log_level;

static void lib_log_ring_push(const char *line) {
    int slot = g_log_ring_head;
    int n = 0;
    for (; line[n] && n < OC_LOG_RING_CAP - 1; n++)
        g_log_ring[slot][n] = line[n];
    g_log_ring[slot][n] = 0;
    g_log_ring_head = (g_log_ring_head + 1) % OC_LOG_RING_LINES;
    if (g_log_ring_count < OC_LOG_RING_LINES) g_log_ring_count++;
}

void lib_log_init(void) {
    /* timestamps come from the PIT; the ring starts empty */
    g_log_ring_count = 0;
    g_log_ring_head = 0;
    g_log_level = 0;
}

void lib_log_set_level(int level) { g_log_level = (level > 0) ? 1 : 0; }
int  lib_log_get_level(void)      { return g_log_level; }

int lib_log_status_is_failure(const char *status) {
    if (!status) return 0;
    const char *p = status;
    while (*p) {
        if ((p[0] == 'F' || p[0] == 'f') &&
            (p[1] == 'A' || p[1] == 'a') &&
            (p[2] == 'I' || p[2] == 'i') &&
            (p[3] == 'L' || p[3] == 'l')) return 1;
        p++;
    }
    return 0;
}

void lib_log_stage(const char *stage, const char *status) {
    char tmp[128];
    char ts[16];

    /* fix4: quiet level suppresses non-failure stage lines (both the
     * console output AND the TUI ring - the viewer shows the same
     * filter the console shows). */
    if (g_log_level > 0 && !lib_log_status_is_failure(status)) return;

    /* Real timestamp from the PIT (00:00:00.000 before core_timer_init). */
    core_timer_format_hms(core_timer_now_ms(), ts);

    /* Build "[HH:MM:SS.mmm] " */
    tmp[0] = '[';
    strcpy(tmp + 1, ts);
    usize p = 1 + strlen(ts);
    tmp[p++] = ']'; tmp[p++] = ' ';
    tmp[p] = 0;

    screen_console_puts(tmp);
    screen_console_puts(stage);

    usize stage_len = strlen(stage);
    usize dot_count = 50 > stage_len ? (50 - stage_len) : 1;
    for (usize i = 0; i < dot_count; ++i) screen_console_putc('.');
    screen_console_putc(' ');
    screen_console_puts(status);
    screen_console_putc('\n');

    /* mirror into the ring for the TUI log viewer */
    {
        char full[160];
        usize q = 0;
        for (usize i = 0; tmp[i] && q < 40; i++) full[q++] = tmp[i];
        for (usize i = 0; stage[i] && q < 100; i++) full[q++] = stage[i];
        full[q++] = ' ';
        full[q++] = '(';
        for (usize i = 0; status[i] && q < 150; i++) full[q++] = status[i];
        full[q++] = ')';
        full[q] = 0;
        lib_log_ring_push(full);
        /* WP-12: record-only mirror - dmesg replays the boot stages */
        klog_record(lib_log_status_is_failure(status)
                        ? KLOG_ERROR : KLOG_INFO, "boot", full);
    }
}

void lib_log_info(const char *s) {
    screen_console_puts("    ");
    screen_console_puts(s);
    screen_console_putc('\n');
    lib_log_ring_push(s);
    /* WP-12: record-only mirror */
    klog_record(KLOG_INFO, "boot", s);
}

/* WP-10-tui: ring accessors.  Index 0 is the OLDEST retained line. */
int lib_log_ring_count(void) { return g_log_ring_count; }

int lib_log_ring_get(int idx, char *out, int cap) {
    if (!out || cap <= 0 || idx < 0 || idx >= g_log_ring_count) return -1;
    int start = (g_log_ring_count == OC_LOG_RING_LINES)
        ? g_log_ring_head : 0;
    const char *src = g_log_ring[(start + idx) % OC_LOG_RING_LINES];
    int n = 0;
    for (; src[n] && n < cap - 1; n++) out[n] = src[n];
    out[n] = 0;
    return 0;
}
