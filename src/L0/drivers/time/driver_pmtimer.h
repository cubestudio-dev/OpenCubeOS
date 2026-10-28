/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-timer_rtc
 * File: drivers/time/driver_pmtimer.h
 * Purpose: ACPI PM Timer (FADT PM_TMR_BLK) - read, 32-bit wrap handling,
 *          PIT comparison calibration.
 *
 * The PM timer lives at FADT.pm_tmr_blk (SystemIO on PIIX4/QEMU:
 * 0x0608).  The counter increments at 3.579545 MHz (nominal).
 *
 * Effective counter width: ACPI 1.0 defined PM_TMR as 24-bit and the
 * PIIX4/ICH family implements exactly that (QEMU 10.0 masks the read
 * to 0xffffff; Linux uses the same ACPI_PM_MASK for its acpi_pm
 * clocksource).  The FADT field says 32-bit (pm_tmr_len = 4), which is
 * the ACPI 2.0+ envelope.  This driver therefore masks deltas to 24
 * bits by default - that arithmetic is correct on BOTH 24-bit and
 * 32-bit hardware as long as a measurement window is shorter than one
 * 24-bit wrap period (2^24 / 3.579545 MHz = 4.67 s), which the
 * calibration window (~1.1 s) and every delta use here are.  True
 * 32-bit hardware (TMR_VAL_EXT) can be unmasked with the runtime
 * config key pmtimer.width=32 (see docs/CONFIG.md).
 *
 * A 64-bit extension tracks full wraps for longer spans; reads must be
 * less than one wrap period apart (4.67 s at 24-bit) to keep the
 * accounting exact.  Calibration compares the PM timer against the PIT
 * tick clock (core_timer_now_ms) - the frequency is not recorded
 * anywhere in the FADT, so it has to be measured.
 *
 * All names/strings are ASCII by project rule.
 */
#ifndef OC_DRIVER_PMTIMER_H
#define OC_DRIVER_PMTIMER_H

#include "types.h"

/* Nominal PM timer frequency (ACPI-fixed 3.579545 MHz). */
#define OC_PM_TIMER_NOMINAL_HZ 3579545u

/* Default delta mask: 24 bits (PIIX4/ACPI 1.0/QEMU/Linux convention).
 * pmtimer.width=32 in /etc/opencube.conf switches to a 32-bit mask for
 * hardware that implements TMR_VAL_EXT. */
#define OC_PM_TIMER_MASK24 0xFFFFFFu
#define OC_PM_TIMER_MASK32 0xFFFFFFFFu

/* 1 when the FADT carried a usable PM_TMR_BLK. */
int  driver_pmtimer_init(void);
int  driver_pmtimer_available(void);

/* Raw 32-bit counter read (wraps modulo 2^32). */
u32  driver_pmtimer_read(void);

/* Monotonic 64-bit view: extends the raw counter across wraps.
 * Reads must be less than one wrap period apart (~20 min) to keep the
 * wrap accounting exact; the calibration and test windows are far
 * below that. */
u64  driver_pmtimer_now64(void);

/* Wrap counter observed so far by driver_pmtimer_now64(). */
u64  driver_pmtimer_wrap_count(void);

/* Base/space/width of the resolved PM_TMR_BLK GAS (for status views). */
u64  driver_pmtimer_base(void);
int  driver_pmtimer_space(void);      /* 0 = SystemMemory, 1 = SystemIO */
int  driver_pmtimer_bit_width(void);
int  driver_pmtimer_is_extended(void);/* 1 = X_PM_TMR, 0 = pm_tmr_blk */

/* Effective delta mask (OC_PM_TIMER_MASK24 by default, 32-bit when
 * pmtimer.width=32 in the config). */
u32  driver_pmtimer_mask(void);
/* The configured width: 24 (default) or 32. */
int  driver_pmtimer_config_width(void);

/* ---- calibration vs PIT ------------------------------------------------
 * Samples the PM timer around a ~1.1 s PIT-bounded window and stores
 * the measured frequency.  Returns 0 on success (also caches it for
 * driver_pmtimer_hz_measured()). */
int  driver_pmtimer_calibrate(u64 *hz_out);

/* Measured frequency (0 = not calibrated yet). */
u32  driver_pmtimer_hz_measured(void);

/* Elapsed milliseconds between two RAW 32-bit samples, wrap-safe and
 * based on the measured frequency (0 when uncalibrated). */
u64  driver_pmtimer_elapsed_ms(u32 start, u32 end);

/* ---- wrap arithmetic (test-visible production code) --------------------
 * The exact functions the driver uses, exposed so the shell `pmtimer
 * wrap` test can drive them with injected fixed values (the same
 * pattern as OC_G8_RTC_DECODE for the RTC decoder). */

/* Modular counter delta: ticks from `start` to `end`, correct across
 * one wrap of the EFFECTIVE counter width (see the mask notes above). */
u32  driver_pmtimer_delta32(u32 start, u32 end);

/* Extend a raw sample into the 64-bit view given the previous raw
 * sample and the wrap counter so far.  Returns the new wrap count and
 * stores the extended value in *out64. */
u64  driver_pmtimer_extend(u32 prev_raw, u32 cur_raw, u64 wraps, u64 *out64);

/* Raw inputs of the LAST calibration (for transparent status output):
 * PIT milliseconds at start/end and the two PM-timer samples. */
u64  driver_pmtimer_cal_ms0(void);
u64  driver_pmtimer_cal_ms1(void);
u32  driver_pmtimer_cal_c0(void);
u32  driver_pmtimer_cal_c1(void);

#endif /* OC_DRIVER_PMTIMER_H */
