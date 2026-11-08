/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS NAK - OCBL shared x86 port I/O helpers (WP-02-ocbl).
 *
 * Same scheme as the ocos kernel helper (kernel/core_io_port.h): the
 * two fundamental port instructions as inline helpers, in a header so
 * every driver that talks to legacy I/O-space devices can inline them.
 *
 * This file is compiled by BOTH boot firmware paths:
 *   - BIOS side  (gcc, ELF64 freestanding)
 *   - UEFI side  (x86_64-w64-mingw32-gcc, PE32+)
 * It must therefore include nothing platform-specific: no BIOS
 * headers, no EFI headers.
 *
 * Naming: [class]_[category]_[role] - core (boot core), io (port
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
