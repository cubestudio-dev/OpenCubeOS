/* SPDX-License-Identifier: Apache-2.0 */
/* Open Cube OS - WP-02
 * File: keyboard.h
 * Purpose: PS/2 keyboard driver (scancode set 1 ? ASCII) + input queue.
 *
 * PS/2 keyboard IRQ is 1 (vector 33). The keyboard controller's output
 * buffer register is at port 0x60, status at 0x64.
 *
 * Scancode set 1: each keypress emits a 1-byte make code (bit 7 = 0) and
 * a 1-byte break code (bit 7 = 1). Extended keys are prefixed with 0xE0.
 *
 * The driver tracks Shift/Ctrl/Alt/CapsLock/NumLock/ScrollLock state and
 * produces ASCII characters (or special codes for non-printable keys).
 *
 * Produced keycodes go into a ring buffer. The console consumes them via
 * oc_keyboard_getch().
 */
#ifndef OC_KEYBOARD_H
#define OC_KEYBOARD_H

#include "idt.h"   /* for oc_irq_frame_t */

/* Special keycodes (above 0x7F) for non-printable keys. */
#define OC_KEY_ENTER    0x0D
#define OC_KEY_BACKSPACE 0x08
#define OC_KEY_TAB      0x09
#define OC_KEY_ESC      0x1B
#define OC_KEY_UP       0x80
#define OC_KEY_DOWN     0x81
#define OC_KEY_LEFT     0x82
#define OC_KEY_RIGHT    0x83
#define OC_KEY_HOME     0x84
#define OC_KEY_END      0x85
#define OC_KEY_PGUP     0x86
#define OC_KEY_PGDN     0x87
#define OC_KEY_INS      0x88
#define OC_KEY_DEL      0x89
#define OC_KEY_F1       0x90
#define OC_KEY_F2       0x91
#define OC_KEY_F3       0x92
#define OC_KEY_F4       0x93
#define OC_KEY_F5       0x94
#define OC_KEY_F6       0x95
#define OC_KEY_F7       0x96
#define OC_KEY_F8       0x97
#define OC_KEY_F9       0x98
#define OC_KEY_F10      0x99
#define OC_KEY_F11      0x9A
#define OC_KEY_F12      0x9B
#define OC_KEY_CTRL_C   0x03   /* Ctrl+C ? ETX */
#define OC_KEY_CTRL_D   0x04   /* Ctrl+D ? EOT */

void oc_keyboard_init(void);

/* IRQ1 handler (called from oc_irq_dispatch). Public so keyboard.c can
 * reference it before its definition. */
void oc_keyboard_irq_handler(void *ctx, oc_irq_frame_t *f);

/* Get one key from the buffer. Returns -1 if empty. */
int oc_keyboard_getch(void);

/* Non-blocking peek. */
int oc_keyboard_has_key(void);

/* Inject a keycode directly into the queue (used by serial input bridge). */
void oc_keyboard_inject(u16 keycode);

/* Modifier flags (for oc_keyboard_get_mods). */
#define OC_MOD_SHIFT  0x01
#define OC_MOD_CTRL   0x02
#define OC_MOD_ALT    0x04
#define OC_MOD_CAPS   0x08
#define OC_MOD_NUM    0x10
#define OC_MOD_SCROLL 0x20
u8 oc_keyboard_get_mods(void);

/* ---- L1 extension: keyboard input handler ----
 *
 * L1 can register a handler that receives every key BEFORE it goes into
 * the input buffer. The handler returns 0 to let L0 enqueue the key
 * normally, or non-zero to consume it (so L0 doesn't enqueue it). This
 * is the input-method / hotkey / macro hook.
 *
 * The handler receives the raw keycode (one of the OC_KEY_* constants
 * above, or a printable ASCII byte) and the current modifier mask.
 */
typedef int (*oc_kbd_handler_fn)(u16 keycode, u8 mods);

int oc_keyboard_register_handler(oc_kbd_handler_fn handler);
int oc_keyboard_unregister_handler(oc_kbd_handler_fn handler);

#endif /* OC_KEYBOARD_H */
