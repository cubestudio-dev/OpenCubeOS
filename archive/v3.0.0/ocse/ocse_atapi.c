/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP06b-ocsecl
 * File: ocse/ocse_atapi.c
 * Purpose: ATAPI PIO driver - see ocse_atapi.h for the layout notes.
 *
 * Protocol (PIO data-in, polled; the kernel registers no ATA IRQ):
 *   PACKET(0xA0) -> wait DRQ -> write the 12-byte CDB as 6 words ->
 *   loop { wait !BSY; if DRQ read the current byte count from
 *   LBA_MID/LBA_HI and drain that many words } until !DRQ && !BSY.
 * QEMU's ATAPI implementation (and every real drive) reports the
 * actual data byte count in the byte-count registers on each DRQ
 * pulse, so multi-packet transfers work without knowing the total in
 * advance.
 */
#include "ocse_atapi.h"
#include "driver_block_blk.h"
#include "lib_string.h"
#include <stdint.h>

/* ---- IDE register offsets (same layout driver_block_ata.c uses) ---- */
#define ATAPI_REG_DATA     0
#define ATAPI_REG_FEATURES 1
#define ATAPI_REG_LBA_MID  4
#define ATAPI_REG_LBA_HI   5
#define ATAPI_REG_DRIVE    6
#define ATAPI_REG_STATUS   7
#define ATAPI_REG_COMMAND  7

#define ATAPI_SR_ERR  0x01
#define ATAPI_SR_DRQ  0x08
#define ATAPI_SR_BSY  0x80

#define ATAPI_CMD_PACKET           0xA0
#define ATAPI_CMD_IDENTIFY_PACKET  0xA1

static const u16 g_chan_base[4] = { 0x1F0, 0x1F0, 0x170, 0x170 };

static inline void outb_(u16 p, u8 v) { __asm__ volatile("outb %0, %1" :: "a"(v), "Nd"(p)); }
static inline void outw_(u16 p, u16 v){ __asm__ volatile("outw %0, %1" :: "a"(v), "Nd"(p)); }
static inline u8   inb_(u16 p)        { u8 v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline u16  inw_(u16 p)        { u16 v; __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline void io_wait_(void)     { outb_(0x80, 0); }

static u8 drive_select_(int drive) {
    return (drive & 1) ? 0xF0 : 0xE0;    /* LBA mode, master/slave */
}

static int wait_bsy_(u16 base) {
    for (int i = 0; i < 200000; i++) {
        u8 s = inb_(base + ATAPI_REG_STATUS);
        if (!(s & ATAPI_SR_BSY)) return 0;
        io_wait_();
    }
    return -1;
}

static int wait_drq_(u16 base) {
    for (int i = 0; i < 200000; i++) {
        u8 s = inb_(base + ATAPI_REG_STATUS);
        if (s & ATAPI_SR_ERR) return -1;
        if (!(s & ATAPI_SR_BSY) && (s & ATAPI_SR_DRQ)) return 0;
        io_wait_();
    }
    return -1;
}

/* Send a 12-byte CDB, then drain every DRQ pulse into `out` (cap
 * bytes). Returns 0 on success, -1 on bus error/timeout. `out` may be
 * NULL when only the completion status matters (TEST UNIT READY). */
static int atapi_packet_(int drive, const u8 cdb[12],
                         void* out, u32 cap) {
    u16 base = g_chan_base[drive];

    outb_(base + ATAPI_REG_DRIVE, drive_select_(drive));
    io_wait_();
    if (wait_bsy_(base) < 0) return -1;

    outb_(base + ATAPI_REG_FEATURES, 0x00);       /* PIO, no overlap */
    outb_(base + ATAPI_REG_LBA_MID, 0xFF);        /* byte count limit lo */
    outb_(base + ATAPI_REG_LBA_HI, 0xFF);         /* byte count limit hi */
    outb_(base + ATAPI_REG_COMMAND, ATAPI_CMD_PACKET);
    if (wait_drq_(base) < 0) return -1;

    /* The CDB goes out as 6 little-endian words. */
    for (int i = 0; i < 6; i++) {
        u16 w = (u16)cdb[i * 2] | ((u16)cdb[i * 2 + 1] << 8);
        outw_(base + ATAPI_REG_DATA, w);
    }

    u8* dst = (u8*)out;
    u32 got = 0;
    for (;;) {
        if (wait_bsy_(base) < 0) return -1;
        u8 st = inb_(base + ATAPI_REG_STATUS);
        if (st & ATAPI_SR_ERR) return -1;
        if (!(st & ATAPI_SR_DRQ)) break;          /* transfer complete */
        u32 bc = (u32)inb_(base + ATAPI_REG_LBA_MID) |
                 ((u32)inb_(base + ATAPI_REG_LBA_HI) << 8);
        for (u32 i = 0; i < bc; i += 2) {
            u16 w = inw_(base + ATAPI_REG_DATA);
            if (dst && got < cap)       dst[got] = (u8)(w & 0xFF);
            if (dst && got + 1 < cap)   dst[got + 1] = (u8)(w >> 8);
            got += 2;
        }
    }
    return 0;
}

/* ---- drive state ---- */
typedef struct {
    int  present;
    int  ata_drive;          /* IDE position 0..3 */
    u64  phys_sectors;       /* 2048-byte sectors on the medium */
} atapi_dev_t;

static atapi_dev_t g_atapi[4];
static int g_atapi_n = 0;

/* One-physical-sector cache shared by the pseudo-sector reader. */
static u8  g_cd_buf[2048];
static i64 g_cd_cached = -1;

static int atapi_read_phys_(atapi_dev_t* d, u64 psec, void* out2048) {
    if (psec >= d->phys_sectors) return -1;
    u8 cdb[12];
    memset(cdb, 0, sizeof(cdb));
    cdb[0] = 0x28;                                   /* READ(10) */
    cdb[2] = (u8)(psec >> 24);                       /* LBA big-endian */
    cdb[3] = (u8)(psec >> 16);
    cdb[4] = (u8)(psec >> 8);
    cdb[5] = (u8)psec;
    cdb[8] = 1;                                      /* 1 block */
    return atapi_packet_(d->ata_drive, cdb, out2048, 2048);
}

/* ---- block-device ops (pseudo 512-byte sectors) ---- */
static int atapi_blk_read(driver_block_device_t* dev, u64 lba, u32 count, void* buf) {
    atapi_dev_t* d = (atapi_dev_t*)dev->priv;
    if (!d || !d->present) return -1;
    u8* out = (u8*)buf;
    for (u32 i = 0; i < count; i++) {
        u64 l = lba + (u64)i;
        u64 p = l >> 2;                              /* physical sector */
        u32 off = (u32)(l & 3) * 512;
        if (p != (u64)g_cd_cached) {
            if (atapi_read_phys_(d, p, g_cd_buf) != 0) {
                g_cd_cached = -1;
                return -1;
            }
            g_cd_cached = (i64)p;
        }
        memcpy(out + (u64)i * 512, g_cd_buf + off, 512);
    }
    return 0;
}

static int atapi_blk_write(driver_block_device_t* dev, u64 lba, u32 count, const void* buf) {
    (void)dev; (void)lba; (void)count; (void)buf;
    return -1;                                       /* optical media */
}

static const driver_block_ops_t g_atapi_ops = {
    .read  = atapi_blk_read,
    .write = atapi_blk_write,
    .flush = NULL,
};

/* ---- probe ---- */

/* Read the medium capacity (READ CAPACITY(10)); returns 0 on success. */
static int atapi_capacity_(int drive, u64* phys_out) {
    u8 cdb[12];
    u8 resp[8];
    memset(cdb, 0, sizeof(cdb));
    cdb[0] = 0x25;                                   /* READ CAPACITY(10) */
    memset(resp, 0, sizeof(resp));
    if (atapi_packet_(drive, cdb, resp, sizeof(resp)) != 0) return -1;
    u64 last = ((u64)resp[0] << 24) | ((u64)resp[1] << 16) |
               ((u64)resp[2] << 8) | (u64)resp[3];
    u32 bsz = ((u32)resp[4] << 24) | ((u32)resp[5] << 16) |
              ((u32)resp[6] << 8) | (u32)resp[7];
    if (bsz != 2048 || last == 0) return -1;         /* no/odd medium */
    *phys_out = last + 1;
    return 0;
}

int ocse_atapi_init(void) {
    g_atapi_n = 0;
    g_cd_cached = -1;
    for (int drive = 0; drive < 4 && g_atapi_n < 4; drive++) {
        u16 base = g_chan_base[drive];
        outb_(base + ATAPI_REG_DRIVE, drive_select_(drive));
        io_wait_(); io_wait_();
        /* Live-caught in bring-up: the QEMU ATAPI drive answers with
         * status 0x00 right after select (a hard disk shows DRDY=0x50;
         * an absent device floats 0xFF). Existence must be judged by
         * the PACKET signature, not by the status byte - the very
         * reason the v1.0.0 ATA probe misses optical drives. */
        if (inb_(base + ATAPI_REG_LBA_MID) != 0x14) continue;
        if (inb_(base + ATAPI_REG_LBA_HI) != 0xEB) continue;
        if (wait_bsy_(base) < 0) continue;
        /* Confirm with IDENTIFY PACKET DEVICE and drain its data. */
        outb_(base + ATAPI_REG_COMMAND, ATAPI_CMD_IDENTIFY_PACKET);
        if (wait_drq_(base) < 0) continue;
        {
            u16 id[256];
            for (int i = 0; i < 256; i++) id[i] = inw_(base + ATAPI_REG_DATA);
            /* word 0 bits 2..5 = ATAPI packet command set (5 = CD/DVD). */
            u16 proto = (id[0] >> 8) & 0x1F;
            if (proto != 0x05) continue;
        }
        /* Medium present? */
        u64 phys = 0;
        if (atapi_capacity_(drive, &phys) != 0) continue;

        atapi_dev_t* d = &g_atapi[g_atapi_n];
        d->present = 1;
        d->ata_drive = drive;
        d->phys_sectors = phys;

        char name[8] = "cd?";
        name[2] = (char)('0' + g_atapi_n);
        driver_block_device_t dev;
        memset(&dev, 0, sizeof(dev));
        strcpy(dev.name, name);
        dev.type = BLK_TYPE_ATA;        /* closest bus class; cd* name is the tag */
        dev.sectors = phys * 4;         /* pseudo 512-byte sectors */
        dev.sector_size = 512;
        dev.priv = d;                   /* persistent per-drive state */
        driver_block_register(&dev, &g_atapi_ops);
        g_atapi_n++;
    }
    return g_atapi_n;
}

int ocse_atapi_is_cd_name(const char* name) {
    if (!name || name[0] != 'c' || name[1] != 'd') return 0;
    if (name[2] < '0' || name[2] > '9') return 0;
    return name[3] == 0;
}
