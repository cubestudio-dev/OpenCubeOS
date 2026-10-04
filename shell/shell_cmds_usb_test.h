/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10d
 * File: kernel/driver_usb_test_cmds.h
 * Purpose: WP-10d USB shell commands + tests (driver_usb_test_cmds.c).
 *
 * Registers: usb, usbdev, driver_usb_core_test, driver_usb_kbd_test,
 *   driver_usb_mouse_test, driver_usb_storage_test, driver_usb_serial_test,
 *   driver_usb_hotplug_test, driver_usb_hub_test.  driver_usb_audio_test stays in
 *   driver_snd_test_cmds.c (WP-10c).  real_hw_test (WP-10a, in
 *   disk_test_cmds.c) gains the WP-10d USB section.
 *
 * Also exposes driver_usb_poll_start(): spawn the kernel poll thread that
 * drives hot-plug + HID input + serial RX for the whole session.
 */
#ifndef OC_USB_TEST_CMDS_H
#define OC_USB_TEST_CMDS_H

void shell_cmds_usb_test_register(void);
void driver_usb_poll_start(void);

#endif /* OC_USB_TEST_CMDS_H */
