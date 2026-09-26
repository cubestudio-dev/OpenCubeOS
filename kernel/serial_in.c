/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-02
 * File: kernel/serial_in.c
 * Purpose: COM1 serial RX ? keyboard queue bridge.
 */
#include "serial_in.h"
#include "keyboard.h"
#include "irq.h"
#include "idt.h"

static inline u8 inb(u16 p) { u8 v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline void outb(u16 p, u8 v) { __asm__ volatile("outb %0, %1" :: "a"(v), "Nd"(p)); }

#define COM1 0x3F8

void oc_serial_in_init(void) {
    /* Enable FIFO + trigger at 1 byte. */
    outb(COM1 + 2, 0xC7);
    /* Enable received-data-available IRQ (bit 0 of IER). */
    outb(COM1 + 1, 0x01);
    /* IRQ4 will now fire on each byte received. */
    oc_irq_register_handler(4, oc_serial_in_irq_handler, NULL);
}

void oc_serial_in_irq_handler(void *ctx, oc_irq_frame_t *f) {
    (void)ctx; (void)f;
    /* Check IIR: bit 0 = 0 means interrupt pending. */
    u8 iir = inb(COM1 + 2);
    if (iir & 0x01) return;  /* no interrupt */

    /* Drain all available bytes (FIFO may have multiple). */
    while (inb(COM1 + 5) & 0x01) {
        u8 b = inb(COM1);
        u16 key;
        switch (b) {
            case '\r': case '\n':
                key = OC_KEY_ENTER;
                break;
            case 0x7F: case 0x08:
                key = OC_KEY_BACKSPACE;
                break;
            case 0x03: /* Ctrl+C */
                key = OC_KEY_CTRL_C;
                break;
            case 0x04: /* Ctrl+D */
                key = OC_KEY_CTRL_D;
                break;
            case 0x1B:
                key = OC_KEY_ESC;
                break;
            case '\t':
                key = OC_KEY_TAB;
                break;
            default:
                if (b >= 0x20 && b < 0x7F) key = (u16)b;
                else continue;
        }
        /* Push directly into the keyboard queue via the internal API.
         * We mark the modifiers as 0 since serial doesn't carry mods. */
        /* Reuse the keyboard IRQ handler's queue by calling a helper. */
        oc_keyboard_inject(key);
    }
}
