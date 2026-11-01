/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-input_chipset
 * File: drivers/i2c/driver_i2c_i801.c
 * Purpose: Intel i801 (ICH) + PIIX4 SMBus host controller driver.
 *
 * Register semantics follow the i801/PIIX4 datasheets and the Linux
 * i2c-i801/i2c-piix4 drivers:
 *   HST_STS +0   bit0 HOST_BUSY, bit1 INTR, bit2 DEV_ERR, bit3 BUS_ERR,
 *                bit4 FAILED, bit5 SMBALERT, bit6 INUSE, bit7 BYTE_DONE
 *   HST_CNT +2   protocol in bits 4:2, START = 0x40, KILL = 0x02
 *   HST_CMD +3   command byte (register offset / block first byte)
 *   HST_ADD +4   (7-bit address << 1) | rw   (rw: 1 = read)
 *   HST_DAT0 +5  first data byte / block byte count
 *   HST_DAT1 +6  second data byte (word transfers)
 *   HST_BLKDAT+7 block data window (auto-incrementing)
 * Protocols: QUICK 0x00, BYTE 0x04, BYTE_DATA 0x08, WORD_DATA 0x0C,
 *            PROC_CALL 0x10, BLOCK 0x14, I2C_BLOCK_READ 0x18.
 *
 * PCI attachment: PIIX4 (8086:7113, class 0x0680) keeps the I/O base
 * at config 0x90 and the enable bit at 0xD2 bit0. ICH-family i801
 * (class 0x0C05) keeps it at config 0x20 (SMBBASE) and enables the
 * controller through SMBHSTCFG bit0 at 0x40.
 */
#include "driver_i2c_i801.h"
#include "driver_i2c.h"
#include "driver_pci.h"
#include "core_timer.h"
#include "lib_string.h"

#include <stdint.h>

static inline void i8_outb(u16 p, u8 v)  { __asm__ volatile("outb %0, %1" :: "a"(v), "Nd"(p)); }
static inline u8   i8_inb(u16 p)         { u8 v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(p)); return v; }

/* registers */
#define I801_HST_STS    0x00
#define I801_HST_CNT    0x02
#define I801_HST_CMD    0x03
#define I801_HST_ADD    0x04
#define I801_HST_DAT0   0x05
#define I801_HST_DAT1   0x06
#define I801_HST_BLKDAT 0x07
#define I801_AUX_CTL    0x0d   /* ICH AUXS; AUX_CTL bit1 gates the
                                * auto-incrementing BLKDAT data queue on the
                                * QEMU pm_smbus model and on real ICH silicon */

/* status bits */
#define ST_HOST_BUSY    0x01
#define ST_INTR         0x02
#define ST_DEV_ERR      0x04
#define ST_BUS_ERR      0x08
#define ST_FAILED       0x10
#define ST_SMBALERT     0x20
#define ST_INUSE        0x40
#define ST_BYTE_DONE    0x80

/* protocols (already shifted into CNT bits 4:2) */
#define P_QUICK         0x00
#define P_BYTE          0x04
#define P_BYTE_DATA     0x08
#define P_WORD_DATA     0x0C
#define P_PROC_CALL     0x10
#define P_BLOCK         0x14
#define P_I2C_BLOCK     0x18

#define CNT_START       0x40
#define CNT_KILL        0x02

/* state */
static u16  g_base;
static int  g_piix4;
static int  g_bus_id = -1;
static int  g_present;
static char g_last_err[40];
/* WP-10-input_chipset diagnostics: the raw config-0x90 dword the
 * controller was found with and the first HST_STS byte read through the
 * I/O window (0xFF there means the window does not decode). */
static u32  g_cfg_raw;
static u8   g_sts_probe;

static const char* err_text(int st) {
    if (st & ST_DEV_ERR)  return "NACK/device error";
    if (st & ST_BUS_ERR)  return "bus error/collision";
    if (st & ST_FAILED)   return "transaction failed";
    return "timeout";
}

static void set_err(const char *t) {
    usize i = 0;
    for (; i + 1 < sizeof(g_last_err) && t[i]; i++) g_last_err[i] = t[i];
    g_last_err[i] = 0;
}

/* wait until HOST_BUSY clears; returns the final status or 0 on timeout */
static u8 i801_wait_done(void) {
    for (int t = 0; t < 200000; t++) {
        u8 st = i8_inb(g_base + I801_HST_STS);
        if (!(st & ST_HOST_BUSY)) return st;
        for (volatile int i = 0; i < 50; i++) { }
    }
    return 0;
}

static u8 i801_clear_status(void) {
    u8 st = i8_inb(g_base + I801_HST_STS);
    if (st) i8_outb(g_base + I801_HST_STS, st);
    return i8_inb(g_base + I801_HST_STS);
}

/* one transaction through the host controller */
static int i801_run(u8 proto, u8 addr7, int read,
                    u8 cmd, u8 *d0, u8 *d1,
                    u8 *block, int block_len) {
    if (!g_present) return -1;

    u8 st = i801_clear_status();
    if (st & ST_HOST_BUSY) {
        set_err("controller stuck busy");
        return -1;
    }

    /* Block transfers run with AUX_CTL.AUX_BLK set: the BLKDAT window
     * becomes an auto-incrementing FIFO over the controller's data
     * queue. Without it the QEMU model (and ICH silicon) exposes only a
     * single latch, so the queued bytes would never reach the bus and a
     * multi-byte block read would return the same byte over and over.
     * Set it BEFORE any BLKDAT access and restore it afterwards. */
    if (proto == P_BLOCK) i8_outb(g_base + I801_AUX_CTL, 0x02);  /* AUX_BLK is bit1 */

    i8_outb(g_base + I801_HST_ADD, (u8)((addr7 << 1) | (read ? 1 : 0)));
    if (proto == P_BYTE && !read) {
        i8_outb(g_base + I801_HST_CMD, cmd);
    } else if (proto != P_QUICK && proto != P_BYTE) {
        i8_outb(g_base + I801_HST_CMD, cmd);
    }
    if (!read) {
        switch (proto) {
        case P_BYTE_DATA: i8_outb(g_base + I801_HST_DAT0, d0 ? *d0 : 0); break;
        case P_WORD_DATA: i8_outb(g_base + I801_HST_DAT0, d0 ? *d0 : 0);
                          i8_outb(g_base + I801_HST_DAT1, d1 ? *d1 : 0); break;
        case P_BLOCK:     i8_outb(g_base + I801_HST_DAT0, (u8)block_len);
                          (void)i8_inb(g_base + I801_HST_CNT);  /* reset BLKDAT index */
                          for (int i = 0; i < block_len; i++)
                              i8_outb(g_base + I801_HST_BLKDAT, block[i]);
                          break;
        default: break;
        }
    }

    /* start */
    i8_outb(g_base + I801_HST_CNT, proto);
    i8_outb(g_base + I801_HST_CNT, (u8)(proto | CNT_START));

    u8 fin = i801_wait_done();
    if (fin == 0) {
        /* force-abort */
        i8_outb(g_base + I801_HST_CNT, CNT_KILL);
        i8_outb(g_base + I801_HST_CNT, (u8)(CNT_KILL | CNT_START));
        i801_clear_status();
        set_err("timeout");
        return -1;
    }
    if (fin & (ST_DEV_ERR | ST_BUS_ERR | ST_FAILED)) {
        set_err(err_text(fin));
        i801_clear_status();
        return -2;
    }

    if (read) {
        switch (proto) {
        /* BYTE read lands in DAT0 on the PIIX4/QEMU model (same place
         * Linux i2c-piix4 reads it; the i801 CMD-register variant only
         * exists on later ICH silicon, which this path does not serve). */
        case P_BYTE:      if (d0) *d0 = i8_inb(g_base + I801_HST_DAT0); break;
        case P_BYTE_DATA: if (d0) *d0 = i8_inb(g_base + I801_HST_DAT0); break;
        case P_WORD_DATA: if (d0) *d0 = i8_inb(g_base + I801_HST_DAT0);
                          if (d1) *d1 = i8_inb(g_base + I801_HST_DAT1);
                          break;
        case P_BLOCK: {
            u8 cnt = i8_inb(g_base + I801_HST_DAT0);
            if (cnt > 32) cnt = 32;
            if (cnt > block_len) cnt = (u8)block_len;
            (void)i8_inb(g_base + I801_HST_CNT);   /* reset BLKDAT index */
            for (int i = 0; i < cnt; i++)
                block[i] = i8_inb(g_base + I801_HST_BLKDAT);
            if (d0) *d0 = cnt;
            break;
        }
        default: break;
        }
    }

    if (proto == P_BLOCK) i8_outb(g_base + I801_AUX_CTL, 0x00);

    i801_clear_status();
    return 0;
}

/* I2C core backend */
static int i801_xfer(driver_i2c_bus_t *bus, u16 addr, int addr10,
                     const u8 *wbuf, int wlen, u8 *rbuf, int rlen) {
    (void)bus;
    if (addr10) {
        set_err("10-bit not supported by i801-class SMBus");
        return -2;
    }
    if (addr > 0x7F) { set_err("bad address"); return -1; }

    /* quick command (address probe, no data phases) */
    if (!wlen && !rlen)
        return i801_run(P_QUICK, (u8)addr, 0, 0, NULL, NULL, NULL, 0);

    /* pure read */
    if (!wlen && rlen) {
        if (rlen == 1)
            return i801_run(P_BYTE_DATA, (u8)addr, 1, 0, rbuf, NULL, NULL, 0);
        if (rlen == 2)
            return i801_run(P_WORD_DATA, (u8)addr, 1, 0, rbuf, rbuf + 1, NULL, 0);
        /* Block read: the controller reports the byte count in DAT0 and
         * streams the payload through BLKDAT. Surface it in the SMBus
         * layer's documented self-describing layout - rbuf[0] = count,
         * payload at rbuf[1..] - so a caller can tell how many bytes
         * the device actually sent (the old path dropped the count on
         * the floor and left the payload shifted by one). */
        {
            u8 cnt = 0;
            int rc = i801_run(P_BLOCK, (u8)addr, 1, 0, &cnt, NULL, rbuf, rlen - 1);
            if (rc == 0) {
                if (cnt > (u8)(rlen - 1)) cnt = (u8)(rlen - 1);
                for (int i = cnt; i > 0; i--) rbuf[i] = rbuf[i - 1];
                rbuf[0] = cnt;
            }
            return rc;
        }
    }
    /* pure write */
    if (wlen && !rlen) {
        if (wlen == 1)
            return i801_run(P_BYTE_DATA, (u8)addr, 0, wbuf[0], NULL, NULL, NULL, 0);
        if (wlen == 2)
            return i801_run(P_BYTE_DATA, (u8)addr, 0, wbuf[0], (u8*)&wbuf[1],
                            NULL, NULL, 0);
        if (wlen == 3)
            return i801_run(P_WORD_DATA, (u8)addr, 0, wbuf[0], (u8*)&wbuf[1],
                            (u8*)&wbuf[2], NULL, 0);
        u8 payload[32];
        int n = wlen - 1;
        if (n > 32) n = 32;
        for (int i = 0; i < n; i++) payload[i] = wbuf[1 + i];
        return i801_run(P_BLOCK, (u8)addr, 0, wbuf[0], NULL, NULL, payload, n);
    }
    /* write-then-read (set register, read back): byte data path */
    if (wlen == 1 && rlen == 1)
        return i801_run(P_BYTE_DATA, (u8)addr, 1, wbuf[0], rbuf, NULL, NULL, 0);
    if (wlen == 1 && rlen == 2)
        return i801_run(P_WORD_DATA, (u8)addr, 1, wbuf[0], rbuf, rbuf + 1, NULL, 0);
    if (wlen == 1 && rlen > 2) {
        /* Block read with a command byte (SMBus block read): the count
         * comes back in DAT0, the payload streams through BLKDAT, and
         * the SMBus layer expects the self-describing layout
         * rbuf[0] = count, payload at rbuf[1..]. */
        u8 cnt = 0;
        int rc = i801_run(P_BLOCK, (u8)addr, 1, wbuf[0], &cnt, NULL, rbuf, rlen - 1);
        if (rc == 0) {
            if (cnt > (u8)(rlen - 1)) cnt = (u8)(rlen - 1);
            for (int i = cnt; i > 0; i--) rbuf[i] = rbuf[i - 1];
            rbuf[0] = cnt;
        }
        return rc;
    }
    set_err("unsupported combined transfer");
    return -1;
}

int driver_i2c_i801_init(void) {
    g_present = 0;
    g_base = 0;
    g_piix4 = 0;
    g_last_err[0] = 0;

    u8 bus, dev, fn;

    /* 1. PIIX4 (8086:7113) - QEMU pc machine */
    if (driver_pci_find_device(0x8086, 0x7113, &bus, &dev, &fn) == 0) {
        u32 base = driver_pci_read_config(bus, dev, fn, 0x90) & 0xFFC0;
        g_cfg_raw = driver_pci_read_config(bus, dev, fn, 0x90);
        if (base) {
            g_base = (u16)base;
            g_piix4 = 1;
            /* SMBHSTCFG.HST_EN (config 0xD2 bit 0) gates the I/O
             * window on the PIIX4 (same as Linux i2c-piix4: enable it
             * when the firmware left it off). */
            u32 cfg = driver_pci_read_config(bus, dev, fn, 0xD2);
            driver_pci_write_config(bus, dev, fn, 0xD2, cfg | 0x01u);
        }
    }
    /* 2. ICH-family i801 (class 0x0C05) - real hardware / q35 ICH9 */
    if (!g_base) {
        u8 b2, d2, f2;
        if (driver_pci_find_class_mask(0x0C0500, 0xFFFFFF, 0, &b2, &d2, &f2) == 0) {
            u32 id = driver_pci_read_config(b2, d2, f2, 0x00);
            if ((id & 0xFFFF) == 0x8086) {
                u32 base = driver_pci_read_config(b2, d2, f2, 0x20) & 0xFFE0;
                if (base) {
                    g_base = (u16)base;
                    g_piix4 = 0;
                    /* enable the host controller + I/O decode */
                    driver_pci_enable_device(b2, d2, f2);
                    u32 cfg = driver_pci_read_config(b2, d2, f2, 0x40);
                    driver_pci_write_config(b2, d2, f2, 0x40,
                                            (cfg & ~0xFFu) | 0x01u);  /* SMBHSTCFG.HST_EN */
                }
            }
        }
    }
    if (!g_base) return -1;

    /* sanity: status register must be readable */
    g_sts_probe = i8_inb(g_base + I801_HST_STS);

    driver_i2c_bus_t b;
    b.xfer = i801_xfer;
    b.priv = NULL;
    char name[24] = "i801-smbus";
    if (g_piix4) { name[0] = 'p'; name[1] = 'i'; name[2] = 'i'; name[3] = 'x';
                   name[4] = '4'; name[5] = 0; }
    g_bus_id = driver_i2c_register(&b, name);
    g_present = 1;
    return 0;
}

int  driver_i2c_i801_present(void)   { return g_present; }
u16  driver_i2c_i801_base(void)      { return g_base; }
u32  driver_i2c_i801_cfg_raw(void)   { return g_cfg_raw; }
u8   driver_i2c_i801_sts_probe(void) { return g_sts_probe; }
int  driver_i2c_i801_is_piix4(void)  { return g_piix4; }
int  driver_i2c_i801_bus_id(void)    { return g_bus_id; }
u32  driver_i2c_i801_xfer_count(void) {
    driver_i2c_bus_t *b = driver_i2c_get(g_bus_id);
    return b ? b->xfer_count : 0;
}
u32  driver_i2c_i801_error_count(void) {
    driver_i2c_bus_t *b = driver_i2c_get(g_bus_id);
    return b ? b->error_count : 0;
}
const char* driver_i2c_i801_last_error(void) { return g_last_err; }
