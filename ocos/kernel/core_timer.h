/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-02 / WP-compat-and-security-hardening B4
 * File: kernel/core/core_timer.h
 * Purpose: system tick + soft timers - RTC-first, PIT fallback.
 *
 * WP-compat-and-security-hardening B4: the default heartbeat is the
 * CMOS RTC periodic interrupt (1024 Hz on IRQ8, fanned out by the
 * single RegC-owning handler in the RTC driver - RegC is
 * read-to-clear, so exactly one reader).  The PIT path below is the
 * FALLBACK: it runs only when the RTC hardware does not answer, or
 * when oc.tick=pit forces it (the acceptance path for "RTC broken ->
 * degrade to PIT").  Either source drives the same soft timers +
 * the preemptive scheduler; the 1024 Hz tick is converted to whole
 * milliseconds with a half-ns accumulator (zero drift).
 */
#ifndef OC_TIMER_H
#define OC_TIMER_H

#include "types.h"
#include "arch_idt.h"   /* for arch_irq_frame_t */

#define OC_TIMER_HZ 100        /* PIT frequency (fallback path) */
#define OC_TIMER_MS_PER_TICK (1000 / OC_TIMER_HZ)

/* cmdline may carry oc.tick=rtc|pit (default: auto = RTC wins when
 * the hardware answers; PIT only as the fallback). */
void core_timer_init(const char *cmdline);

/* WP-10-chinese_support-fix2: reprogram the PIT after an ACPI S3
 * resume WITHOUT wiping the soft-timer table. core_timer_init() memsets
 * g_timers and zeroes the tick/boot counters, which silently killed the
 * 10 ms network poll timer (and every other registered timer) after
 * every suspend -> the resumed system could not receive a single
 * packet. The table lives in RAM and survives S3, so only the PIT
 * itself needs reprogramming. */
void core_timer_resume(void);

/* IRQ0 handler (called from arch_irq_dispatch - PIT fallback path). */
void core_timer_irq_handler(void *ctx, arch_irq_frame_t *f);

/* WP-compat-and-security-hardening B4: the RTC tick consumer (called
 * from the single RegC-owning IRQ8 handler in drv_time_cmos_rtc.c). */
void core_timer_rtc_tick(u8 regc);

/* "rtc" or "pit" - which source owns the system tick (diagnostics +
 * the acceptance test assert on this). */
const char *core_timer_source(void);

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
