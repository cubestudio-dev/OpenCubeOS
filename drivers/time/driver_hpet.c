/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-timer_rtc
 * File: drivers/time/driver_hpet.c
 * Purpose: HPET MMIO driver: main counter, one-shot and periodic
 *          comparators on timer1 via legacy routing (IRQ8).
 */
#include "driver_hpet.h"
#include "driver_acpi.h"
#include "arch_irq.h"
#include "core_timer.h"

#include <stdint.h>

static inline u32 mmio_r32(u64 addr) {
    return *(volatile u32 *)(uintptr_t)addr;
}
static inline void mmio_w32(u64 addr, u32 v) {
    *(volatile u32 *)(uintptr_t)addr = v;
}

/* HPET register offsets (HPET 1.0a). */
#define HPET_GCAP        0x00u   /* 64-bit: caps | period_fs<<32 */
#define HPET_GEN_CFG     0x10u
#define HPET_GEN_STAT    0x20u
#define HPET_MAIN_CNT    0xF0u
#define HPET_TIM_CONF(n) (0x100u + 0x20u * (u32)(n))
#define HPET_TIM_CMP(n)  (0x108u + 0x20u * (u32)(n))

/* GEN_CFG bits */
#define HPET_ENABLE_CNF  (1u << 0)
#define HPET_LEG_RT_CNF  (1u << 1)
/* Tn_CONFIG bits */
#define HPET_TN_LEVEL        (1u << 1)
#define HPET_TN_ENABLE       (1u << 2)
#define HPET_TN_PERIODIC     (1u << 3)
#define HPET_TN_PERIODIC_CAP (1u << 4)
#define HPET_TN_SETVAL       (1u << 5)

static u64 g_base = 0;
static u32 g_caps = 0;        /* GCAP lower 32 bits */
static u32 g_period_fs = 0;   /* GCAP upper 32 bits */
static u32 g_table_block_id = 0;
static int g_present = 0;
static u64 g_irq_count = 0;

int driver_hpet_init(void) {
    g_present = 0;
    const acpi_hpet_t *t = driver_acpi_hpet();
    if (!t) return -1;
    /* Only SystemMemory GAS is usable (identity-mapped < 4 GiB). */
    if (t->base_address.address_space != 0) return -1;
    u64 base = t->base_address.address;
    if (base == 0 || base >= 0x100000000ull) return -1;
    g_table_block_id = t->event_timer_block_id;
    u64 gcap = ((u64)mmio_r32(base + HPET_GCAP + 4) << 32)
             | (u64)mmio_r32(base + HPET_GCAP);
    g_caps = (u32)(gcap & 0xFFFFFFFFull);
    g_period_fs = (u32)(gcap >> 32);
    if (g_period_fs == 0) return -1;   /* no clock period: unusable */
    g_base = base;
    g_present = 1;
    return 0;
}

int driver_hpet_available(void)     { return g_present; }
u64  driver_hpet_base(void)         { return g_base; }
u32  driver_hpet_period_fs(void)    { return g_period_fs; }
u64  driver_hpet_freq_hz(void) {
    if (g_period_fs == 0) return 0;
    return 1000000000000000ull / (u64)g_period_fs;
}
int driver_hpet_timer_count(void)   { return (int)((g_caps >> 8) & 0x1F) + 1; }
int driver_hpet_counter_64bit(void) { return (g_caps >> 13) & 1; }
int driver_hpet_legacy_cap(void)    { return (g_caps >> 15) & 1; }
int driver_hpet_revision(void)      { return (int)(g_caps & 0xFF); }
u32  driver_hpet_table_block_id(void) { return g_table_block_id; }

u64 driver_hpet_ticks_to_ns(u64 ticks) {
    if (g_period_fs == 0) return 0;
    return ticks * (u64)g_period_fs / 1000ull;
}

u64 driver_hpet_counter_read(void) {
    if (!g_present) return 0;
    u32 hi1 = mmio_r32(g_base + HPET_MAIN_CNT + 4);
    u32 lo  = mmio_r32(g_base + HPET_MAIN_CNT);
    u32 hi2 = mmio_r32(g_base + HPET_MAIN_CNT + 4);
    if (hi1 != hi2) lo = mmio_r32(g_base + HPET_MAIN_CNT);
    return ((u64)hi2 << 32) | (u64)lo;
}

static u32 gen_cfg_read(void)  { return mmio_r32(g_base + HPET_GEN_CFG); }
static void gen_cfg_write(u32 v) { mmio_w32(g_base + HPET_GEN_CFG, v); }

static u32 tim_conf_read(int n)  { return mmio_r32(g_base + HPET_TIM_CONF(n)); }
static void tim_conf_write(int n, u32 v) { mmio_w32(g_base + HPET_TIM_CONF(n), v); }
static void tim_cmp_write(int n, u64 v) {
    mmio_w32(g_base + HPET_TIM_CMP(n), (u32)(v & 0xFFFFFFFFull));
    mmio_w32(g_base + HPET_TIM_CMP(n) + 4, (u32)(v >> 32));
}

/* ---- IRQ8 handler ----------------------------------------------------- */

static void hpet_irq_handler(void *ctx, arch_irq_frame_t *f) {
    (void)ctx; (void)f;
    /* Clear the timer1 status bit (write-1-to-clear) so edge/level
     * sources deassert; count every pulse. */
    mmio_w32(g_base + HPET_GEN_STAT, (1u << 1));
    g_irq_count++;
}

u64 driver_hpet_irq_count(void) { return g_irq_count; }

/* ---- interrupt tests --------------------------------------------------- */

static void ms_busy_wait_hpet(u64 ms) {
    /* Busy-wait bounded by the HPET counter itself: independent of the
     * PIT tick (which legacy routing gates). */
    u64 start = driver_hpet_counter_read();
    u64 need = ms * driver_hpet_freq_hz() / 1000ull;
    while (driver_hpet_counter_read() - start < need) { __asm__ volatile("pause"); }
}

int driver_hpet_counter_test(u64 *delta_ticks, u64 *ms_elapsed) {
    if (!g_present) return -1;
    u32 cfg = gen_cfg_read();
    u64 t0_ticks, t1_ticks;
    u64 ms0, ms1;
    int enabled_here = 0;
    if (!(cfg & HPET_ENABLE_CNF)) {
        gen_cfg_write(cfg | HPET_ENABLE_CNF);
        enabled_here = 1;
    }
    ms0 = core_timer_now_ms();
    t0_ticks = driver_hpet_counter_read();
    ms_busy_wait_hpet(100);          /* 100 ms HPET-bounded busy window */
    t1_ticks = driver_hpet_counter_read();
    ms1 = core_timer_now_ms();
    if (enabled_here) gen_cfg_write(cfg);
    if (t1_ticks <= t0_ticks) return -1;
    if (delta_ticks) *delta_ticks = t1_ticks - t0_ticks;
    if (ms_elapsed)  *ms_elapsed = ms1 - ms0;
    return 0;
}

static int timer_irq_test_arm(int periodic, int period_ms) {
    /* Requires the legacy route capability; uses timer1 -> IRQ8. */
    if (!g_present || !driver_hpet_legacy_cap()) return -1;
    if (driver_hpet_timer_count() < 2) return -1;
    u32 cfg = gen_cfg_read();
    if (periodic) {
        u32 tconf = tim_conf_read(1);
        if (!(tconf & HPET_TN_PERIODIC_CAP)) return -1;
    }
    if (arch_irq_register_handler(8, hpet_irq_handler, NULL) != 0) return -1;
    g_irq_count = 0;

    /* Configure with the HPET disabled (spec requirement), then arm. */
    gen_cfg_write(cfg & ~HPET_ENABLE_CNF);
    gen_cfg_write((cfg & ~HPET_ENABLE_CNF) | HPET_LEG_RT_CNF);
    u64 freq = driver_hpet_freq_hz();
    u64 period_ticks = (u64)period_ms * freq / 1000ull;
    u32 tconf = tim_conf_read(1) & ~(HPET_TN_LEVEL | HPET_TN_SETVAL | HPET_TN_PERIODIC);
    tconf |= HPET_TN_ENABLE | HPET_TN_LEVEL;   /* level: stat-clear acks */
    if (periodic) tconf |= HPET_TN_PERIODIC;
    tim_conf_write(1, tconf);
    u64 counter = driver_hpet_counter_read();
    tim_cmp_write(1, counter + period_ticks);       /* first fire */
    if (periodic) {
        tim_conf_write(1, tconf | HPET_TN_SETVAL);
        tim_cmp_write(1, period_ticks);             /* accumulation value */
        tim_conf_write(1, tconf & ~HPET_TN_SETVAL);
    }
    gen_cfg_write(gen_cfg_read() | HPET_ENABLE_CNF);
    return 0;
}

static void timer_irq_test_disarm(void) {
    u32 cfg = gen_cfg_read();
    u32 tconf = tim_conf_read(1);
    tim_conf_write(1, tconf & ~(HPET_TN_ENABLE | HPET_TN_PERIODIC));
    gen_cfg_write(cfg & ~(HPET_ENABLE_CNF | HPET_LEG_RT_CNF));
    arch_irq_unregister_handler(8, hpet_irq_handler, NULL);
}

/* Deadline bounded by the HPET counter: `want` fires must arrive within
 * want*period + grace counter-milliseconds. */
static int wait_irq_hpet(u64 want, int period_ms, int grace_ms) {
    u64 start = driver_hpet_counter_read();
    u64 limit = (want * (u64)period_ms + (u64)grace_ms)
                * driver_hpet_freq_hz() / 1000ull;
    while (g_irq_count < want) {
        if (driver_hpet_counter_read() - start > limit) return -1;
        __asm__ volatile("pause");
    }
    return 0;
}

int driver_hpet_oneshot_test(int ms) {
    if (timer_irq_test_arm(0, ms) != 0) return -1;
    int rc = wait_irq_hpet(1, ms, ms + 100);
    timer_irq_test_disarm();
    if (rc != 0) return 0;
    return (int)g_irq_count >= 1 ? 1 : 0;
}

int driver_hpet_periodic_test(int period_ms, int count) {
    if (count < 1 || count > 100) return -1;
    if (timer_irq_test_arm(1, period_ms) != 0) return -1;
    int rc = wait_irq_hpet((u64)count, period_ms, period_ms + 100);
    timer_irq_test_disarm();
    if (rc != 0) return (int)g_irq_count;   /* partial: reported honestly */
    return (int)g_irq_count;
}
