/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS NAK - x86 port I/O helpers (WP-01-tree).
 *
 * Freestanding kernel code has no libc, so the two fundamental x86
 * port instructions are provided here as inline helpers for every
 * driver that talks to legacy I/O-space devices (serial UART, VGA
 * CRTC, ...). They live in a header because they must be inlinable
 * from several translation units at once.
 *
 * Naming: [class]_[category]_[role] - core (kernel core), io (port
 * I/O), out8/in8 (width in bits).
 */

#ifndef CORE_IO_PORT_H
#define CORE_IO_PORT_H

#include <stdint.h>

/* Write one byte to an I/O port.
 *
 * %w1 forces the %dx register form (`outb %al, %dx`): the only
 * encoding every gcc/binutils combination accepts (imm8 port
 * operands do not exist above 0xFF). */
static inline void core_io_out8(uint16_t port, uint8_t value)
{
    __asm__ __volatile__("outb %b0, %w1"
                         :
                         : "a"(value), "d"(port));
}

/* Read one byte from an I/O port. */
static inline uint8_t core_io_in8(uint16_t port)
{
    uint8_t value;
    __asm__ __volatile__("inb %w1, %b0"
                         : "=a"(value)
                         : "d"(port));
    return value;
}

#endif /* CORE_IO_PORT_H */
