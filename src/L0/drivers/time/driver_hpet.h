/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-timer_rtc
 * File: drivers/time/driver_hpet.h
 * Purpose: HPET (High Precision Event Timer) driver - the ACPI HPET
 *          table is already parsed by driver_acpi (driver_acpi_hpet());
 *          this driver adds the MMIO timer itself.
 *
 * Register map (HPET 1.0a, base from the table's GAS):
 *   0x00  GCAP_ID      64-bit capabilities: caps[31:0] = REV_ID[7:0],
 *                      NUM_TIM_CAP[12:8] (= timers-1), CNT_SIZE_CAP
 *                      (bit13, 1 = 64-bit counter), LEG_RT_CAP (bit15),
 *                      VENDOR_ID[31:16]; caps[63:32] = counter clock
 *                      period in femtoseconds.
 *   0x10  GEN_CFG      bit0 ENABLE_CNF, bit1 LEG_RT_CNF (legacy route:
 *                      timer0 -> ISA IRQ0, timer1 -> ISA IRQ8)
 *   0x20  GEN_STAT     per-timer interrupt status (write-1-to-clear)
 *   0xF0  MAIN_CNT     64-bit main counter
 *   0x100+0x20n  Tn_CONFIG, 0x108+0x20n Tn_COMPARATOR
 *     Tn_CONFIG: bit1 LEVEL, bit2 ENABLE, bit3 PERIODIC,
 *                bit4 PERIODIC_CAP (RO), bit5 SETVAL (write period),
 *                bit6 32MODE
 *
 * Interrupt path: this kernel drives the two 8259 PICs only (no
 * IO-APIC redirections), so HPET timer interrupts are delivered the
 * only way a PIC-only kernel can receive them: LEG_RT_CNF legacy
 * routing with timer1 -> IRQ8 (timer0 would collide with the PIT on
 * IRQ0 and is never enabled).  While legacy routing is on, the PIT is
 * disconnected from IRQ0, so the one-shot / periodic tests bound their
 * timeouts with the HPET counter itself, not with core_timer_now_ms().
 *
 * All names/strings are ASCII by project rule.
 */
#ifndef OC_DRIVER_HPET_H
#define OC_DRIVER_HPET_H

#include "types.h"

/* Probe the ACPI HPET table + capabilities registers.
 * Returns 0 when the HPET is usable, -1 when absent/unusable. */
int driver_hpet_init(void);

/* 1 after a successful driver_hpet_init(). */
int driver_hpet_available(void);

/* Capability accessors (valid after init). */
u64  driver_hpet_base(void);          /* MMIO base address */
u32  driver_hpet_period_fs(void);     /* counter clock period, femtoseconds */
u64  driver_hpet_freq_hz(void);       /* 1e15 / period (0 when unusable) */
int  driver_hpet_revision(void);      /* GCAP REV_ID [7:0] */
int  driver_hpet_timer_count(void);   /* from the live caps register */
int  driver_hpet_counter_64bit(void); /* CNT_SIZE_CAP */
int  driver_hpet_legacy_cap(void);    /* LEG_RT_CAP */

/* Read the 64-bit main counter (re-read-high for consistency). */
u64  driver_hpet_counter_read(void);

/* Counter enablement helpers for external measurers (driver_tsc):
 * ensure the main counter runs (GEN ENABLE_CNF) and restore the
 * previous state afterwards. */
int  driver_hpet_ensure_enabled(void);
void driver_hpet_restore_enabled(int was_enabled);

/* Raw Event Timer Block ID from the ACPI table (decode with the
 * ACPI_HPET_* macros from driver_acpi.h). */
u32  driver_hpet_table_block_id(void);

/* Convert counter ticks to nanoseconds (0 when the period is unusable). */
u64  driver_hpet_ticks_to_ns(u64 ticks);

/* One counter-test round: samples the counter around a busy window and
 * reports the tick delta + the PIT-measured milliseconds it spanned.
 * Returns 0 on success (monotonic, delta > 0). */
int  driver_hpet_counter_test(u64 *delta_ticks, u64 *ms_elapsed);

/* ---- interrupt tests (legacy route, timer1 -> IRQ8) -------------------
 * Both tests arm the REAL hardware comparator and count REAL IRQ8
 * pulses; timeouts are HPET-counter bounded (the PIT tick is gated by
 * the legacy route while the test runs).  Hardware state is restored
 * before returning. */

/* One-shot: comparator fires once after `ms` milliseconds.
 * Returns the observed fire count (1 = PASS) - 0 on timeout,
 * -1 when legacy routing is unavailable. */
int  driver_hpet_oneshot_test(int ms);

/* Periodic: comparator fires every `period_ms` milliseconds, `count`
 * times. Returns the observed fire count, -1 when legacy routing or
 * the periodic capability is unavailable. */
int  driver_hpet_periodic_test(int period_ms, int count);

/* Total IRQ8 pulses counted by the HPET handler (monotonic). */
u64  driver_hpet_irq_count(void);

#endif /* OC_DRIVER_HPET_H */
