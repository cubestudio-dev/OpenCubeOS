/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-timer_rtc
 * File: drivers/time/driver_tsc.c
 * Purpose: TSC reads + calibration against PIT / HPET / PM timer.
 */
#include "driver_tsc.h"
#include "driver_hpet.h"
#include "driver_pmtimer.h"
#include "core_timer.h"
#include "core_sched.h"

#include <stdint.h>

static u64         g_hz = 0;
static const char *g_source = "none";

u64 driver_tsc_read(void) {
    u32 lo, hi;
    /* cpuid serializes the pipeline so rdtsc is not reordered ahead of
     * the code that means to bracket it. */
    __asm__ volatile("cpuid" : : : "rax", "rbx", "rcx", "rdx");
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((u64)hi << 32) | (u64)lo;
}

int driver_tsc_invariant(void) {
    u32 a, b, c, d;
    __asm__ volatile("cpuid"
                     : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
                     : "a"(0x80000007u));
    return (d >> 8) & 1;
}

/* ---- PIT reference ------------------------------------------------------ */

int driver_tsc_calibrate_pit(u64 *hz_out) {
    u64 ms0 = core_timer_now_ms();
    u64 t0 = driver_tsc_read();
    u64 ms;
    do {
        core_sched_yield();
        ms = core_timer_now_ms();
    } while (ms - ms0 < 1100);
    u64 t1 = driver_tsc_read();
    if (t1 <= t0 || ms <= ms0) return -1;
    u64 hz = (t1 - t0) * 1000ull / (ms - ms0);
    if (hz == 0) return -1;
    if (hz_out) *hz_out = hz;
    return 0;
}

/* ---- HPET reference ------------------------------------------------------ */

int driver_tsc_calibrate_hpet(u64 *hz_out) {
    if (!driver_hpet_available()) return -1;
    u64 hpet_hz = driver_hpet_freq_hz();
    if (hpet_hz == 0) return -1;
    /* The main counter is frozen while GEN ENABLE_CNF is clear (the
     * boot default); measuring against a frozen counter would spin
     * forever, so enable it for the window and restore. */
    int was_enabled = driver_hpet_ensure_enabled();
    u64 c0 = driver_hpet_counter_read();
    u64 t0 = driver_tsc_read();
    u64 need = hpet_hz / 10;                 /* 0.1 s of HPET ticks */
    u64 c1 = c0;
    while (c1 - c0 < need) {
        c1 = driver_hpet_counter_read();
        __asm__ volatile("pause");
    }
    u64 t1 = driver_tsc_read();
    driver_hpet_restore_enabled(was_enabled);
    u64 dticks = c1 - c0;
    if (t1 <= t0 || dticks == 0) return -1;
    u64 hz = (t1 - t0) * hpet_hz / dticks;
    if (hz == 0) return -1;
    if (hz_out) *hz_out = hz;
    return 0;
}

/* ---- ACPI PM timer reference --------------------------------------------- */

int driver_tsc_calibrate_pmtimer(u64 *hz_out) {
    if (!driver_pmtimer_available()) return -1;
    u64 pmt_hz = driver_pmtimer_hz_measured();
    if (pmt_hz == 0) return -1;              /* needs a PIT calibration */
    u32 p0 = driver_pmtimer_read();
    u64 t0 = driver_tsc_read();
    u32 need = (u32)(pmt_hz / 2);            /* 0.5 s < one 24-bit wrap */
    u32 p1 = p0;
    while (driver_pmtimer_delta32(p0, p1) < need) {
        p1 = driver_pmtimer_read();
        __asm__ volatile("pause");
    }
    u64 t1 = driver_tsc_read();
    u32 dticks = driver_pmtimer_delta32(p0, p1);
    if (t1 <= t0 || dticks == 0) return -1;
    u64 hz = (t1 - t0) * pmt_hz / dticks;
    if (hz == 0) return -1;
    if (hz_out) *hz_out = hz;
    return 0;
}

/* ---- cached calibration --------------------------------------------------- */

static void cache_calibration(u64 hz, const char *source) {
    g_hz = hz;
    g_source = source;
}

int driver_tsc_calibrated(u64 *hz_out, const char **source_out) {
    if (g_hz == 0) return -1;
    if (hz_out) *hz_out = g_hz;
    if (source_out) *source_out = g_source;
    return 0;
}

const char* driver_tsc_source(void) { return g_source; }

/* Cache the best available calibration: HPET first (most precise),
 * then the PM timer, then the PIT. */
void driver_tsc_autocalibrate(void) {
    u64 hz;
    if (driver_tsc_calibrate_hpet(&hz) == 0) {
        cache_calibration(hz, "hpet");
        return;
    }
    if (driver_tsc_calibrate_pmtimer(&hz) == 0) {
        cache_calibration(hz, "pmtimer");
        return;
    }
    if (driver_tsc_calibrate_pit(&hz) == 0) {
        cache_calibration(hz, "pit");
    }
}

/* ---- TSC-driven delay ------------------------------------------------------ */

int driver_tsc_delay_ms(u64 ms, u64 *actual_ms) {
    if (g_hz == 0) driver_tsc_autocalibrate();
    if (g_hz == 0) return -1;
    if (ms == 0) ms = 1;
    u64 t0 = driver_tsc_read();
    u64 ms0 = core_timer_now_ms();
    u64 need = ms * g_hz / 1000ull;
    while (driver_tsc_read() - t0 < need) {
        __asm__ volatile("pause");
    }
    u64 ms1 = core_timer_now_ms();
    if (actual_ms) *actual_ms = ms1 - ms0;
    return 0;
}
