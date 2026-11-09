/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP03 - services/display: kernel-side display op
 * bodies (stats / uname / clear), rescued verbatim out of core/main.c.
 *
 * The two timer-view counters (soft one-second heartbeat fires, oneshot
 * callback fires) used to live next to the oc> shell's test commands;
 * the stats op surface prints them, so they move WITH the op body.
 * kmain registers the periodic heartbeat (see kmain, step: display
 * service probes) so "soft=" keeps counting exactly as before.
 */
#include "disp_cmds.h"
#include "lib_string.h"
#include "screen_console.h"
#include "core_timer.h"
#include "arch_idt.h"
#include "version_gen.h"

/* one-second soft-timer heartbeat counter (fed by kmain's periodic
 * registration; view-only input of the stats op) */
u64 g_disp_soft_timer_fires = 0;
void disp_soft_timer_heartbeat(void *ctx) {
    (void)ctx;
    g_disp_soft_timer_fires++;
}

/* oneshot-callback counter: 0 unless some kernel-side oneshot test
 * fires; kept so the stats op output shape is unchanged. */
u64 g_disp_oneshot_fires = 0;

int disp_cmd_stats(const char *args) {
    (void)args;
    char buf[120]; char n[20];
    strcpy(buf, "ticks="); u64_to_str(core_timer_ticks(), n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), "  ms="); u64_to_str(core_timer_now_ms(), n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), "  IRQ0="); u64_to_str(arch_irq_counts[OC_IRQ0_VECTOR], n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), "  IRQ1="); u64_to_str(arch_irq_counts[OC_IRQ1_VECTOR], n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), "  soft="); u64_to_str(g_disp_soft_timer_fires, n); strcpy(buf+strlen(buf), n);
    strcpy(buf+strlen(buf), "  one="); u64_to_str(g_disp_oneshot_fires, n); strcpy(buf+strlen(buf), n);
    screen_console_puts(buf); screen_console_putc('\n');
    return 0;
}

int disp_cmd_uname(const char *args) {
    int show_all = 0, show_s = 0, show_r = 0, show_m = 0;
    if (args && args[0] == '-' && args[1]) {
        for (int i = 1; args[i] && args[i] != ' '; i++) {
            if (args[i] == 'a') show_all = 1;
            else if (args[i] == 's') show_s = 1;
            else if (args[i] == 'r') show_r = 1;
            else if (args[i] == 'm') show_m = 1;
        }
    } else {
        show_s = 1;  /* default */
    }
    if (show_all) {
        screen_console_puts("Open Cube OS " OC_RELEASE_VERSION " x86_64\n");
        return 0;
    }
    if (show_s) screen_console_puts("Open Cube OS\n");
    if (show_r) screen_console_puts(OC_RELEASE_VERSION "\n");
    if (show_m) screen_console_puts("x86_64\n");
    return 0;
}

int disp_cmd_clear(const char *args) {
    (void)args;
    screen_console_clear();
    return 0;
}
