/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP02-ocire_recovery
 * File: ocire/ocire_ata.c
 * Purpose: ATA PIO polling driver (see ocire_ata.h).
 *
 * Commands used: IDENTIFY DEVICE (0xEC) for probing, READ SECTORS
 * (0x20) and WRITE SECTORS (0x30) with 28-bit LBA.  The recovery
 * volumes (a 16 MiB OCIRE disk, a 128 MiB OCOS disk) are far below
 * the 2^28-sector LBA28 ceiling, so LBA48 is not needed for the WP-02
 * scope; devices that only report LBA48 sizes still work for the
 * first 128 GiB, which covers every recovery target this kernel
 * touches.
 */

#include "ocire_ata.h"
#include "ocire_io.h"
#include "ocire_util.h"

/* Primary bus 0x1F0, secondary 0x170. */
static const u16 BUS_BASE[2] = { 0x1F0, 0x170 };

/* Register offsets from the bus base. */
#define R_DATA  0
#define R_ERR   1
#define R_NSECT 2
#define R_LBA0  3
#define R_LBA1  4
#define R_LBA2  5
#define R_DEV   6
#define R_STAT  7
#define R_CTRL  0x206          /* control port (alternate status) */

/* Status bits. */
#define ST_BSY  0x80
#define ST_DRDY 0x40
#define ST_DRQ  0x08
#define ST_ERR  0x01

static ocire_ata_dev_t g_devs[OCIRE_ATA_MAX_DEVS];
static int g_ndevs;

const ocire_ata_dev_t *ocire_ata_dev(int idx) {
    if (idx < 0 || idx >= g_ndevs) return (ocire_ata_dev_t *)0;
    return &g_devs[idx];
}

/* Wait for BSY to clear (up to ~30 ms in 400 ns units).  Returns the
 * final status byte, or -1 on timeout. */
static int wait_bsy(u16 base) {
    for (int i = 0; i < 100000; i++) {
        u8 st = oc_inb(base + R_STAT);
        if (!(st & ST_BSY)) return st;
        oc_io_wait();
    }
    return -1;
}

/* Wait for DRQ (or ERR) after BSY cleared.  0 = DRQ ok, -1 = error
 * or timeout. */
static int wait_drq(u16 base) {
    for (int i = 0; i < 100000; i++) {
        u8 st = oc_inb(base + R_STAT);
        if (st & ST_ERR) return -1;
        if (st & ST_DRQ) return 0;
        oc_io_wait();
    }
    return -1;
}

static void select_dev(u16 base, int slave) {
    /* 0xE0 = LBA mode, master; +0x10 selects the slave. */
    oc_outb(base + R_DEV, (u8)(0xE0 | (slave ? 0x10 : 0x00)));
    oc_io_wait();
    oc_io_wait();
}

static int identify(int bus, int slave, ocire_ata_dev_t *out) {
    u16 base = BUS_BASE[bus];
    select_dev(base, slave);
    oc_outb(base + R_NSECT, 0);
    oc_outb(base + R_LBA0, 0);
    oc_outb(base + R_LBA1, 0);
    oc_outb(base + R_LBA2, 0);
    oc_outb(base + 7, 0xEC);            /* IDENTIFY DEVICE */

    /* A missing drive leaves the bus floating: status 0 or the LBA0
     * probe port stays 0 (the classic check for an empty slot). */
    oc_io_wait();
    u8 st = oc_inb(base + R_STAT);
    if (st == 0) return -1;
    if (oc_inb(base + R_LBA2) == 0) return -1;

    int s = wait_bsy(base);
    if (s < 0) return -1;
    if (s & ST_ERR) return -1;          /* packet device or none */
    if (wait_drq(base) != 0) return -1;

    u16 id[256];
    for (int i = 0; i < 256; i++) {
        id[i] = oc_inw(base + R_DATA);
    }

    /* Sanity: non-zero sector count and the "fixed disk" flag. */
    u64 lba28 = ((u64)id[60] << 0) | ((u64)id[61] << 16);
    if (lba28 == 0) return -1;

    out->present = 1;
    out->bus = bus;
    out->slave = slave;
    out->sectors = lba28;
    /* Model string: words 27..46, byte-swapped pairs. */
    int n = 0;
    for (int i = 27; i <= 46 && n < 15; i++) {
        char a = (char)(id[i] >> 8);
        char b = (char)(id[i] & 0xFF);
        if (a && a != ' ') out->name[n++] = a;
        if (b && b != ' ') out->name[n++] = b;
    }
    out->name[n] = 0;
    return 0;
}

int ocire_ata_scan(void) {
    g_ndevs = 0;
    for (int bus = 0; bus < 2; bus++) {
        for (int slave = 0; slave < 2; slave++) {
            if (g_ndevs >= OCIRE_ATA_MAX_DEVS) break;
            ocire_ata_dev_t d;
            oc_memset(&d, 0, sizeof(d));
            if (identify(bus, slave, &d) == 0) {
                g_devs[g_ndevs++] = d;
            }
        }
    }
    return g_ndevs;
}

static int do_pio(int idx, u64 lba, u32 count, void *buf, int is_write) {
    if (idx < 0 || idx >= g_ndevs) return -1;
    if (count == 0) return 0;
    if (lba + count > g_devs[idx].sectors) return -2;   /* out of range */
    if (lba >= 0x10000000ull) return -3;                /* beyond LBA28 */

    u16 base = BUS_BASE[g_devs[idx].bus];
    u16 *p = (u16 *)buf;

    while (count > 0) {
        u32 n = (count > 256) ? 256 : count;
        select_dev(base, g_devs[idx].slave);
        oc_outb(base + R_NSECT, (u8)((n == 256) ? 0 : n));
        oc_outb(base + R_LBA0, (u8)(lba & 0xFF));
        oc_outb(base + R_LBA1, (u8)((lba >> 8) & 0xFF));
        oc_outb(base + R_LBA2, (u8)((lba >> 16) & 0xFF));
        oc_outb(base + R_DEV, (u8)(0xE0 | (g_devs[idx].slave ? 0x10 : 0) |
                                   ((lba >> 24) & 0x0F)));
        oc_outb(base + 7, is_write ? 0x30 : 0x20);

        if (is_write) {
            for (u32 s = 0; s < n; s++) {
                int st = wait_bsy(base);
                if (st < 0) return -4;
                if (wait_drq(base) != 0) return -5;
                for (int i = 0; i < 256; i++) {
                    oc_outw(base + R_DATA, *p++);
                }
            }
        } else {
            for (u32 s = 0; s < n; s++) {
                int st = wait_bsy(base);
                if (st < 0) return -6;
                if (wait_drq(base) != 0) return -7;
                for (int i = 0; i < 256; i++) {
                    *p++ = oc_inw(base + R_DATA);
                }
            }
        }

        lba += n;
        count -= n;
    }

    /* Flush the drive's write cache path: wait for BSY clear so the
     * data is on media before the caller inspects the disk image. */
    if (is_write) {
        if (wait_bsy(base) < 0) return -8;
    }
    return 0;
}

int ocire_ata_read(int idx, u64 lba, u32 count, void *buf) {
    return do_pio(idx, lba, count, buf, 0);
}

int ocire_ata_write(int idx, u64 lba, u32 count, const void *buf) {
    return do_pio(idx, lba, count, (void *)buf, 1);
}
