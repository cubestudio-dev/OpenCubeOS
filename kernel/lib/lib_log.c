/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-02
 * File: kernel/log.c
 *
 * WP-02: lib_log_stage() now uses real timestamps from core_timer_now_ms().
 * Before the PIT is up (timer not initialized), timestamps read 0 which
 * is correct - the boot log before timer init shows [00:00:00.000].
 */
#include "lib_log.h"
#include "screen_console.h"
#include "lib_string.h"
#include "core_timer.h"

void lib_log_init(void) {
    /* Nothing to init in WP-02 - timestamps come from the PIT. */
}

void lib_log_stage(const char *stage, const char *status) {
    char tmp[128];
    char ts[16];

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
}

void lib_log_info(const char *s) {
    screen_console_puts("    ");
    screen_console_puts(s);
    screen_console_putc('\n');
}
