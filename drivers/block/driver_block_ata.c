/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-05/WP-07
 * File: kernel/ata.c
 * Purpose: ATA/IDE PIO driver (LBA28, polling, no IRQ).
 *
 * We support up to four drives on two channels (primary 0x1F0, secondary
 * 0x170). Each channel has a master and a slave. Reads/writes use 16-bit
 * PIO DATA IN/OUT (rep insw / rep outsw emitted as a tight loop).
 *
 * Status register bits:
 *   0x80 ERR  - error
 *   0x40 DRQ  - data request (ready to transfer)
 *   0x20 SRV  - service
 *   0x10 DF   - drive fault
 *   0x08  - data index
 *   0x01  - error
 *
 * This driver does NOT use interrupts; it spins on the status register's
 * BSY/DRQ bits. That's fine for a hobby kernel.
 */
#include "driver_block_ata.h"
#include "screen_console.h"
#include "lib_string.h"

/* ---- Port I/O helpers ---- */

static inline void outb(u16 p, u8  v) { __asm__ volatile("outb %0, %1" :: "a"(v),  "Nd"(p)); }
static inline void outw(u16 p, u16 v) { __asm__ volatile("outw %0, %1" :: "a"(v),  "Nd"(p)); }
static inline u8   inb (u16 p)        { u8  v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline u16  inw (u16 p)        { u16 v; __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline void io_wait(void)      { outb(0x80, 0); }

/* ---- ATA register offsets from the channel base ---- */
#define ATA_REG_DATA     0
#define ATA_REG_ERROR    1
#define ATA_REG_FEATURES 1
#define ATA_REG_COUNT    2
#define ATA_REG_LBA_LO   3
#define ATA_REG_LBA_MID  4
#define ATA_REG_LBA_HI   5
#define ATA_REG_DRIVE    6
#define ATA_REG_STATUS   7
#define ATA_REG_COMMAND  7

/* Control register (offset from control base). */
#define ATA_REG_CONTROL  0

/* Status bits. */
#define ATA_SR_ERR   0x01
#define ATA_SR_DRQ   0x08
#define ATA_SR_DF    0x20
#define ATA_SR_BSY   0x80

/* Commands. */
#define ATA_CMD_READ_PIO    0x20
#define ATA_CMD_WRITE_PIO   0x30
#define ATA_CMD_IDENTIFY    0xEC

/* Channel bases. */
static const u16 g_chan_base[4]    = { 0x1F0, 0x1F0, 0x170, 0x170 };
static const u16 g_chan_ctrl[4]    = { 0x3F6, 0x3F6, 0x376, 0x376 };

/* Drive select byte (LBA mode | slave bit). */
static u8 driver_block_ata_drive_select(int drive) {
    u8 head = 0xE0;             /* LBA mode, master */
    if (drive & 1) head = 0xF0; /* slave */
    return head;
}

/* Wait for BSY to clear. Returns 0 on success, -1 on timeout. */
static int driver_block_ata_wait_bsy(u16 base) {
    for (int i = 0; i < 100000; i++) {
        u8 s = inb(base + ATA_REG_STATUS);
        if ((s & ATA_SR_BSY) == 0) return 0;
        io_wait();
    }
    return -1;
}

/* Wait for DRQ to set (and BSY clear). Returns 0 on success, -1 on timeout
 * or error. */
static int driver_block_ata_wait_drq(u16 base) {
    for (int i = 0; i < 100000; i++) {
        u8 s = inb(base + ATA_REG_STATUS);
        if (s & ATA_SR_ERR) return -1;
        if (s & ATA_SR_DF)  return -1;
        if ((s & ATA_SR_BSY) == 0 && (s & ATA_SR_DRQ)) return 0;
        io_wait();
    }
    return -1;
}

/* ---- Public API ---- */

void driver_block_ata_init(void) {
    /* Disable interrupts on both channels via the control register
     * (bit 1 = nIEN). */
    outb(g_chan_ctrl[0], 0x02);
    outb(g_chan_ctrl[2], 0x02);

    /* Probe and report. */
    for (int d = 0; d < 4; d++) {
        if (driver_block_ata_detect(d)) {
            char msg[60];
            const char *chan = (d < 2) ? "primary" : "secondary";
            const char *role = (d & 1) ? "slave" : "master";
            strcpy(msg, "ata: ");
            strcpy(msg + strlen(msg), chan);
            strcpy(msg + strlen(msg), " ");
            strcpy(msg + strlen(msg), role);
            strcpy(msg + strlen(msg), " present\n");
            screen_console_puts(msg);
        }
    }
}

int driver_block_ata_detect(int drive) {
    if (drive < 0 || drive > 3) return 0;
    u16 base = g_chan_base[drive];
    u16 ctrl = g_chan_ctrl[drive];

    /* Select the drive. */
    outb(base + ATA_REG_DRIVE, driver_block_ata_drive_select(drive));
    io_wait();
    /* Issue a soft reset, then wait. */
    outb(ctrl + ATA_REG_CONTROL, 0x04);  /* SRST */
    io_wait();
    outb(ctrl + ATA_REG_CONTROL, 0x02);  /* clear SRST, keep nIEN */
    io_wait();
    /* Wait for BSY clear. */
    if (driver_block_ata_wait_bsy(base) < 0) return 0;
    /* Read signature: LBA_MID and LBA_HI must be 0x00 0x00 for ATA. */
    u8 mid = inb(base + ATA_REG_LBA_MID);
    u8 hi  = inb(base + ATA_REG_LBA_HI);
    if (mid != 0x00 || hi != 0x00) return 0;
    /* Issue IDENTIFY to confirm. */
    outb(base + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    io_wait();
    if (driver_block_ata_wait_drq(base) < 0) return 0;
    /* Drain the 256-word IDENTIFY data so the drive is in a clean state. */
    for (int i = 0; i < 256; i++) {
        (void)inw(base + ATA_REG_DATA);
    }
    return 1;
}

int driver_block_ata_read_sectors(int drive, u64 lba, int count, void *buf) {
    if (drive < 0 || drive > 3) return -1;
    if (count <= 0) return 0;
    if (lba + (u64)count > 0x10000000ULL) return -2;  /* LBA28 limit */
    u16 base = g_chan_base[drive];
    u8 *out = (u8 *)buf;

    for (int s = 0; s < count; s++) {
        u32 cur = (u32)(lba + (u64)s);
        /* Select drive + LBA top 4 bits. */
        outb(base + ATA_REG_DRIVE, (u8)(driver_block_ata_drive_select(drive) | ((cur >> 24) & 0x0F)));
        io_wait();
        outb(base + ATA_REG_FEATURES, 0x00);
        outb(base + ATA_REG_COUNT, 1);
        outb(base + ATA_REG_LBA_LO, (u8)(cur & 0xFF));
        outb(base + ATA_REG_LBA_MID, (u8)((cur >> 8) & 0xFF));
        outb(base + ATA_REG_LBA_HI, (u8)((cur >> 16) & 0xFF));
        outb(base + ATA_REG_COMMAND, ATA_CMD_READ_PIO);
        io_wait();
        if (driver_block_ata_wait_drq(base) < 0) return (s > 0) ? s : -3;
        /* Read 256 16-bit words = 512 bytes. */
        u16 *p = (u16 *)(out + s * 512);
        for (int i = 0; i < 256; i++) {
            p[i] = inw(base + ATA_REG_DATA);
        }
        /* Tiny delay between sectors. */
        io_wait();
    }
    return count;
}

int driver_block_ata_write_sectors(int drive, u64 lba, int count, const void *buf) {
    if (drive < 0 || drive > 3) return -1;
    if (count <= 0) return 0;
    if (lba + (u64)count > 0x10000000ULL) return -2;
    u16 base = g_chan_base[drive];
    const u8 *in = (const u8 *)buf;

    for (int s = 0; s < count; s++) {
        u32 cur = (u32)(lba + (u64)s);
        outb(base + ATA_REG_DRIVE, (u8)(driver_block_ata_drive_select(drive) | ((cur >> 24) & 0x0F)));
        io_wait();
        outb(base + ATA_REG_FEATURES, 0x00);
        outb(base + ATA_REG_COUNT, 1);
        outb(base + ATA_REG_LBA_LO, (u8)(cur & 0xFF));
        outb(base + ATA_REG_LBA_MID, (u8)((cur >> 8) & 0xFF));
        outb(base + ATA_REG_LBA_HI, (u8)((cur >> 16) & 0xFF));
        outb(base + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);
        io_wait();
        if (driver_block_ata_wait_drq(base) < 0) return (s > 0) ? s : -3;
        /* Write 256 16-bit words. */
        const u16 *p = (const u16 *)(in + s * 512);
        for (int i = 0; i < 256; i++) {
            outw(base + ATA_REG_DATA, p[i]);
        }
        /* Flush via the cache-flush command (0xE7) — wait for BSY clear. */
        outb(base + ATA_REG_COMMAND, 0xE7);
        io_wait();
        if (driver_block_ata_wait_bsy(base) < 0) return (s + 1 < count) ? (s + 1) : -4;
    }
    return count;
}

/* ---- WP-07 blk layer integration ---- */
#include "driver_block_blk.h"

static int driver_block_ata_blk_read(driver_block_device_t *dev, u64 lba, u32 count, void *buf) {
    int drive = (int)(uintptr_t)dev->priv;
    int rc = driver_block_ata_read_sectors(drive, lba, (int)count, buf);
    return (rc == (int)count) ? 0 : -1;
}

static int driver_block_ata_blk_write(driver_block_device_t *dev, u64 lba, u32 count, const void *buf) {
    int drive = (int)(uintptr_t)dev->priv;
    int rc = driver_block_ata_write_sectors(drive, lba, (int)count, buf);
    return (rc == (int)count) ? 0 : -1;
}

static const driver_block_ops_t driver_block_ata_blk_ops = { .read = driver_block_ata_blk_read, .write = driver_block_ata_blk_write };

/* Register all detected ATA drives with the block layer. Called after driver_block_init.
 * Uses driver_block_ata_detect() to check presence and IDENTIFY to read capacity.
 * WP-10a: driver_block_ata_dma_init() runs BEFORE this function and may already have
 * registered a drive with DMA ops — in that case do not register a second
 * (PIO) entry for the same device name. */
void driver_block_ata_register_blk(void) {
    for (int drive = 0; drive < 4; drive++) {
        if (driver_block_ata_detect(drive)) {
            char name[8] = "hd?";
            name[2] = 'a' + (char)drive;
            /* WP-10a: skip drives already registered by the BMDMA driver. */
            if (driver_block_find_device(name) >= 0) continue;
            /* Read capacity from IDENTIFY data (word 60-61). */
            u16 base = g_chan_base[drive];
            driver_block_ata_drive_select(drive);
            io_wait();
            outb(base + 2, 0); outb(base + 3, 0); outb(base + 4, 0); outb(base + 5, 0);
            outb(base + 7, 0xEC);  /* IDENTIFY */
            io_wait();
            u8 st = inb(base + 7);
            if (st == 0) continue;
            if (driver_block_ata_wait_bsy(base) < 0) continue;
            if (!(inb(base + 7) & 0x08)) continue;  /* DRQ */
            u16 id[256];
            for (int i = 0; i < 256; i++) id[i] = inw(base);
            u32 sectors = (u32)id[60] | ((u32)id[61] << 16);
            driver_block_register_device(name, BLK_TYPE_ATA, sectors, 512, &driver_block_ata_blk_ops, (void*)(uintptr_t)drive);
        }
    }
}

/* ---- WP-10a: capacity query for the Bus-Master DMA driver ----
 * Same PIO IDENTIFY sequence as driver_block_ata_register_blk() above, wrapped as a
 * single-drive query.  Returns 0 on success, negative if the drive is
 * absent or the IDENTIFY data is unusable. */
int driver_block_ata_identify_capacity(int drive, u32 *sectors_out) {
    if (drive < 0 || drive > 3 || !sectors_out) return -1;
    if (!driver_block_ata_detect(drive)) return -1;
    u16 base = g_chan_base[drive];
    driver_block_ata_drive_select(drive);
    io_wait();
    outb(base + 2, 0); outb(base + 3, 0); outb(base + 4, 0); outb(base + 5, 0);
    outb(base + 7, 0xEC);  /* IDENTIFY DEVICE */
    io_wait();
    u8 st = inb(base + 7);
    if (st == 0) return -1;
    if (driver_block_ata_wait_bsy(base) < 0) return -1;
    if (!(inb(base + 7) & 0x08)) return -1;  /* DRQ */
    u16 id[256];
    for (int i = 0; i < 256; i++) id[i] = inw(base);
    u32 sectors = (u32)id[60] | ((u32)id[61] << 16);
    if (sectors == 0) return -1;
    *sectors_out = sectors;
    return 0;
}
