/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-input_chipset
 * File: drivers/i2c/driver_smbus.c
 * Purpose: SMBus protocol implementation over the I2C core.
 *
 * Semantics per the SMBus 2.0 spec:
 *   Quick           : zero-length START/STOP address probe
 *   Byte            : single byte with optional command in flight
 *   Byte Data       : Cmd + one data byte
 *   Word Data       : Cmd + two data bytes (little endian on the wire)
 *   Process Call    : Cmd + word, device swaps the word, reads it back
 *   Block R/W       : Cmd + count + up to 32 bytes
 * Device scan: quick() over 0x03..0x77 (skipping reserved addresses).
 */
#include "driver_smbus.h"
#include "driver_i2c.h"
#include "driver_i2c_i801.h"
#include "lib_string.h"
#include "lib_log.h"

static int g_bus = -1;
static smbus_device_t g_devs[SMBUS_MAX_DEVICES];
static int g_dev_count;

int driver_smbus_bus_id(void) { return g_bus; }

int driver_smbus_device_count(void) { return g_dev_count; }

const smbus_device_t* driver_smbus_device(int idx) {
    if (idx < 0 || idx >= g_dev_count) return NULL;
    return &g_devs[idx];
}

/* ---- protocol operations ---- */

int driver_smbus_quick(int bus, u8 addr) {
    return driver_i2c_transfer(bus, addr, 0, NULL, 0, NULL, 0);
}

int driver_smbus_read_byte(int bus, u8 addr) {
    u8 v = 0;
    if (driver_i2c_transfer(bus, addr, 0, NULL, 0, &v, 1) != 0) return -1;
    return v;
}

int driver_smbus_write_byte(int bus, u8 addr, u8 value) {
    return driver_i2c_transfer(bus, addr, 0, &value, 1, NULL, 0);
}

int driver_smbus_read_byte_data(int bus, u8 addr, u8 reg) {
    u8 r = reg, v = 0;
    if (driver_i2c_transfer(bus, addr, 0, &r, 1, &v, 1) != 0) return -1;
    return v;
}

int driver_smbus_write_byte_data(int bus, u8 addr, u8 reg, u8 value) {
    u8 w[2] = { reg, value };
    return driver_i2c_transfer(bus, addr, 0, w, 2, NULL, 0);
}

int driver_smbus_read_word_data(int bus, u8 addr, u8 reg) {
    u8 r = reg, v[2] = { 0, 0 };
    if (driver_i2c_transfer(bus, addr, 0, &r, 1, v, 2) != 0) return -1;
    return (int)v[0] | ((int)v[1] << 8);
}

int driver_smbus_write_word_data(int bus, u8 addr, u8 reg, u16 value) {
    u8 w[3] = { reg, (u8)(value & 0xFF), (u8)(value >> 8) };
    return driver_i2c_transfer(bus, addr, 0, w, 3, NULL, 0);
}

int driver_smbus_process_call(int bus, u8 addr, u8 reg, u16 value, u16 *out) {
    /* SMBus process call is an atomic write-then-swap transaction; a
     * non-atomic word write + word read decompose would CORRUPT EEPROM
     * contents (the swap leg is not a store on real devices). The
     * i801-class controller in this kernel exposes no atomic process
     * call path yet, so the operation is reported unsupported instead
     * of silently performing a different transaction. */
    (void)bus; (void)addr; (void)reg; (void)value; (void)out;
    return -2;
}

int driver_smbus_read_block(int bus, u8 addr, u8 reg, u8 *out, int maxlen) {
    u8 r = reg;
    u8 blk[34];
    int rc = driver_i2c_transfer(bus, addr, 0, &r, 1, blk, 33);
    if (rc != 0) return rc;
    u8 n = blk[0];   /* the controller surfaces the count in DAT0 */
    if (n > 32) n = 32;
    if (n > maxlen) n = (u8)maxlen;
    for (int i = 0; i < n; i++) out[i] = blk[1 + i];
    return n;
}

int driver_smbus_write_block(int bus, u8 addr, u8 reg, const u8 *in, int len) {
    if (len > 32) len = 32;
    u8 w[34];
    w[0] = reg;
    for (int i = 0; i < len; i++) w[1 + i] = in[i];
    return driver_i2c_transfer(bus, addr, 0, w, 1 + len, NULL, 0);
}

/* ---- device scan ---- */

static void smbus_scan(void) {
    g_dev_count = 0;
    for (u16 addr = 0x03; addr <= 0x77 && g_dev_count < SMBUS_MAX_DEVICES; addr++) {
        if (addr >= 0x30 && addr <= 0x37) continue;   /* reserved (DDC etc) */
        if (addr >= 0x48 && addr <= 0x4F) continue;   /* try anyway later */
        if (driver_smbus_quick(g_bus, (u8)addr) != 0) continue;
        smbus_device_t *d = &g_devs[g_dev_count++];
        d->addr = (u8)addr;
        d->kind = 0;
        for (int i = 0; i < 8; i++) d->eep[i] = 0;
        if (addr >= 0x50 && addr <= 0x57) {
            d->kind = 1;
            for (int i = 0; i < 8; i++) {
                int b = driver_smbus_read_byte_data(g_bus, (u8)addr, (u8)i);
                d->eep[i] = (b < 0) ? 0 : (u8)b;
            }
        } else if (addr >= 0x48 && addr <= 0x4F) {
            d->kind = 2;
        }
    }
}

int driver_smbus_init(void) {
    g_bus = driver_i2c_i801_bus_id();
    if (g_bus < 0) {
        OC_LOG_FAIL("SMBus: no controller bus registered");
        return -1;
    }
    smbus_scan();
    {
        char line[80]; char n[8];
        strcpy(line, "SMBus: ");
        u64_to_str((u64)g_dev_count, n); strcat(line, n);
        strcat(line, " device(s) at scan");
        OC_LOG_OK(line);
    }
    return g_dev_count;
}

/* ---- self test (needs an EEPROM-style device, QEMU smbus-eeprom) ---- */

int driver_smbus_selftest(void) {
    u32 mask = 0;   /* bit0 byte / bit1 word / bit2 block / bit3 nack */
    if (g_bus < 0) return 0xF;

    /* find an EEPROM device */
    int eep = -1;
    for (int i = 0; i < g_dev_count; i++) {
        if (g_devs[i].kind == 1) { eep = i; break; }
    }
    if (eep < 0) {
        return 0xF;   /* no EEPROM present: every check reports honestly */
    }
    u8 addr = g_devs[eep].addr;

    /* 1. byte-data round trip */
    {
        u8 orig = (u8)driver_smbus_read_byte_data(g_bus, addr, 0x00);
        int wr = driver_smbus_write_byte_data(g_bus, addr, 0x00, 0xA5);
        int rd = driver_smbus_read_byte_data(g_bus, addr, 0x00);
        driver_smbus_write_byte_data(g_bus, addr, 0x00, orig);
        if (wr != 0 || rd != 0xA5) mask |= 1u << 0;
    }

    /* 2. word round trip */
    {
        u16 worig = (u16)driver_smbus_read_word_data(g_bus, addr, 0x02);
        int wr = driver_smbus_write_word_data(g_bus, addr, 0x02, 0x1234);
        int rd = driver_smbus_read_word_data(g_bus, addr, 0x02);
        driver_smbus_write_word_data(g_bus, addr, 0x02, worig);
        if (wr != 0 || rd != 0x1234) mask |= 1u << 1;
    }

    /* 3. block write + read-back (16 bytes) */
    {
        u8 wr[16], rd[16];
        for (int i = 0; i < 16; i++) wr[i] = (u8)(0x10 + i);
        int rc_w = driver_smbus_write_block(g_bus, addr, 0x04, wr, 16);
        int rn = driver_smbus_read_block(g_bus, addr, 0x04, rd, 16);
        if (rc_w != 0 || rn != 16) { mask |= 1u << 2; }
        else {
            for (int i = 0; i < 16; i++)
                if (rd[i] != wr[i]) { mask |= 1u << 2; break; }
        }
    }

    /* 4. NACK on an absent address must be an error, not success */
    {
        u8 unused = 0;
        for (u16 a = 0x03; a <= 0x77; a++) {
            if (a >= 0x30 && a <= 0x37) continue;
            int hit = 0;
            for (int i = 0; i < g_dev_count; i++) if (g_devs[i].addr == a) hit = 1;
            if (!hit) {
                if (driver_i2c_transfer(g_bus, a, 0, &unused, 1, NULL, 0) == 0)
                    mask |= 1u << 3;
                break;
            }
        }
    }

    return (int)mask;
}
