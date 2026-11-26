/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-timer_rtc
 * File: drivers/time/driver_tsc.h
 * Purpose: TSC (rdtsc) as a high-precision time source - serialized
 *          reads, invariant-TSC probe, and calibration against the
 *          PIT / HPET / ACPI PM timer.
 *
 * The TSC has no firmware-declared frequency: it has to be measured.
 * Three independent references are provided:
 *   PIT     - core_timer_now_ms() (100 Hz tick, +-1% over ~1.1 s)
 *   HPET    - main counter (100 MHz in QEMU, needs an init'd HPET)
 *   PMT     - ACPI PM timer (needs a PIT-calibrated PM timer)
 * The first successful calibration is cached and used for the
 * TSC-driven delay; the shell command can re-calibrate against any
 * reference and compare the results.
 *
 * All names/strings are ASCII by project rule.
 */
#ifndef OC_DRIVER_TSC_H
#define OC_DRIVER_TSC_H

#include "types.h"

/* Serialized TSC read: cpuid fences the pipeline, then rdtsc. */
u64  driver_tsc_read(void);

/* 1 when CPUID 0x80000007.EDX[8] (Invariant TSC) is set. */
int  driver_tsc_invariant(void);

/* ---- calibration against the three references -------------------------- */

/* Measure the TSC frequency over a ~1.1 s PIT-bounded window.
 * Returns 0 and stores the measured Hz. */
int  driver_tsc_calibrate_pit(u64 *hz_out);

/* Measure against the HPET main counter over a ~0.1 s window.
 * Returns 0 on success, -1 when the HPET is unavailable. */
int  driver_tsc_calibrate_hpet(u64 *hz_out);

/* Measure against the ACPI PM timer over a ~0.5 s window (inside one
 * 24-bit wrap period).  Returns 0 on success, -1 when the PM timer is
 * not calibrated yet. */
int  driver_tsc_calibrate_pmtimer(u64 *hz_out);

/* WP-compat-and-security-hardening B3: measure against the RTC 1024 Hz
 * system tick over 32 ticks (~31 ms).  Fast + available right after
 * core_timer_init() (before HPET/PM timer come up), which is exactly
 * when the storage drivers need their wall-clock deadlines.  Returns 0
 * on success, -1 when the system tick is not on the RTC. */
int  driver_tsc_calibrate_rtc(u64 *hz_out);

/* Cached calibration: stores the frequency + reference name
 * ("pit"/"hpet"/"pmtimer").  Returns 0 when a calibration is cached,
 * -1 when none exists yet. */
int  driver_tsc_calibrated(u64 *hz_out, const char **source_out);
const char* driver_tsc_source(void);

/* Calibrate once against the best available reference (HPET, then PM
 * timer, then PIT) and cache the result.  Safe to call again. */
void driver_tsc_autocalibrate(void);

/* Busy-wait `ms` milliseconds using the TSC (requires a cached
 * calibration; calibrates via PIT as a fallback).  Reports the
 * PIT-measured actual elapsed time.  Returns 0 on success. */
int  driver_tsc_delay_ms(u64 ms, u64 *actual_ms);

/* WP-severe-fix-and-closure B-vmx1: microsecond pause with ZERO port
 * I/O - a pure rdtsc/`pause` spin.  Under VMware every inb/outb is a
 * VM exit (1-3 us of host time), so an un-paced poll loop does
 * millions of exits per second; pacing the polls with THIS spin (the
 * TSC read and the pause instruction never trap) cuts the exit rate
 * by ~1000x while the polled latency stays in the same range.  When
 * the TSC is uncalibrated the fallback is a bounded pure-CPU spin
 * (still zero port I/O - approximate time, correct behavior). */
void driver_tsc_pause_us(u64 us);

#endif /* OC_DRIVER_TSC_H */
