/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-timer_rtc
 * File: drivers/time/driver_cmos_rtc.h
 * Purpose: CMOS RTC driver - status registers + RTC interrupts on IRQ8
 *          (update-ended / periodic) and the alarm interrupt.
 *
 * The wall-clock reader itself lives in kernel/core/core_rtc.c (WP-09,
 * reused unchanged).  This driver adds the parts WP-09 did not need:
 *   - status register reporting (Reg A/B/C/D: UIP, rate, PIE/AIE/UIE,
 *     BCD/binary, 12/24-hour, VRT)
 *   - an IRQ8 handler that counts update-ended / periodic / alarm fires
 *   - three interrupt tests driven from shell commands:
 *       UIE  - one update-ended interrupt per second
 *       PIE  - periodic interrupts at 32768>>(rate-1) Hz
 *       AIE  - alarm interrupt at a time-of-day match
 *
 * IRQ8 is wired through the shared PIC chain table (arch_irq.c), so the
 * RTC handler coexists with any other IRQ8 user.  Handlers only count;
 * the shell command polls the counters with core_timer_now_ms() bounds,
 * so nothing in IRQ context ever waits.
 *
 * All names/strings are ASCII by project rule.
 */
#ifndef OC_DRIVER_CMOS_RTC_H
#define OC_DRIVER_CMOS_RTC_H

#include "types.h"

/* Decoded status-register snapshot. */
typedef struct driver_cmos_rtc_status {
    u8  reg_a;        /* raw status register A */
    u8  reg_b;        /* raw status register B */
    u8  reg_c;        /* raw status register C (drained before the read) */
    u8  reg_d;        /* raw status register D */
    int uip;          /* 1 = update in progress (Reg A bit 7) */
    int bcd;          /* 1 = BCD data mode (Reg B DM bit 2 = 0) */
    int hour24;       /* 1 = 24-hour mode (Reg B bit 1) */
    int pie;          /* periodic interrupt enabled (Reg B bit 6) */
    int aie;          /* alarm interrupt enabled (Reg B bit 5) */
    int uie;          /* update-ended interrupt enabled (Reg B bit 4) */
    int vrt;          /* valid RAM/time (Reg D bit 7) */
    int rate_hz;      /* decoded periodic rate from Reg A rate[3:0] (0 = off/invalid) */
    int fadt_century; /* FADT century register index (0 = none reported) */
} driver_cmos_rtc_status_t;

/* Probe the CMOS RTC and snapshot the static status registers.
 * Returns 0 when the device answers (VRT observed), -1 otherwise. */
int driver_cmos_rtc_init(void);

/* 1 after a successful driver_cmos_rtc_init(). */
int driver_cmos_rtc_available(void);

/* Fill a fresh status snapshot.  Never fails. */
void driver_cmos_rtc_status(driver_cmos_rtc_status_t *out);

/* ---- interrupt tests --------------------------------------------------
 * Each test drives the REAL interrupt line: it installs a handler on
 * IRQ8, arms the requested interrupt source in the hardware, polls the
 * handler counters under a core_timer_now_ms() deadline, then restores
 * the previous Reg B and uninstalls the handler.  Return values are the
 * observed fire counts (a returned value smaller than the expectation
 * means the test missed deadline - the shell reports it honestly). */

/* Enable the update-ended interrupt and wait for `seconds` fires.
 * Expectation: one fire per second, so `seconds` <= 4 keeps the run
 * bounded. Returns the observed count. */
int driver_cmos_rtc_uie_test(int seconds, u64 timeout_ms);

/* Enable the periodic interrupt at rate index `rate` (3..15, frequency
 * 32768>>(rate-1) Hz) and wait for `count` fires. Returns the observed
 * count.  Rate indexes 0-2 are undefined by the spec and rejected. */
int driver_cmos_rtc_pie_test(int rate, int count, u64 timeout_ms);

/* Program the alarm `ahead_s` seconds ahead of the current wall clock
 * and wait for the alarm interrupt. Returns 1 when it fired, 0 on
 * timeout, -1 when the target would cross midnight (skipped honestly). */
int driver_cmos_rtc_alarm_test(int ahead_s, u64 timeout_ms);

/* Handler counters (monotonic; IRQ8 must be armed by a test to move). */
u64 driver_cmos_rtc_irq_total(void);
u64 driver_cmos_rtc_irq_update(void);
u64 driver_cmos_rtc_irq_periodic(void);
u64 driver_cmos_rtc_irq_alarm(void);

/* Raw register access for the shell status view (no side effects on
 * Reg B; Reg C is drain-on-read so only the status view touches it). */
u8 driver_cmos_rtc_read_reg(u8 reg);
void driver_cmos_rtc_write_reg(u8 reg, u8 value);

#endif /* OC_DRIVER_CMOS_RTC_H */
