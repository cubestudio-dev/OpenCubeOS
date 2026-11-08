/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS NAK - kernel C entry point (WP-01-tree).
 *
 * Skeleton scope only: bring up the two boot console outputs (the
 * VGA text screen and the COM1 UART), print the four-line boot
 * banner on BOTH outputs and halt. No memory management, no
 * interrupts, no further drivers - those arrive in later work
 * packages.
 *
 * The banner a user sees on a real machine is the VGA copy; the
 * serial copy is what a headless boot logs. Both sinks receive
 * byte-identical text through core_main_banner_puts().
 *
 * CORE_MAIN_BUILD_TIMESTAMP is provided by ocos/Makefile and derives
 * from SOURCE_DATE_EPOCH (already converted to UTC+8 by the
 * Makefile), which is what makes the banner reproducible.
 *
 * Naming: [class]_[category]_[role] - core (kernel core), main
 * (kernel main module), entry/version/arch/build stamp.
 */

#include <stdint.h>
#include <stddef.h>

#include "drivers/drv_serial_16550.h"
#include "drivers/drv_video_vga.h"

#ifndef CORE_MAIN_BUILD_TIMESTAMP
#define CORE_MAIN_BUILD_TIMESTAMP "unknown"
#endif

#define CORE_MAIN_VERSION "WP-01-tree"
#define CORE_MAIN_ARCH    "x86_64"

/* One banner line to both sinks: the serial log and the screen show
 * byte-identical text. */
static void core_main_banner_puts(const char *s)
{
    drv_serial_puts(s);
    drv_video_puts(s);
}

void core_main_entry(uint32_t mb2_magic, uint32_t mb2_info_addr)
{
    (void)mb2_magic;
    (void)mb2_info_addr;

    drv_serial_init();
    drv_video_init();

    core_main_banner_puts("Open Cube OS NAK kernel booted\n");
    core_main_banner_puts("Version: " CORE_MAIN_VERSION "\n");
    core_main_banner_puts("Arch: " CORE_MAIN_ARCH "\n");
    core_main_banner_puts("Build: " CORE_MAIN_BUILD_TIMESTAMP "\n");

    drv_video_sync_cursor();

    for (;;) {
        __asm__ __volatile__("hlt");
    }
}
