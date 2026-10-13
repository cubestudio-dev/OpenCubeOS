/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-timer_rtc
 * File: drivers/time/driver_cmos_rtc.c
 * Purpose: CMOS RTC interrupts (IRQ8): update-ended, periodic, alarm.
 *
 * Register map (MC146818 / PIIX4, ports 0x70 index + 0x71 data):
 *   Reg A: bit7 UIP, bits[3:0] periodic rate index
 *   Reg B: bit7 SET, bit6 PIE, bit5 AIE, bit4 UIE,
 *          bit2 DM (1 = binary), bit1 24h, bit0 DSE
 *   Reg C: bit7 IRQF, bit6 PF, bit5 AF, bit4 UF (clear-on-read)
 *   Reg D: bit7 VRT
 *
 * Every test follows the same discipline:
 *   1. drain Reg C (it is clear-on-read, pending IRQF is discarded)
 *   2. install the counting handler on IRQ8 (shared chain, arch_irq.c)
 *   3. arm the source in the hardware (Reg A/B/alarm regs)
 *   4. poll the counters under a core_timer_now_ms() deadline
 *   5. restore the previous Reg A/B, uninstall the handler
 * so a failed or interrupted test always leaves the RTC in its prior
 * state and never leaves an IRQ8 handler dangling.
 */
#include "driver_cmos_rtc.h"
#include "driver_acpi.h"
#include "arch_irq.h"
#include "core_rtc.h"
#include "core_timer.h"
#include "core_sched.h"
#include "lib_string.h"

#include <stdint.h>

static inline void cmos_outb(u16 p, u8 v) {
    __asm__ volatile("outb %0, %1" :: "a"(v), "Nd"(p));
}
static inline u8 cmos_inb(u16 p) {
    u8 v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(p));
    return v;
}

static int  g_present = 0;
static int  g_vrt = 0;
static int  g_fadt_century = 0;
static u64  g_cnt_total = 0;
static u64  g_cnt_update = 0;
static u64  g_cnt_periodic = 0;
static u64  g_cnt_alarm = 0;

u8 driver_cmos_rtc_read_reg(u8 reg) {
    cmos_outb(0x70, reg);
    return cmos_inb(0x71);
}

void driver_cmos_rtc_write_reg(u8 reg, u8 value) {
    cmos_outb(0x70, reg);
    cmos_outb(0x71, value);
}

int driver_cmos_rtc_init(void) {
    u8 regd = driver_cmos_rtc_read_reg(0x0D);
    g_vrt = (regd & 0x80) ? 1 : 0;
    /* FADT century register (QEMU reports 0x32).  Values outside the
     * plausible CMOS index range are reported as "none". */
    const acpi_fadt_t *fadt = driver_acpi_fadt();
    g_fadt_century = (fadt && fadt->century >= 0x10 && fadt->century <= 0x5F)
                         ? fadt->century : 0;
    g_present = 1;
    return g_vrt ? 0 : -1;
}

int driver_cmos_rtc_available(void) {
    return g_present;
}

/* Decode the Reg A rate index into Hz (0 = invalid/off). */
static int rate_index_to_hz(int rate) {
    if (rate < 3 || rate > 15) return 0;
    return 32768 >> (rate - 1);
}

void driver_cmos_rtc_status(driver_cmos_rtc_status_t *out) {
    u8 a = driver_cmos_rtc_read_reg(0x0A);
    u8 b = driver_cmos_rtc_read_reg(0x0B);
    u8 c = driver_cmos_rtc_read_reg(0x0C);   /* drain-on-read by design */
    u8 d = driver_cmos_rtc_read_reg(0x0D);
    out->reg_a = a;
    out->reg_b = b;
    out->reg_c = c;
    out->reg_d = d;
    out->uip    = (a & 0x80) ? 1 : 0;
    out->bcd    = (b & 0x04) ? 0 : 1;
    out->hour24 = (b & 0x02) ? 1 : 0;
    out->pie    = (b & 0x40) ? 1 : 0;
    out->aie    = (b & 0x20) ? 1 : 0;
    out->uie    = (b & 0x10) ? 1 : 0;
    out->vrt    = (d & 0x80) ? 1 : 0;
    out->rate_hz = rate_index_to_hz((int)(a & 0x0F));
    out->fadt_century = g_fadt_century;
}

/* ---- IRQ8 counting handler ------------------------------------------ */

static void cmos_rtc_irq_handler(void *ctx, arch_irq_frame_t *f) {
    (void)ctx; (void)f;
    u8 c = driver_cmos_rtc_read_reg(0x0C);
    if (!(c & 0x80)) return;                 /* not an RTC interrupt */
    g_cnt_total++;
    if (c & 0x40) g_cnt_periodic++;
    if (c & 0x20) g_cnt_alarm++;
    if (c & 0x10) g_cnt_update++;
}

static void counters_reset(void) {
    g_cnt_total = 0;
    g_cnt_update = 0;
    g_cnt_periodic = 0;
    g_cnt_alarm = 0;
}

u64 driver_cmos_rtc_irq_total(void)    { return g_cnt_total; }
u64 driver_cmos_rtc_irq_update(void)   { return g_cnt_update; }
u64 driver_cmos_rtc_irq_periodic(void) { return g_cnt_periodic; }
u64 driver_cmos_rtc_irq_alarm(void)    { return g_cnt_alarm; }

/* Install the handler + reset the counters. 0 on success. */
static int arm_irq8(void) {
    counters_reset();
    return arch_irq_register_handler(8, cmos_rtc_irq_handler, NULL);
}

static void disarm_irq8(void) {
    arch_irq_unregister_handler(8, cmos_rtc_irq_handler, NULL);
}

/* Wait until `*counter` reaches `want` or the deadline passes. */
static int wait_counts(volatile u64 *counter, u64 want, u64 timeout_ms) {
    u64 deadline = core_timer_now_ms() + timeout_ms;
    while (*counter < want) {
        if (core_timer_now_ms() >= deadline) return -1;
        core_sched_yield();
    }
    return 0;
}

/* ---- UIE: one update-ended interrupt per second ---------------------- */

int driver_cmos_rtc_uie_test(int seconds, u64 timeout_ms) {
    if (!g_present || seconds < 1 || seconds > 10) return -1;
    u8 regb = driver_cmos_rtc_read_reg(0x0B);
    (void)driver_cmos_rtc_read_reg(0x0C);            /* drain pending */
    if (arm_irq8() != 0) return -1;
    driver_cmos_rtc_write_reg(0x0B, (u8)(regb | 0x10));  /* UIE on */
    int rc = wait_counts(&g_cnt_update, (u64)seconds, timeout_ms);
    driver_cmos_rtc_write_reg(0x0B, regb);           /* restore */
    disarm_irq8();
    return rc == 0 ? (int)g_cnt_update : (int)g_cnt_update;
}

/* ---- PIE: periodic interrupts at 32768>>(rate-1) Hz ------------------- */

int driver_cmos_rtc_pie_test(int rate, int count, u64 timeout_ms) {
    if (!g_present || rate_index_to_hz(rate) == 0 || count < 1 || count > 100000)
        return -1;
    u8 rega = driver_cmos_rtc_read_reg(0x0A);
    u8 regb = driver_cmos_rtc_read_reg(0x0B);
    (void)driver_cmos_rtc_read_reg(0x0C);            /* drain pending */
    if (arm_irq8() != 0) return -1;
    driver_cmos_rtc_write_reg(0x0A, (u8)((rega & 0xF0) | ((u8)rate & 0x0F)));
    driver_cmos_rtc_write_reg(0x0B, (u8)((regb & ~0x10) | 0x40)); /* UIE off, PIE on */
    int rc = wait_counts(&g_cnt_periodic, (u64)count, timeout_ms);
    driver_cmos_rtc_write_reg(0x0B, regb);           /* restore both */
    driver_cmos_rtc_write_reg(0x0A, rega);
    disarm_irq8();
    return rc == 0 ? (int)g_cnt_periodic : (int)g_cnt_periodic;
}

/* ---- AIE: alarm interrupt at a time-of-day match ---------------------- */

/* Encode a 0..59 value in the register's data mode. */
static u8 enc_bcd_or_bin(int v, u8 regb) {
    if (regb & 0x04) return (u8)v;                    /* binary mode */
    return (u8)(((v / 10) << 4) | (v % 10));          /* BCD mode */
}

int driver_cmos_rtc_alarm_test(int ahead_s, u64 timeout_ms) {
    if (!g_present || ahead_s < 1 || ahead_s > 3600) return -1;
    core_rtc_time_t now;
    core_rtc_read(&now);
    int s = now.second + ahead_s;
    int m = now.minute;
    int h = now.hour;
    while (s >= 60) { s -= 60; m++; }
    while (m >= 60) { m -= 60; h++; }
    if (h >= 24) return -1;   /* alarm target would cross midnight */

    u8 regb = driver_cmos_rtc_read_reg(0x0B);
    u8 asec  = enc_bcd_or_bin(s, regb);
    u8 amin  = enc_bcd_or_bin(m, regb);
    u8 ahour;
    if (regb & 0x02) {
        /* 24-hour mode: plain (encoded) hour */
        ahour = enc_bcd_or_bin(h, regb);
    } else {
        /* 12-hour mode: 1..12 + PM flag in bit7 */
        int h12 = h % 12;
        if (h12 == 0) h12 = 12;
        ahour = (u8)(enc_bcd_or_bin(h12, regb) | (h >= 12 ? 0x80 : 0x00));
    }
    (void)driver_cmos_rtc_read_reg(0x0C);            /* drain pending */
    if (arm_irq8() != 0) return -1;
    driver_cmos_rtc_write_reg(0x01, asec);
    driver_cmos_rtc_write_reg(0x03, amin);
    driver_cmos_rtc_write_reg(0x05, ahour);
    /* AIE on; clear PIE/UIE so the counters isolate the alarm fire */
    driver_cmos_rtc_write_reg(0x0B, (u8)((regb & ~0x50) | 0x20));
    int rc = wait_counts(&g_cnt_alarm, 1, timeout_ms);
    driver_cmos_rtc_write_reg(0x0B, regb);           /* restore */
    disarm_irq8();
    return rc == 0 ? 1 : 0;
}
