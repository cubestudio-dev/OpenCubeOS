/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 2.0.0 - services/usb L1 surface (SYS_L1SVC cat "usb"):
 * the USB family (host/device status + class driver tests).  Bodies
 * stay in recovery/shell_cmds_usb_test.c (exported as usb_cmd_usb*
 * by group N); the kernel-side poll thread (driver_usb_poll_start)
 * stays with the shell module init. */
#include "core_registry.h"
#include "l1svc_defs.h"
#include "usb_cmds.h"
#include "core_kcap.h"

typedef int (*usb_cmd_fn_t)(const char *);

static int usb_l1_dispatch(u32 op, const char *arg, char *out, int cap) {
    if (!out || cap <= 0) return -1;
    usb_cmd_fn_t fn = 0;
    switch (op) {
        case L1SVC_USB_STATUS:        fn = usb_cmd_usb;               break;
        case L1SVC_USB_DEV:           fn = usb_cmd_usbdev;            break;
        case L1SVC_USB_CORE_TEST:     fn = usb_cmd_usb_core_test;     break;
        case L1SVC_USB_KBD_TEST:      fn = usb_cmd_usb_kbd_test;      break;
        case L1SVC_USB_MOUSE_TEST:    fn = usb_cmd_usb_mouse_test;    break;
        case L1SVC_USB_STORAGE_TEST:  fn = usb_cmd_usb_storage_test;  break;
        case L1SVC_USB_SERIAL_TEST:   fn = usb_cmd_usb_serial_test;   break;
        case L1SVC_USB_HOTPLUG_TEST:  fn = usb_cmd_usb_hotplug_test;  break;
        case L1SVC_USB_HUB_TEST:      fn = usb_cmd_usb_hub_test;      break;
        default: return -1;
    }
    return core_kcap_run_fn(fn, arg, out, cap) == 0 ? 0 : -1;
}

/* v2.0.0 group N: usb service identity (host stack + class drivers);
 * controller init stays in kmain/driver init. */
OC_SERVICE_DECL_L1("usb", "usb service (host stack + class drivers + ring-3 surface)",
                   (oc_service_init_fn_t)0, (oc_service_late_fn_t)0,
                   usb_l1_dispatch);
