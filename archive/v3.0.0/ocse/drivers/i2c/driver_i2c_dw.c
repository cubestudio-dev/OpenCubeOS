/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-input_chipset
 * File: drivers/i2c/driver_i2c_dw.c
 * Purpose: Synopsys DesignWare I2C controller, polled master mode.
 *
 * Register map (DW_apb_i2c databook, offsets in bytes):
 *   IC_CON 0x00  master_en(0) speed(1:2) 10bitaddr_master(4) restart(5)
 *   IC_TAR 0x04  target address, bit12 = 10-bit marker
 *   IC_DATA_CMD 0x10  cmd/data: bit8 STOP, bit9 RESTART, bit8? read flag is
 *                     bit8 = CMD (0=write,1=read), bit9 STOP, bit10 RESTART
 *   IC_SS_SCL_HCNT 0x14 / IC_SS_SCL_LCNT 0x18  standard speed timing
 *   IC_INTR_MASK 0x30 / IC_INTR_STAT 0x2C
 *   IC_RXFLR 0x78 / IC_TXFLR 0x74 / IC_TX_ABRT_SOURCE 0x80
 *   IC_ENABLE 0x6C / IC_ENABLE_STATUS 0x9C / IC_CLR_TX_ABRT 0xA4
 */
#include "driver_i2c_dw.h"
#include "driver_i2c.h"
#include "lib_string.h"
#include "lib_log.h"

#include <stdint.h>

#define DW_CON          0x00
#define DW_TAR          0x04
#define DW_DATA_CMD     0x10
#define DW_SS_SCL_HCNT  0x14
#define DW_SS_SCL_LCNT  0x18
#define DW_INTR_MASK    0x30
#define DW_TXFLR        0x74
#define DW_RXFLR        0x78
#define DW_TX_ABRT      0x80
#define DW_CLR_TX_ABRT  0xA4
#define DW_ENABLE       0x6C
#define DW_ENABLE_STAT  0x9C

#define DW_TXFLR_DEPTH  32

typedef struct {
    volatile u8 *base;
    int bus_id;
    int attached;
} dw_priv_t;

static dw_priv_t g_dw[I2C_MAX_BUSES];
static int g_dw_count;

static inline u32 dw_rd(dw_priv_t *p, u32 off) {
    return *(volatile u32*)(uintptr_t)(p->base + off);
}
static inline void dw_wr(dw_priv_t *p, u32 off, u32 v) {
    *(volatile u32*)(uintptr_t)(p->base + off) = v;
}

static int dw_wait_idle(dw_priv_t *p) {
    for (int t = 0; t < 1000000; t++) {
        if ((dw_rd(p, DW_ENABLE_STAT) & 1) == 0) return 0;   /* !ACTIVE */
        for (volatile int i = 0; i < 20; i++) { }
    }
    return -1;
}

static int dw_xfer(driver_i2c_bus_t *bus, u16 addr, int addr10,
                   const u8 *wbuf, int wlen, u8 *rbuf, int rlen) {
    (void)bus;
    dw_priv_t *p = (dw_priv_t*)bus->priv;
    if (!p || !p->attached) return -1;

    /* configure + enable */
    dw_wr(p, DW_ENABLE, 0);
    u32 con = dw_rd(p, DW_CON);
    con |= 1u;                       /* MASTER_MODE */
    con = (con & ~0x6u) | (1u << 1); /* standard speed */
    if (addr10) con |= (1u << 4); else con &= ~(1u << 4);
    con |= (1u << 5);                /* RESTART_EN */
    dw_wr(p, DW_CON, con);

    u32 tar = addr & 0x3FF;
    if (addr10) tar |= (1u << 12);
    dw_wr(p, DW_TAR, tar);

    dw_wr(p, DW_INTR_MASK, 0);       /* polled: no interrupts */
    (void)dw_rd(p, DW_CLR_TX_ABRT);
    dw_wr(p, DW_ENABLE, 1);

    int total = wlen + rlen;
    int wi = 0, ri = 0, sent = 0;
    while (sent < total) {
        /* check abort */
        if (dw_rd(p, DW_TX_ABRT) != 0) {
            (void)dw_rd(p, DW_CLR_TX_ABRT);
            dw_wr(p, DW_ENABLE, 0);
            (void)dw_wait_idle(p);
            return -2;
        }
        while (sent < total &&
               dw_rd(p, DW_TXFLR) < DW_TXFLR_DEPTH) {
            u32 cmd;
            if (wi < wlen) {
                cmd = wbuf[wi++];
            } else {
                cmd = 0x100u | (sent == total - 1 ? 0x200u : 0);  /* CMD=read, STOP on last */
                sent++;   /* count the read command */
                dw_wr(p, DW_DATA_CMD, cmd);
                /* read it back once RXFLR shows data */
                for (int t = 0; t < 100000; t++) {
                    if (dw_rd(p, DW_RXFLR) > 0) break;
                    for (volatile int i = 0; i < 10; i++) { }
                }
                if (ri < rlen) rbuf[ri++] = (u8)dw_rd(p, DW_DATA_CMD);
                continue;
            }
            if (sent == total - 1 && wi == wlen && !rlen) cmd |= 0x200u;  /* STOP */
            dw_wr(p, DW_DATA_CMD, cmd);
            sent++;
        }
    }

    int rc = dw_wait_idle(p);
    dw_wr(p, DW_ENABLE, 0);
    (void)dw_wait_idle(p);
    return rc == 0 ? 0 : -1;
}

int driver_i2c_dw_attach(u64 mmio_base, const char *name) {
    if (!mmio_base || g_dw_count >= I2C_MAX_BUSES) return -1;
    dw_priv_t *p = &g_dw[g_dw_count];
    p->base = (volatile u8*)(uintptr_t)mmio_base;
    p->attached = 1;

    driver_i2c_bus_t b;
    b.xfer = dw_xfer;
    b.priv = p;
    int id = driver_i2c_register(&b, name ? name : "dw-i2c");
    if (id >= 0) {
        p->bus_id = id;
        g_dw_count++;
    }
    return id;
}

int driver_i2c_dw_selftest(int bus_id) {
    driver_i2c_bus_t *b = driver_i2c_get(bus_id);
    if (!b || !b->priv) return -1;
    dw_priv_t *p = (dw_priv_t*)b->priv;
    if (!p->attached) return -1;

    /* enable/disable handshake */
    dw_wr(p, DW_ENABLE, 1);
    for (int t = 0; t < 10000; t++)
        if (dw_rd(p, DW_ENABLE_STAT) & 1) break;
    int en_ok = (dw_rd(p, DW_ENABLE_STAT) & 1) != 0;
    dw_wr(p, DW_ENABLE, 0);
    for (int t = 0; t < 10000; t++)
        if ((dw_rd(p, DW_ENABLE_STAT) & 1) == 0) break;
    int dis_ok = (dw_rd(p, DW_ENABLE_STAT) & 1) == 0;

    /* FIFO level registers must read as valid (0 <= level <= depth) */
    u32 tx = dw_rd(p, DW_TXFLR);
    u32 rx = dw_rd(p, DW_RXFLR);
    int fifo_ok = (tx <= DW_TXFLR_DEPTH) && (rx <= DW_TXFLR_DEPTH);

    /* abort source must be clearable */
    dw_wr(p, DW_ENABLE, 1);
    (void)dw_rd(p, DW_CLR_TX_ABRT);
    int abort_ok = dw_rd(p, DW_TX_ABRT) == 0;
    dw_wr(p, DW_ENABLE, 0);

    return (en_ok && dis_ok && fifo_ok && abort_ok) ? 0 : -1;
}
