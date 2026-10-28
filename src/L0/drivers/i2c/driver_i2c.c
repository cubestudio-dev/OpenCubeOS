/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-input_chipset
 * File: drivers/i2c/driver_i2c.c
 * Purpose: I2C core - bus registry + dispatch (see driver_i2c.h).
 */
#include "driver_i2c.h"
#include "lib_string.h"

static driver_i2c_bus_t g_buses[I2C_MAX_BUSES];
static int g_bus_count;

int driver_i2c_register(driver_i2c_bus_t *bus, const char *name) {
    if (!bus || !bus->xfer || !name) return -1;
    if (g_bus_count >= I2C_MAX_BUSES) return -1;
    /* BUG-FIX (WP-10-input_chipset): the old code memset() the CALLER's
     * bus struct - wiping bus->xfer/priv - and THEN copied it into the
     * registry slot, so every registered bus ended up with xfer==NULL
     * and driver_i2c_transfer() bailed at the !b->xfer gate before a
     * single transaction ever reached the controller (SMBus scan saw
     * nothing, every i2cget/i2cset NACKed with an empty last-error).
     * Fill the registry slot directly from the caller's fields. */
    driver_i2c_bus_t *slot = &g_buses[g_bus_count];
    memset(slot, 0, sizeof(*slot));
    for (int i = 0; i < 23 && name[i]; i++) slot->name[i] = name[i];
    slot->name[23] = 0;
    slot->xfer = bus->xfer;
    slot->priv = bus->priv;
    slot->id   = g_bus_count;
    slot->inited = 1;
    return g_bus_count++;
}

int driver_i2c_count(void) { return g_bus_count; }

driver_i2c_bus_t* driver_i2c_get(int id) {
    if (id < 0 || id >= g_bus_count) return NULL;
    return &g_buses[id];
}

int driver_i2c_transfer(int id, u16 addr, int addr10,
                        const u8 *wbuf, int wlen, u8 *rbuf, int rlen) {
    if (id < 0 || id >= g_bus_count) return -1;
    driver_i2c_bus_t *b = &g_buses[id];
    if (!b->inited || !b->xfer) return -1;
    /* BUG-FIX (WP-10-input_chipset): a zero-length transfer IS valid -
     * it is the SMBus/I2C QUICK command (address probe, no data phase)
     * and the only way i2cdetect/SMBus scans can discover devices. The
     * old gate rejected wbuf==NULL && rbuf==NULL, so every quick probe
     * died in the core and the bus scanned as empty even with the
     * controller and devices fully alive. Only non-zero lengths must
     * come with a buffer. */
    if ((wlen && !wbuf) || (rlen && !rbuf)) return -1;
    int rc = b->xfer(b, addr, addr10, wbuf, wlen, rbuf, rlen);
    if (rc == 0) b->xfer_count++;
    else         b->error_count++;
    return rc;
}
