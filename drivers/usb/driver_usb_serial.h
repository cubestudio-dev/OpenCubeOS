/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10d
 * File: kernel/driver_usb_serial.h
 * Purpose: USB serial class driver interface (driver_usb_serial.c) -
 *          CDC-ACM (the standard USB RS-232 profile) with an FTDI
 *          SIO transport for QEMU's usb-serial device and real
 *          FTDI adapters.
 *
 * L1 extension surface:
 *
 *   int  driver_usb_serial_init(void);
 *       Register the CDC-ACM and FTDI class drivers with the USB
 *       core.  Call once before driver_usb_probe_all()/driver_usb_enumerate().
 *
 *   void driver_usb_serial_poll(void);
 *       One bulk-IN drain round on every port (called by the USB
 *       poll thread); received bytes land in the RX ring.
 *
 *   int  driver_usb_serial_num_ports(void);
 *   int  driver_usb_serial_write(const void *buf, int len);
 *       Blocking bulk-OUT write; returns bytes sent or negative.
 *   int  driver_usb_serial_read(void *buf, int max);
 *       Drain up to max bytes from the RX ring; returns bytes read.
 *   void driver_usb_serial_print_state(void);
 *       One status block for the `usb` command.
 */
#ifndef OC_USB_SERIAL_H
#define OC_USB_SERIAL_H

int  driver_usb_serial_init(void);
void driver_usb_serial_poll(void);
int  driver_usb_serial_num_ports(void);
int  driver_usb_serial_write(const void *buf, int len);
int  driver_usb_serial_read(void *buf, int max);
void driver_usb_serial_print_state(void);

#endif /* OC_USB_SERIAL_H */
