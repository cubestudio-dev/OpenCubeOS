/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10d
 * File: kernel/driver_usb_hid.h
 * Purpose: USB HID class driver interface (driver_usb_hid.c) - keyboards and
 *          mice, wired into the standard input paths.
 *
 * L1 extension surface:
 *
 *   int  driver_usb_hid_init(void);
 *       Register the HID keyboard and mouse class drivers with the
 *       USB core (driver_usb_register_driver).  Call once, before
 *       driver_usb_probe_all()/driver_usb_enumerate().  0 on success.
 *
 *   void driver_usb_hid_poll(void);
 *       One round of interrupt-IN reads on every registered keyboard
 *       and mouse.  Called by the USB poll kernel thread; keystrokes
 *       are injected through the PS/2 driver's standard input queue
 *       (driver_input_keyboard_inject), mouse movement lands in the event ring
 *       (driver_usb_mouse_read_event).
 *
 *   int  driver_usb_hid_num_keyboards(void);
 *   int  driver_usb_hid_num_mice(void);
 *       Number of HID devices claimed by each sub-driver.
 *
 *   typedef struct { int dx, dy; u8 buttons; u8 wheel; }
 *       driver_usb_mouse_event_t;
 *   int  driver_usb_mouse_read_event(driver_usb_mouse_event_t *out);
 *       Pop one mouse event from the ring; 1 = event returned,
 *       0 = ring empty.  dx/dy are relative, buttons is bit0 left,
 *       bit1 right, bit2 middle, wheel is signed wheel motion.
 *
 *   int  driver_usb_mouse_num_events(void);
 *       Events currently buffered (how much driver_usb_mouse_read_event
 *       can drain right now).
 *
 *   void driver_usb_mouse_print_state(void);
 *       One status block for the `usb` command: device, counters,
 *       last event.
 */
#ifndef OC_USB_HID_H
#define OC_USB_HID_H

#include "types.h"

typedef struct {
    int dx, dy;
    u8  buttons;      /* bit0 left, bit1 right, bit2 middle */
    u8  wheel;
} driver_usb_mouse_event_t;

int  driver_usb_hid_init(void);
void driver_usb_hid_poll(void);
int  driver_usb_hid_num_keyboards(void);
int  driver_usb_hid_num_mice(void);
int  driver_usb_mouse_read_event(driver_usb_mouse_event_t *out);
int  driver_usb_mouse_num_events(void);
void driver_usb_mouse_print_state(void);

#endif /* OC_USB_HID_H */
