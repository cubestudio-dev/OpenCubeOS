/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP06c-installer_gui
 * File: ocse/drivers/input/driver_input_ps2mouse.h
 * Purpose: PS/2 auxiliary-port (i8042 aux) mouse driver - IRQ12.
 *
 * The GUI wizard needs a pointer on machines without USB HID or an
 * I2C touchpad: QEMU's default pointing device is a PS/2 mouse on
 * the auxiliary port, and every PC BIOS wires one up by default.
 *
 * Handshake (all inside cli/sti - the i8042 raises IRQ1 for EVERY
 * output-buffer byte, including controller responses, and the
 * keyboard handler eats them first if interrupts are on):
 *   1. enable the aux port (0xA8),
 *   2. read-modify-write the controller config byte (0x20/0x60):
 *      clear CFG_AUX_CLK_DIS (bit 5) ONLY - the keyboard translate
 *      bit must stay as the BIOS left it (clearing it turns the
 *      keyboard raw set-2 and every hotkey dies - live-caught),
 *   3. RESET (0xFF) -> ACK(0xFA) + selftest-ok(0xAA) + device-id(0x00)
 *      - the id byte MUST be consumed or it poisons the next ACK
 *      read ("aux ack missing" - live-caught),
 *   4. SET_DEFAULTS (0xF6), ENABLE (0xF4).
 *
 * Stream mode then delivers 3-byte packets over IRQ12:
 *   [0] bit0=left, bit1=right, bit2=middle, bit3=1 (sync),
 *       bit6=x-overflow, bit7=y-overflow
 *   [1] delta-x (signed), [2] delta-y (signed; Y grows UPWARD)
 * The sync bit re-synchronizes after a lost byte. Events land in a
 * small ring queue the GUI polls.
 */
#ifndef OC_PS2MOUSE_H
#define OC_PS2MOUSE_H

#include "types.h"

typedef struct {
    i32 dx, dy;                 /* signed movement (y up)          */
    u8  buttons;                /* bit0 left, bit1 right, bit2 mid */
} ps2mouse_event_t;

/* Full init: controller handshake + IRQ12 registration. Returns 0
 * when a mouse answered, -1 when the aux port is absent/dead (the
 * GUI then runs keyboard-only; never fatal). */
int  driver_input_ps2mouse_init(void);

/* Poll one queued event. Returns 1 with an event, 0 when empty. */
int  driver_input_ps2mouse_read_event(ps2mouse_event_t* out);

/* True once init saw a live device (drives the pointer draw). */
int  driver_input_ps2mouse_present(void);

#endif /* OC_PS2MOUSE_H */
