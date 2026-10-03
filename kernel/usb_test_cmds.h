/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10d
 * File: kernel/usb_test_cmds.h
 * Purpose: WP-10d USB shell commands + tests (usb_test_cmds.c).
 *
 * Registers: usb, usbdev, usb_core_test, usb_kbd_test,
 *   usb_mouse_test, usb_storage_test, usb_serial_test,
 *   usb_hotplug_test, usb_hub_test.  usb_audio_test stays in
 *   snd_test_cmds.c (WP-10c).  real_hw_test (WP-10a, in
 *   disk_test_cmds.c) gains the WP-10d USB section.
 *
 * Also exposes usb_poll_start(): spawn the kernel poll thread that
 * drives hot-plug + HID input + serial RX for the whole session.
 */
#ifndef OC_USB_TEST_CMDS_H
#define OC_USB_TEST_CMDS_H

void usb_test_cmds_register(void);
void usb_poll_start(void);

#endif /* OC_USB_TEST_CMDS_H */
