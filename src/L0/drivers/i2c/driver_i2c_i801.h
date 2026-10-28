/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-input_chipset
 * File: drivers/i2c/driver_i2c_i801.h
 * Purpose: Intel i801 (ICH/ICHx/PCH) + PIIX4 SMBus host controller.
 *          Both share the register layout (HST_STS/CNT/CMD/ADD/DAT0/
 *          DAT1/BLKDAT); they differ in where the PCI config space
 *          keeps the I/O base and in the extra ICH registers (AUXCTL,
 *          I2C_EN) which are gated on the detected device.
 */
#ifndef OC_DRIVER_I2C_I801_H
#define OC_DRIVER_I2C_I801_H

#include "types.h"

/* Probe PCI for the controller, map the I/O window, register the bus.
 * Returns 0 when a controller was found and registered. */
int driver_i2c_i801_init(void);

/* Status for the i2ctest command. */
int  driver_i2c_i801_present(void);
u16  driver_i2c_i801_base(void);
u32  driver_i2c_i801_cfg_raw(void);    /* raw PCI config dword at the base register */
u8   driver_i2c_i801_sts_probe(void);  /* first HST_STS byte read through the I/O window */
int  driver_i2c_i801_is_piix4(void);   /* 1 = PIIX4 (7113), 0 = i801 ICH */
int  driver_i2c_i801_bus_id(void);
u32  driver_i2c_i801_xfer_count(void);
u32  driver_i2c_i801_error_count(void);

/* Last transaction error code (diagnostics). */
const char* driver_i2c_i801_last_error(void);

#endif /* OC_DRIVER_I2C_I801_H */
