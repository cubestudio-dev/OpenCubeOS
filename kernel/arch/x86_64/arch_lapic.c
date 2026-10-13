/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-input_chipset
 * File: kernel/arch/x86_64/arch_lapic.c
 * Purpose: Local APIC (xAPIC) enable/EOI/self-IPI + MSI vector
 *          dispatch. See arch_lapic.h for the vector layout.
 */
#include "arch_lapic.h"
#include "lib_string.h"

#include <stdint.h>

/* ---- xAPIC MMIO window (identity-mapped, first 4 GiB) ---- */
#define LAPIC_BASE_DEFAULT  0xFEE00000ull
#define LAPIC_MSR           0x1B

/* register offsets (xAPIC) */
#define LAPIC_REG_ID        0x020
#define LAPIC_REG_EOI       0x0B0
#define LAPIC_REG_SPURIOUS  0x0F0
#define LAPIC_REG_ESR       0x280
#define LAPIC_REG_ICR_LOW   0x300
#define LAPIC_REG_ICR_HIGH  0x310
#define LAPIC_REG_LVT_TIMER 0x320
#define LAPIC_REG_LVT_THERM 0x330
#define LAPIC_REG_LVT_PERF  0x340
#define LAPIC_REG_LVT_LINT0 0x350
#define LAPIC_REG_LVT_LINT1 0x360
#define LAPIC_REG_LVT_ERROR 0x370

static volatile u32 *g_lapic;
static int g_present;

static inline u32 lapic_read(u32 reg) {
    return g_lapic[reg / 4];
}

static inline void lapic_write(u32 reg, u32 val) {
    g_lapic[reg / 4] = val;
}

int arch_lapic_init(void) {
    /* CPUID leaf 1, EDX bit 9 = APIC present. */
    u32 a, b, c, d;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1));
    if (!(d & (1u << 9))) return -1;

    /* IA32_APIC_BASE: base address (bits 12..35) + global enable (11).
     * The base mask must strip bits 0..11 - they are reserved plus the
     * BSP flag (bit 8) and the enable bit (bit 11); keeping them made
     * every LAPIC register land 0x900 bytes off (g_lapic = 0xFEE00900),
     * so reads silently returned 0 (a fake "APIC id 0") and writes hit
     * illegal offsets (ESR bit7 = illegal-register-address, the exact
     * 0x80 QEMU reported) while the self-IPI never went out. */
    u32 lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(LAPIC_MSR));
    u64 base = ((u64)(hi & 0xFu) << 32) | (lo & 0xFFFFF000ull);
    if (!base) base = LAPIC_BASE_DEFAULT;
    if (base >= 0x100000000ull) return -1;   /* x2APIC range not supported */

    g_lapic = (volatile u32*)(uintptr_t)base;
    if (!(lo & (1u << 11))) {
        /* globally disabled: enable via MSR */
        lo |= (1u << 11);
        __asm__ volatile("wrmsr" :: "a"(lo), "d"(hi), "c"(LAPIC_MSR));
    }

    /* Spurious-interrupt vector 0xFF + APIC software enable (bit 8). */
    lapic_write(LAPIC_REG_SPURIOUS, lapic_read(LAPIC_REG_SPURIOUS) | 0xFFu | (1u << 8));

    /* Mask the local sources the kernel does not use. LINT0/LINT1 are
     * deliberately LEFT AS THE FIRMWARE SET THEM: on the pc platform
     * the 8259 PIC delivers every legacy line (PIT, keyboard, serial,
     * ...) through LINT0 as ExtINT, so masking it here would silently
     * kill all PIC interrupts the moment the LAPIC comes up (observed
     * live: serial console input went dead right after init). */
    lapic_write(LAPIC_REG_LVT_TIMER,  lapic_read(LAPIC_REG_LVT_TIMER)  | (1u << 16));
    lapic_write(LAPIC_REG_LVT_THERM, lapic_read(LAPIC_REG_LVT_THERM) | (1u << 16));
    lapic_write(LAPIC_REG_LVT_PERF,  lapic_read(LAPIC_REG_LVT_PERF)  | (1u << 16));
    lapic_write(LAPIC_REG_LVT_ERROR, lapic_read(LAPIC_REG_LVT_ERROR) | (1u << 16));

    g_present = 1;
    return 0;
}

int arch_lapic_present(void) { return g_present; }

u32 arch_lapic_id(void) {
    if (!g_present) return 0;
    return (lapic_read(LAPIC_REG_ID) >> 24) & 0xFF;
}

void arch_lapic_eoi(void) {
    if (!g_present) return;
    lapic_write(LAPIC_REG_EOI, 0);
}

int arch_lapic_send_self_ipi(u8 vector) {
    if (!g_present) return -1;
    /* Self ICI: Delivery Mode 000 (fixed), Level 1 (assert), Trigger
     * edge, Destination Shorthand 01 = SELF ONLY. (The xAPIC shorthand
     * field is 00=none, 01=self, 10=all-incl-self, 11=all-EXCL-self -
     * the old 11 sent the message to every APIC *except* this one, so
     * on the single-BSP platform nobody ever received it and the
     * interrupt evaporated.) The ICR is written low-dword-only per the
     * xAPIC spec for the self shorthand. */
    u32 icr = (u32)vector | (1u << 14) | (1u << 18);
    lapic_write(LAPIC_REG_ICR_LOW, icr);
    return 0;
}

/* ---- MSI dispatch table (vectors 0x30..0x6F) ---- */

#define MSI_VEC_BASE   0x30
#define MSI_VEC_COUNT  0x40
#define MSI_CHAIN_LEN  4

typedef struct {
    arch_irq_handler_fn fn;
    void *ctx;
} msi_slot_t;

static msi_slot_t g_msi_slots[MSI_VEC_COUNT][MSI_CHAIN_LEN];
static u64 g_msi_counts[MSI_VEC_COUNT];

int arch_lapic_register_vector(int vector, arch_irq_handler_fn handler, void *ctx) {
    if (!g_present || !handler) return -1;
    if (vector < MSI_VEC_BASE || vector >= MSI_VEC_BASE + MSI_VEC_COUNT) return -1;
    int i = vector - MSI_VEC_BASE;
    for (int k = 0; k < MSI_CHAIN_LEN; k++) {
        if (g_msi_slots[i][k].fn == NULL) {
            g_msi_slots[i][k].fn  = handler;
            g_msi_slots[i][k].ctx = ctx;
            return 0;
        }
    }
    return -2;
}

int arch_lapic_unregister_vector(int vector, arch_irq_handler_fn handler, void *ctx) {
    if (vector < MSI_VEC_BASE || vector >= MSI_VEC_BASE + MSI_VEC_COUNT) return -1;
    int i = vector - MSI_VEC_BASE;
    for (int k = 0; k < MSI_CHAIN_LEN; k++) {
        if (g_msi_slots[i][k].fn == handler && g_msi_slots[i][k].ctx == ctx) {
            for (int j = k; j + 1 < MSI_CHAIN_LEN; j++) g_msi_slots[i][j] = g_msi_slots[i][j + 1];
            g_msi_slots[i][MSI_CHAIN_LEN - 1].fn  = NULL;
            g_msi_slots[i][MSI_CHAIN_LEN - 1].ctx = NULL;
            return 0;
        }
    }
    return -2;
}

u64 arch_lapic_vector_count(int vector) {
    if (vector < MSI_VEC_BASE || vector >= MSI_VEC_BASE + MSI_VEC_COUNT) return 0;
    return g_msi_counts[vector - MSI_VEC_BASE];
}

void arch_msi_dispatch(arch_irq_frame_t *f) {
    u64 v = f->int_no;
    if (v < MSI_VEC_BASE || v >= MSI_VEC_BASE + MSI_VEC_COUNT) return;
    int i = (int)(v - MSI_VEC_BASE);
    /* Early EOI (same policy as arch_irq_dispatch, BUG-002): the LAPIC
     * may deliver the next message as soon as handlers run. */
    arch_lapic_eoi();
    g_msi_counts[i]++;
    for (int k = 0; k < MSI_CHAIN_LEN; k++) {
        if (g_msi_slots[i][k].fn) {
            g_msi_slots[i][k].fn(g_msi_slots[i][k].ctx, f);
        }
    }
}
