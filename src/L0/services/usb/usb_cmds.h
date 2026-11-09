/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP03 - services/usb op surface (exported bodies).
 * The bodies were rescued verbatim from recovery/shell_cmds_usb_test.c
 * into services/usb/usb_cmds_test.c (renamed shell_cmd_* -> usb_cmd_*)
 * when the oc> shell died; this header exports them to the L1 dispatch
 * table (services/usb/usb_l1.c). */
#ifndef OC_USB_CMDS_H
#define OC_USB_CMDS_H

#ifdef __cplusplus
extern "C" {
#endif

/* services/usb/usb_cmds_test.c (WP-03 rescue, renamed usb_cmd_*) */
int usb_cmd_usb(const char *args);
int usb_cmd_usbdev(const char *args);
int usb_cmd_usb_core_test(const char *args);
int usb_cmd_usb_kbd_test(const char *args);
int usb_cmd_usb_mouse_test(const char *args);
int usb_cmd_usb_storage_test(const char *args);
int usb_cmd_usb_serial_test(const char *args);
int usb_cmd_usb_hotplug_test(const char *args);
int usb_cmd_usb_hub_test(const char *args);

#ifdef __cplusplus
}
#endif

#endif /* OC_USB_CMDS_H */
