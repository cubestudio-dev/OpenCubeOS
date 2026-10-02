/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10c
 * File: kernel/usb.h
 * Purpose: USB host stack (UHCI) interface - implementation: usb.c.
 *
 * WP-10c adds the minimal-but-real USB 1.1 host stack needed for USB
 * audio class devices:
 *
 *   - UHCI controller driver (Intel 8086:7020 piix3 / 8086:7112 piix4,
 *     the controller QEMU's `-usb` puts on the PIIX3)
 *   - blocking control transfers (SETUP + DATA + STATUS TD chains)
 *   - device enumeration (descriptors, SET_ADDRESS / SET_CONFIGURATION
 *     / SET_INTERFACE)
 *   - isochronous OUT scheduling (one ISO TD per 1 ms frame, real DMA
 *     into the audio endpoint)
 *
 * L1 extension surface:
 *
 *   int usb_init(void);
 *       Probe the PCI bus for a UHCI controller, reset it, allocate the
 *       1024-entry frame list and start the schedule.  0 on success,
 *       -1 when no UHCI controller exists.
 *
 *   int usb_enumerate(void);
 *       Poll both root-hub ports; for each attached device run the
 *       enumeration sequence and register a usb_device_t.  Returns the
 *       number of devices found (>= 0).
 *
 *   int usb_num_devices(void);
 *   usb_device_t *usb_get_device(int idx);
 *
 *   int usb_control(usb_device_t *dev, u8 req_type, u8 request,
 *                   u16 value, u16 index, void *buf, u16 len);
 *       Blocking control transfer.  buf/len: data stage (IN) or setup
 *       payload (OUT).  Returns 0 on success.
 *
 *   int usb_set_interface(usb_device_t *dev, u16 interface, u16 alt);
 *       SET_INTERFACE request (activates an alternate setting, e.g.
 *       the UAC1 streaming interface).
 *
 * Device descriptor fields interesting to class drivers (vid/pid/
 * class/subclass) are copied into usb_device_t; the full raw config
 * descriptor is kept so class drivers can parse their own class
 * descriptors.
 */
#ifndef OC_USB_H
#define OC_USB_H

#include "types.h"

#define USB_MAX_DEVICES 4
#define USB_RAW_CFG_MAX 256

typedef struct usb_device {
    u8  addr;             /* USB address (after SET_ADDRESS) */
    u8  port;             /* root hub port */
    u8  speed_ls;         /* 1 = low speed */
    u16 vid, pid;
    u8  class, subclass, protocol;
    u8  max_packet0;

    /* audio-relevant streaming endpoint found by the class driver */
    u8  has_audio_stream;
    u8  iso_out_ep;       /* endpoint address (e.g. 0x01) */
    u16 iso_out_maxpack;  /* wMaxPacketSize of the ISO OUT endpoint */
    u8  audio_if_num;     /* streaming interface number */
    u8  audio_if_alt;     /* alternate setting with the endpoint */

    u8  raw_cfg[USB_RAW_CFG_MAX];
    u16 raw_cfg_len;
    int present;
} usb_device_t;

int  usb_init(void);
int  usb_enumerate(void);
int  usb_num_devices(void);
usb_device_t *usb_get_device(int idx);

int  usb_control(usb_device_t *dev, u8 req_type, u8 request,
                 u16 value, u16 index, void *buf, u16 len);
int  usb_set_interface(usb_device_t *dev, u16 interface, u16 alt);

/* Internal (used by usb_audio.c): schedule one isochronous OUT packet.
 * `data` (<= maxpack bytes) is copied into the DMA buffer which the
 * current frame's ISO TD hands to the device.  Call at frame cadence
 * (1 ms).  Returns 0 on success. */
int  usb_iso_out_submit(usb_device_t *dev, const void *data, u16 len);

/* Status for the shell command. */
void usb_print_state(void);

#endif /* OC_USB_H */
