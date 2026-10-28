/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-input_chipset
 * File: drivers/i2c/driver_i2c_dw.h
 * Purpose: Synopsys DesignWare I2C controller (polled master mode).
 *          Covers the full 7-bit and 10-bit addressing paths. On the
 *          machines this ISO boots on the controller appears as an
 *          ACPI platform device (\_SB.I2Cn with _CRS SystemMemory) -
 *          QEMU's pc/q35 machines do not ship one, so the E2E I2C
 *          traffic is exercised through the i801/PIIX4 controller
 *          while this driver's register paths are covered by the
 *          i2ctest unit checks (honest reporting in the docs).
 */
#ifndef OC_DRIVER_I2C_DW_H
#define OC_DRIVER_I2C_DW_H

#include "types.h"

/* Attach a DesignWare controller at a memory-mapped base (identity
 * mapped) and register it as a bus. Returns the bus id (>= 0) or
 * -1 when the controller is absent. */
int driver_i2c_dw_attach(u64 mmio_base, const char *name);

/* Register-level self checks (no external device needed):
 * verifies the IC enable/disable handshake, TX/RX FIFO level
 * registers and the abort-source cleanup. Returns 0 when the state
 * machine behaves. */
int driver_i2c_dw_selftest(int bus_id);

#endif /* OC_DRIVER_I2C_DW_H */
