/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-input_chipset
 * File: drivers/i2c/driver_smbus.h
 * Purpose: SMBus protocol layer over the I2C core: quick, byte,
 *          byte-data, word-data, process-call, block read/write, plus
 *          the address scan that enumerates devices on the bus.
 */
#ifndef OC_DRIVER_SMBUS_H
#define OC_DRIVER_SMBUS_H

#include "types.h"

#define SMBUS_MAX_DEVICES 16

typedef struct {
    u8  addr;
    u8  kind;       /* 0 = generic, 1 = EEPROM (0x50-0x57), 2 = sensor-ish */
    u8  eep[8];     /* first 8 EEPROM bytes for 0x50-0x57 devices */
} smbus_device_t;

/* Initialize the SMBus layer (probes the controller, runs the scan).
 * Returns the number of devices found (>= 0), -1 when no bus exists. */
int driver_smbus_init(void);

int  driver_smbus_bus_id(void);
int  driver_smbus_device_count(void);
const smbus_device_t* driver_smbus_device(int idx);

/* protocol operations on bus `bus_id` (usually driver_smbus_bus_id()) */
int driver_smbus_quick(int bus, u8 addr);
int driver_smbus_read_byte(int bus, u8 addr);
int driver_smbus_write_byte(int bus, u8 addr, u8 value);
int driver_smbus_read_byte_data(int bus, u8 addr, u8 reg);
int driver_smbus_write_byte_data(int bus, u8 addr, u8 reg, u8 value);
int driver_smbus_read_word_data(int bus, u8 addr, u8 reg);
int driver_smbus_write_word_data(int bus, u8 addr, u8 reg, u16 value);
int driver_smbus_process_call(int bus, u8 addr, u8 reg, u16 value, u16 *out);
int driver_smbus_read_block(int bus, u8 addr, u8 reg, u8 *out, int maxlen);
int driver_smbus_write_block(int bus, u8 addr, u8 reg, const u8 *in, int len);

/* Per-item self test. Returns 0 when every check passes, otherwise a
 * bitmask of failed items: bit0 = byte-data round trip, bit1 = word-data
 * round trip, bit2 = block write/read-back, bit3 = NACK-on-absent-device
 * must be an error (so the shell can name the failing item). */
int driver_smbus_selftest(void);

#endif /* OC_DRIVER_SMBUS_H */
