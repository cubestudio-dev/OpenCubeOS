/* SPDX-License-Identifier: Apache-2.0 */
/* Open Cube OS - WP-02
 * File: kernel/log.c
 *
 * WP-02: log_stage() now uses real timestamps from oc_timer_now_ms().
 * Before the PIT is up (timer not initialized), timestamps read 0 which
 * is correct - the boot log before timer init shows [00:00:00.000].
 */
#include "log.h"
#include "console.h"
#include "string.h"
#include "timer.h"

void oc_log_init(void) {
    /* Nothing to init in WP-02 - timestamps come from the PIT. */
}

void oc_log_stage(const char *stage, const char *status) {
    char tmp[128];
    char ts[16];

    /* Real timestamp from the PIT (00:00:00.000 before timer_init). */
    oc_timer_format_hms(oc_timer_now_ms(), ts);

    /* Build "[HH:MM:SS.mmm] " */
    tmp[0] = '[';
    oc_strcpy(tmp + 1, ts);
    usize p = 1 + oc_strlen(ts);
    tmp[p++] = ']'; tmp[p++] = ' ';
    tmp[p] = 0;

    oc_console_puts(tmp);
    oc_console_puts(stage);

    usize stage_len = oc_strlen(stage);
    usize dot_count = 50 > stage_len ? (50 - stage_len) : 1;
    for (usize i = 0; i < dot_count; ++i) oc_console_putc('.');
    oc_console_putc(' ');
    oc_console_puts(status);
    oc_console_putc('\n');
}

void oc_log_info(const char *s) {
    oc_console_puts("    ");
    oc_console_puts(s);
    oc_console_putc('\n');
}
