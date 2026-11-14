/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-input_chipset
 * File: drivers/pci/driver_pcie.h
 * Purpose: PCI/PCIe bus layer on top of driver_pci: full bus walk with
 *          a device cache, BAR sizing/assignment, PCIe MMCONFIG
 *          (extended config space via the ACPI MCFG table) and the
 *          MSI / MSI-X programming helpers.
 *
 * The existing driver_pci_* API (WP-06) is untouched - this module is
 * additive and consumes it for legacy config access.
 */
#ifndef OC_DRIVER_PCIE_H
#define OC_DRIVER_PCIE_H

#include "types.h"

#define PCIE_MAX_DEVICES 64

/* One discovered PCI function. */
typedef struct driver_pcie_dev {
    u8  bus, dev, fn;
    u8  valid;
    u16 vendor, device;
    u8  base_class, sub_class, prog_if, revision;
    u8  header_type;
    u16 irq_pin, irq_line;
    /* BARs: raw value + decoded size (0 = absent) + type */
    u32 bar_raw[6];
    u32 bar_size[6];
    u8  bar_flags[6];       /* bit0: I/O, bit1: 64-bit, bit2: prefetch */
    /* capability offsets (0 = absent) */
    u8  cap_msi, cap_msix, cap_pcie;
    u16 pcie_type;          /* from PCIe cap, 0 when not PCIe */
    /* MSI-X table location */
    u8  msix_bar;
    u32 msix_offset, msix_count;
} driver_pcie_dev_t;

/* Enumerate every bus (depth-first through bridges) and fill the
 * device cache. Re-running re-scans from scratch. Returns the number
 * of devices found. Safe to call before driver_acpi_init (MMCONFIG
 * appears automatically once MCFG is parsed). */
int  driver_pcie_scan(void);

/* Cache access. Returns the entry or NULL. */
const driver_pcie_dev_t* driver_pcie_get(int index);
int  driver_pcie_count(void);

/* Extended config access: MMCONFIG 0..4095 when the MCFG table covers
 * the bus, legacy CF8/CFC clamped to 0..0xFF otherwise. */
u32  driver_pcie_read_config(u8 bus, u8 dev, u8 func, u16 offset);
void driver_pcie_write_config(u8 bus, u8 dev, u8 func, u16 offset, u32 value);

/* 1 when MMCONFIG (ECAM) is active for segment 0. */
int  driver_pcie_mmconfig_active(void);
/* MMCONFIG base for segment 0 (0 = absent). */
u64  driver_pcie_mmconfig_base(void);

/* BAR sizing + assignment. driver_pcie_assign_bars() walks the cache
 * and, for every unassigned but sized memory/I/O BAR, programs a base
 * address from the kernel's allocation pool. Returns the number of
 * BARs programmed. */
int  driver_pcie_assign_bars(void);

/* MSI / MSI-X programming. Both return 0 on success:
 *   driver_pcie_msi_enable(dev, vector)      - single-message MSI,
 *     arms the device to write LAPIC messages for `vector`.
 *   driver_pcie_msix_enable(dev, entry, vec) - MSI-X table entry.
 *   driver_pcie_msi_disable(dev)             - both flavors. */
int  driver_pcie_msi_enable(const driver_pcie_dev_t *dev, u8 vector);
int  driver_pcie_msix_enable(const driver_pcie_dev_t *dev, int entry, u8 vector);
int  driver_pcie_msi_disable(const driver_pcie_dev_t *dev);

/* Capability walk dump (used by `pcie` / `pcidump`). */
void driver_pcie_print_caps(const driver_pcie_dev_t *d);

#endif /* OC_DRIVER_PCIE_H */
