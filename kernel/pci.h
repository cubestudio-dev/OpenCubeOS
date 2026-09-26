/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-06
 * File: kernel/pci.h
 * Purpose: PCI bus driver - scan bus, read/write config space.
 */
#ifndef OC_PCI_H
#define OC_PCI_H

#include "types.h"

/* PCI config address port and data port. */
#define PCI_CONFIG_ADDR  0xCF8
#define PCI_CONFIG_DATA  0xCFC

/* Read a 32-bit value from PCI config space. */
u32 pci_read_config(u8 bus, u8 dev, u8 func, u8 offset);

/* Write a 32-bit value to PCI config space. */
void pci_write_config(u8 bus, u8 dev, u8 func, u8 offset, u32 value);

/* Find a device by vendor+device ID. Returns 0 on success. */
int pci_find_device(u16 vendor, u16 device, u8 *bus_out, u8 *dev_out, u8 *func_out);

/* Find a device by class code. Returns 0 on success. */
int pci_find_class(u32 class_code, u8 *bus_out, u8 *dev_out, u8 *func_out);

/* Read a BAR (base address register) and return the base address. */
u32 pci_read_bar(u8 bus, u8 dev, u8 func, int bar_index);

/* Initialize PCI subsystem (just prints a banner). */
void pci_init(void);

/* List all PCI devices (for `lspci` command). */
void pci_list_devices(void);

/* Enable bus mastering and I/O/MEM access for a PCI device. */
void pci_enable_device(u8 bus, u8 dev, u8 func);

#endif /* OC_PCI_H */
