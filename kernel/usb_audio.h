/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10c
 * File: kernel/usb_audio.h
 * Purpose: USB Audio Class (UAC 1.0) driver interface (usb_audio.c).
 *
 * L1 extension surface:
 *
 *   int usb_audio_init(void *usb_dev);
 *       Initialise the USB stack (usb.c) if needed, enumerate the bus,
 *       find a USB Audio Class device, parse its descriptors and
 *       register a SND_TYPE_USB_AUDIO device with the snd framework.
 *       `usb_dev` is normally NULL (enumerate internally); a non-NULL
 *       pointer initialises exactly that device.  -1 when no UAC
 *       device is present.
 *
 *   void usb_audio_print_state(void);
 *       One-line status for the `usbaudio` shell command.
 */
#ifndef OC_USB_AUDIO_H
#define OC_USB_AUDIO_H

int usb_audio_init(void *usb_dev);
void usb_audio_print_state(void);

/* WP-10d: register the UAC driver as a USB class driver so hot-plugged
 * audio devices attach automatically (probe = usb_audio_init(dev)). */
int  usb_audio_class_register(void);

#endif /* OC_USB_AUDIO_H */
