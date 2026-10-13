/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-input_chipset
 * File: drivers/i2c/driver_i2c.h
 * Purpose: I2C core - bus registry + transfer API with 7-bit and
 *          10-bit addressing. Controllers (Intel i801/PIIX4, DesignWare)
 *          register a bus here; consumers (SMBus layer, HID over I2C
 *          touchpad, i2cdetect/i2cget/i2cset shell commands) talk to
 *          devices through driver_i2c_transfer().
 */
#ifndef OC_DRIVER_I2C_H
#define OC_DRIVER_I2C_H

#include "types.h"

#define I2C_MAX_BUSES 4
#define I2C_TENBIT_FLAG 0xF000   /* 10-bit marker inside u16 addr */

typedef struct driver_i2c_bus driver_i2c_bus_t;

/* Controller backend: one combined transaction
 * (optional write `wlen` bytes, then optional read `rlen` bytes). */
typedef int (*driver_i2c_xfer_fn)(driver_i2c_bus_t *bus,
                                  u16 addr, int addr10,
                                  const u8 *wbuf, int wlen,
                                  u8 *rbuf, int rlen);

struct driver_i2c_bus {
    char             name[24];
    driver_i2c_xfer_fn xfer;
    void            *priv;
    int              id;          /* filled by the core */
    int              inited;
    u32              xfer_count;
    u32              error_count;
};

/* Register a bus (name copied). Returns the bus id (>= 0) or -1. */
int driver_i2c_register(driver_i2c_bus_t *bus, const char *name);

int driver_i2c_count(void);
driver_i2c_bus_t* driver_i2c_get(int id);

/* One combined transfer on bus `id`. addr10 selects 10-bit addressing
 * (i801-class SMBus controllers reject it with -2; DesignWare
 * supports it natively). Returns 0 on success, -1 on bus error,
 * -2 on NACK / unsupported. */
int driver_i2c_transfer(int id, u16 addr, int addr10,
                        const u8 *wbuf, int wlen, u8 *rbuf, int rlen);

#endif /* OC_DRIVER_I2C_H */
