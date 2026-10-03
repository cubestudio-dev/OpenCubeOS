/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10d
 * File: kernel/usb_msc.h
 * Purpose: USB Mass Storage Class driver interface (usb_msc.c) -
 *          BOT + SCSI, registered as a blk block device ("usda...").
 *
 * L1 extension surface:
 *
 *   int  usb_msc_init(void);
 *       Register the MSC class driver with the USB core.  Call once
 *       before usb_probe_all()/usb_enumerate().  0 on success.
 *
 *   void usb_msc_poll(void);
 *       Re-checks TEST_UNIT_READY on registered drives so a media
 *       change / unplug is noticed by the next status query.
 *
 *   int  usb_msc_num_devices(void);
 *       Number of USB drives registered with the blk layer.
 *
 *   void usb_msc_print_state(void);
 *       One status block for the `usb` command: drive names, LUNs,
 *       capacity, transfer counters.
 */
#ifndef OC_USB_MSC_H
#define OC_USB_MSC_H

int  usb_msc_init(void);
void usb_msc_poll(void);
int  usb_msc_num_devices(void);
void usb_msc_print_state(void);

#endif /* OC_USB_MSC_H */
