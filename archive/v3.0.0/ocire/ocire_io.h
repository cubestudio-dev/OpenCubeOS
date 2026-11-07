/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP-01-ocire-core
 * File: ocire/ocire_io.h
 * Purpose: Port I/O primitives for OCIRE.  The serial console, the PS/2
 *          keyboard, the 8042 reset, ACPI PM ports and the VGA cursor
 *          registers all go through these inline helpers.
 */

#ifndef OCIRE_IO_H
#define OCIRE_IO_H

#include "ocire_types.h"

static inline void oc_outb(u16 port, u8 val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline void oc_outw(u16 port, u16 val) {
    __asm__ volatile("outw %0, %%dx" : : "a"(val), "d"(port));
}

static inline u8 oc_inb(u16 port) {
    u8 v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline u16 oc_inw(u16 port) {
    u16 v;
    __asm__ volatile("inw %%dx, %0" : "=a"(v) : "d"(port));
    return v;
}

/* 32-bit port I/O.  Port comes through the "d" constraint (DX
 * register): PCI config access uses 0xCF8/0xCFC which are far beyond
 * the imm8 range of "Nd" - the exact trap that made the 2.0.0 CI
 * build fail (tui serial ports), fixed the same way here. */
static inline void oc_outl(u16 port, u32 val) {
    __asm__ volatile("outl %0, %%dx" : : "a"(val), "d"(port));
}

static inline u32 oc_inl(u16 port) {
    u32 v;
    __asm__ volatile("inl %%dx, %0" : "=a"(v) : "d"(port));
    return v;
}

static inline void oc_io_wait(void) {
    /* A port read is a safe ~1us-ish delay on every mainstream PC. */
    (void)oc_inb(0x80);
}

#endif /* OCIRE_IO_H */
