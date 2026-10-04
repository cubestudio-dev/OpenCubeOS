/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-02
 * File: kernel/irq.c
 * Purpose: IRQ dispatch + L1 IRQ-handler chain.
 */
#include "arch_irq.h"
#include "arch_idt.h"
#include "arch_pic.h"
#include "lib_string.h"

#define OC_IRQ_CHAIN_LEN 4
typedef struct {
    arch_irq_handler_fn fn;
    void             *ctx;
} arch_irq_slot_t;

static arch_irq_slot_t g_irq_slots[16][OC_IRQ_CHAIN_LEN];

void arch_irq_init(void) {
    memset(g_irq_slots, 0, sizeof(g_irq_slots));
}

int arch_irq_register_handler(int irq, arch_irq_handler_fn handler, void *ctx) {
    if (irq < 0 || irq >= 16 || !handler) return -1;
    for (int i = 0; i < OC_IRQ_CHAIN_LEN; i++) {
        if (g_irq_slots[irq][i].fn == NULL) {
            g_irq_slots[irq][i].fn  = handler;
            g_irq_slots[irq][i].ctx = ctx;
            arch_pic_unmask((u8)irq);
            return 0;
        }
    }
    return -2;
}

int arch_irq_unregister_handler(int irq, arch_irq_handler_fn handler, void *ctx) {
    if (irq < 0 || irq >= 16 || !handler) return -1;
    for (int i = 0; i < OC_IRQ_CHAIN_LEN; i++) {
        if (g_irq_slots[irq][i].fn == handler && g_irq_slots[irq][i].ctx == ctx) {
            g_irq_slots[irq][i].fn  = NULL;
            g_irq_slots[irq][i].ctx = NULL;
            /* compact */
            for (int j = i; j + 1 < OC_IRQ_CHAIN_LEN; j++) {
                g_irq_slots[irq][j] = g_irq_slots[irq][j+1];
                g_irq_slots[irq][j+1].fn = NULL;
                g_irq_slots[irq][j+1].ctx = NULL;
            }
            /* If chain is now empty, mask the IRQ. */
            if (g_irq_slots[irq][0].fn == NULL) {
                arch_pic_mask((u8)irq);
            }
            return 0;
        }
    }
    return -2;
}

void arch_irq_dispatch(arch_irq_frame_t *f) {
    int irq = (int)(f->int_no - OC_IRQ0_VECTOR);
    if (irq < 0 || irq >= 16) return;
    /* BUG-002 FIX (a): Send EOI BEFORE running handlers. If a handler
     * (e.g., core_sched_tick) does a context switch, the switched-away task
     * may never return to this point. With late-EOI, the 8259 PIC keeps
     * the IRQ in-service forever, blocking all further interrupts.
     * Early-EOI ensures the PIC can deliver the next IRQ immediately. */
    arch_pic_eoi((u8)irq);
    /* Run all handlers on the chain. */
    for (int i = 0; i < OC_IRQ_CHAIN_LEN; i++) {
        if (g_irq_slots[irq][i].fn) {
            g_irq_slots[irq][i].fn(g_irq_slots[irq][i].ctx, f);
        }
    }
}
