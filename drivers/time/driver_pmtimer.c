/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-timer_rtc
 * File: drivers/time/driver_pmtimer.c
 * Purpose: ACPI PM Timer: FADT PM_TMR_BLK read (SystemIO or
 *          SystemMemory), 32-bit wrap-safe arithmetic, 64-bit wrap
 *          extension, PIT comparison calibration.
 */
#include "driver_pmtimer.h"
#include "driver_acpi.h"
#include "core_timer.h"
#include "core_sched.h"
#include "lib_config.h"

#include <stdint.h>

static acpi_gas_t g_gas;          /* resolved PM_TMR_BLK address */
static int        g_present = 0;
static int        g_extended = 0;/* 1 = X_PM_TMR, 0 = pm_tmr_blk */
static u32        g_hz = 0;      /* measured frequency */
static u32        g_mask = OC_PM_TIMER_MASK24;
static int        g_cfg_width = 24;
static u32        g_prev_raw = 0;
static u64        g_wraps = 0;
static int        g_have_prev = 0;
static u64        g_cal_ms0 = 0, g_cal_ms1 = 0;
static u32        g_cal_c0 = 0, g_cal_c1 = 0;

int driver_pmtimer_init(void) {
    g_present = 0;
    const acpi_fadt_t *fadt = driver_acpi_fadt();
    if (!fadt) return -1;
    if (driver_acpi_fadt_is_extended() &&
        fadt->x_pm_tmr.address != 0) {
        g_gas = fadt->x_pm_tmr;
        g_extended = 1;
    } else {
        if (fadt->pm_tmr_blk == 0) return -1;
        g_gas.address_space = 1;            /* SystemIO */
        g_gas.bit_width     = (u8)(fadt->pm_tmr_len ? fadt->pm_tmr_len * 8 : 32);
        g_gas.bit_offset    = 0;
        g_gas.access_size   = 0;
        g_gas.address       = fadt->pm_tmr_blk;
        g_extended = 0;
    }
    if (g_gas.address == 0) return -1;
    if (g_gas.address_space != 0 && g_gas.address_space != 1) return -1;
    /* Runtime width override (hda.bdl precedent): pmtimer.width=32
     * unmasks the counter for hardware with TMR_VAL_EXT. */
    {
        char v[16];
        if (lib_config_read("pmtimer.width", v, (int)sizeof(v)) > 0 &&
            v[0] == '3' && v[1] == '2') {
            g_mask = OC_PM_TIMER_MASK32;
            g_cfg_width = 32;
        }
    }
    g_present = 1;
    g_prev_raw = driver_pmtimer_read();
    g_have_prev = 1;
    return 0;
}

int  driver_pmtimer_available(void)  { return g_present; }
u64  driver_pmtimer_base(void)       { return g_gas.address; }
int  driver_pmtimer_space(void)      { return g_gas.address_space; }
int  driver_pmtimer_bit_width(void)  { return g_gas.bit_width; }
int  driver_pmtimer_is_extended(void){ return g_extended; }
u32  driver_pmtimer_mask(void)       { return g_mask; }
int  driver_pmtimer_config_width(void) { return g_cfg_width; }

u32 driver_pmtimer_read(void) {
    return (u32)driver_acpi_gas_read(&g_gas, 4);
}

/* ---- wrap arithmetic ---------------------------------------------------- */

u32 driver_pmtimer_delta32(u32 start, u32 end) {
    /* Modular delta masked to the effective counter width: correct
     * across one wrap on 24-bit (default) and 32-bit hardware alike,
     * as long as the elapsed window is shorter than one wrap period. */
    return (u32)((end - start) & g_mask);
}

u64 driver_pmtimer_extend(u32 prev_raw, u32 cur_raw, u64 wraps, u64 *out64) {
    u32 prev = prev_raw & g_mask;
    u32 cur  = cur_raw & g_mask;
    if (cur < prev) wraps++;   /* counter wrapped between reads */
    if (out64) *out64 = (wraps << g_cfg_width) | (u64)cur;
    return wraps;
}

u64 driver_pmtimer_now64(void) {
    if (!g_present) return 0;
    u32 cur = driver_pmtimer_read() & g_mask;
    if (g_have_prev) {
        g_wraps = driver_pmtimer_extend(g_prev_raw, cur, g_wraps, 0);
    }
    g_prev_raw = cur;
    g_have_prev = 1;
    return (g_wraps << g_cfg_width) | (u64)cur;
}

u64 driver_pmtimer_wrap_count(void) { return g_wraps; }

/* ---- calibration --------------------------------------------------------- */

int driver_pmtimer_calibrate(u64 *hz_out) {
    if (!g_present) return -1;
    u64 ms0 = core_timer_now_ms();
    u32 c0 = driver_pmtimer_read();
    u64 ms;
    do {
        core_sched_yield();
        ms = core_timer_now_ms();
    } while (ms - ms0 < 1100);          /* PIT-bounded ~1.1 s window */
    u32 c1 = driver_pmtimer_read();
    u32 ticks = driver_pmtimer_delta32(c0, c1);
    if (ms <= ms0) return -1;
    g_cal_ms0 = ms0;
    g_cal_ms1 = ms;
    g_cal_c0 = c0;
    g_cal_c1 = c1;
    u64 hz = (u64)ticks * 1000ull / (ms - ms0);
    if (hz == 0) return -1;
    g_hz = (u32)hz;
    if (hz_out) *hz_out = hz;
    return 0;
}

/* Raw calibration sample probes (0 when never calibrated).  Exposed so
 * the shell can print the exact inputs of the last calibration. */
u64 driver_pmtimer_cal_ms0(void) { return g_cal_ms0; }
u64 driver_pmtimer_cal_ms1(void) { return g_cal_ms1; }
u32 driver_pmtimer_cal_c0(void)  { return g_cal_c0; }
u32 driver_pmtimer_cal_c1(void)  { return g_cal_c1; }

u32 driver_pmtimer_hz_measured(void) { return g_hz; }

u64 driver_pmtimer_elapsed_ms(u32 start, u32 end) {
    if (g_hz == 0) return 0;
    u32 ticks = driver_pmtimer_delta32(start, end);
    return (u64)ticks * 1000ull / g_hz;
}
