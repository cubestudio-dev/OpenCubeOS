/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 2.0.0 - services/usb command surface (exported bodies,
 * migrated by declaration in group N).  The command bodies live in
 * recovery/shell_cmds_usb_test.c; this header exports them to the
 * L1 dispatch table (services/usb/usb_l1.c). */
#ifndef OC_USB_CMDS_H
#define OC_USB_CMDS_H

#ifdef __cplusplus
extern "C" {
#endif

/* recovery/shell_cmds_usb_test.c (group N: static -> exported) */
int shell_cmd_usb(const char *args);
int shell_cmd_usbdev(const char *args);
int shell_cmd_usb_core_test(const char *args);
int shell_cmd_usb_kbd_test(const char *args);
int shell_cmd_usb_mouse_test(const char *args);
int shell_cmd_usb_storage_test(const char *args);
int shell_cmd_usb_serial_test(const char *args);
int shell_cmd_usb_hotplug_test(const char *args);
int shell_cmd_usb_hub_test(const char *args);

#ifdef __cplusplus
}
#endif

#endif /* OC_USB_CMDS_H */
