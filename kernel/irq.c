/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Cube Studio <cubestudio@qq.com> */
/* Open Cube OS - WP-02
 * File: kernel/irq.c
 * Purpose: IRQ dispatch + L1 IRQ-handler chain.
 */
#include "irq.h"
#include "idt.h"
#include "pic.h"
#include "string.h"

#define OC_IRQ_CHAIN_LEN 4
typedef struct {
    oc_irq_handler_fn fn;
    void             *ctx;
} oc_irq_slot_t;

static oc_irq_slot_t g_irq_slots[16][OC_IRQ_CHAIN_LEN];

void oc_irq_init(void) {
    oc_memset(g_irq_slots, 0, sizeof(g_irq_slots));
}

int oc_irq_register_handler(int irq, oc_irq_handler_fn handler, void *ctx) {
    if (irq < 0 || irq >= 16 || !handler) return -1;
    for (int i = 0; i < OC_IRQ_CHAIN_LEN; i++) {
        if (g_irq_slots[irq][i].fn == NULL) {
            g_irq_slots[irq][i].fn  = handler;
            g_irq_slots[irq][i].ctx = ctx;
            oc_pic_unmask((u8)irq);
            return 0;
        }
    }
    return -2;
}

int oc_irq_unregister_handler(int irq, oc_irq_handler_fn handler, void *ctx) {
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
                oc_pic_mask((u8)irq);
            }
            return 0;
        }
    }
    return -2;
}

void oc_irq_dispatch(oc_irq_frame_t *f) {
    int irq = (int)(f->int_no - OC_IRQ0_VECTOR);
    if (irq < 0 || irq >= 16) return;
    /* BUG-002 FIX (a): Send EOI BEFORE running handlers. If a handler
     * (e.g., sched_tick) does a context switch, the switched-away task
     * may never return to this point. With late-EOI, the 8259 PIC keeps
     * the IRQ in-service forever, blocking all further interrupts.
     * Early-EOI ensures the PIC can deliver the next IRQ immediately. */
    oc_pic_eoi((u8)irq);
    /* Run all handlers on the chain. */
    for (int i = 0; i < OC_IRQ_CHAIN_LEN; i++) {
        if (g_irq_slots[irq][i].fn) {
            g_irq_slots[irq][i].fn(g_irq_slots[irq][i].ctx, f);
        }
    }
}
