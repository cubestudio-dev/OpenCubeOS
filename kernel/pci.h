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

/* WP-10a: minimal PCI device handle passed to driver init entry points
 * (ahci_init / nvme_init / ata_dma_init).  It identifies one function of
 * one device on one bus; drivers read config space / BARs from it. */
typedef struct pci_dev {
    u8 bus;
    u8 dev;
    u8 func;
} pci_dev_t;

/* Read a 32-bit value from PCI config space. */
u32 pci_read_config(u8 bus, u8 dev, u8 func, u8 offset);

/* Write a 32-bit value to PCI config space. */
void pci_write_config(u8 bus, u8 dev, u8 func, u8 offset, u32 value);

/* Find a device by vendor+device ID. Returns 0 on success. */
int pci_find_device(u16 vendor, u16 device, u8 *bus_out, u8 *dev_out, u8 *func_out);

/* Find a device by class code. Returns 0 on success. */
int pci_find_class(u32 class_code, u8 *bus_out, u8 *dev_out, u8 *func_out);

/* WP-10a: find a device by EXACT 24-bit class code (base class bits 23:16,
 * subclass bits 15:8, programming interface bits 7:0).  Unlike
 * pci_find_class() (which only compares the base class), this matches the
 * full triple — required to distinguish AHCI (0x010601) from other mass
 * storage controllers (e.g. IDE 0x010100). Returns 0 on success.
 * `nth` selects the n-th match (0 = first) so multi-controller systems
 * can enumerate every AHCI / IDE controller. */
int pci_find_class_exact(u32 class_code, int nth,
                         u8 *bus_out, u8 *dev_out, u8 *func_out);

/* WP-10a: like pci_find_class_exact, but only the bits set in `mask`
 * (of the 24-bit class triple) are compared.  Use e.g. mask 0xFFFF00 to
 * match any IDE controller regardless of its programming interface
 * (QEMU's PIIX3 reports prog-if 0x80, real chips 0x00/0x8A/...). */
int pci_find_class_mask(u32 class_code, u32 mask, int nth,
                        u8 *bus_out, u8 *dev_out, u8 *func_out);

/* Read a BAR (base address register) and return the base address. */
u32 pci_read_bar(u8 bus, u8 dev, u8 func, int bar_index);

/* Initialize PCI subsystem (just prints a banner). */
void pci_init(void);

/* List all PCI devices (for `lspci` command). */
void pci_list_devices(void);

/* Enable bus mastering and I/O/MEM access for a PCI device. */
void pci_enable_device(u8 bus, u8 dev, u8 func);

#endif /* OC_PCI_H */
