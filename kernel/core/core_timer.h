/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-02
 * File: kernel/timer.h
 * Purpose: 8253/8254 PIT @ 100 Hz + system tick counter + soft timers.
 *
 * The PIT is the only hardware timer we use in WP-02. The HPET/local APIC
 * timer are left for a later WP. The PIT fires IRQ0 at 100 Hz; each fire:
 *   - increments g_tick_count
 *   - updates g_boot_time_ms (from tick count ? 10)
 *   - runs any registered soft-timer callbacks whose deadline has passed
 *
 * Real timestamps: core_timer_now_ms() returns milliseconds since boot,
 * derived from g_tick_count ? 10. This replaces the WP-01 placeholder
 * counter in log.c.
 */
#ifndef OC_TIMER_H
#define OC_TIMER_H

#include "types.h"
#include "arch_idt.h"   /* for arch_irq_frame_t */

#define OC_TIMER_HZ 100        /* PIT frequency */
#define OC_TIMER_MS_PER_TICK (1000 / OC_TIMER_HZ)

void core_timer_init(void);

/* IRQ0 handler (called from arch_irq_dispatch). */
void core_timer_irq_handler(void *ctx, arch_irq_frame_t *f);

/* Monotonic time since timer init, in milliseconds. */
u64 core_timer_now_ms(void);

/* Total tick count since timer init. */
u64 core_timer_ticks(void);

/* Format a timestamp into HH:MM:SS.mmm (12 bytes + null). */
void core_timer_format_hms(u64 ms, char out[16]);

/* ---- L1 extension: soft timers ----
 *
 * Register a callback to be called every `interval_ms` milliseconds
 * (periodic) or once after `interval_ms` milliseconds (one-shot).
 *
 * Returns a timer id >= 0 on success, negative on error.
 *
 * The callback runs in IRQ context (IRQ0). It must be fast and non-blocking.
 */
typedef void (*core_timer_cb_fn)(void *ctx);

/* Periodic: fires every `interval_ms` until cancelled. */
int core_timer_register_periodic(core_timer_cb_fn fn, void *ctx, u64 interval_ms);
/* One-shot: fires once after `delay_ms`, then auto-cancels. */
int core_timer_register_oneshot(core_timer_cb_fn fn, void *ctx, u64 delay_ms);
/* Cancel a timer by id. Returns 0 on success. */
int core_timer_cancel(int id);

#endif /* OC_TIMER_H */
