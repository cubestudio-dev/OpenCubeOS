/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10a
 * File: drivers/block/driver_block_ata_dma.c
 * Purpose: PCI Bus-Master IDE (BMDMA) driver - real DMA, polled.
 *
 * The PCI IDE controller spec defines, per channel:
 *   - a Bus-Master IDE Command register  (BMIC): bit 0 = START,
 *     bit 3 = R/W (1 = read from device into memory)
 *   - a Bus-Master IDE Status register   (BMIS): bit 0 = DMA interrupt,
 *     bit 1 = DMA error, bit 2 = simple interrupt (write 1 to clear)
 *   - a Bus-Master IDE Descriptor Table Pointer (BMIDTP): physical
 *     address of the PRDT, 4-byte aligned, bit 1:0 = 0
 * The controller registers live in I/O space at BAR4 (+0 for primary,
 * +8 for secondary).
 *
 * PRDT: physical-region descriptor table - up to 8 entries of 8 bytes:
 *   u32 address (physical, 64K-page aligned), u16 reserved (we use the
 *   high 16 bits of the 32-bit field for nothing), u16 byte count with
 *   bit 15 = EOT (end of table).
 * NOTE: the PRDT is the PCI IDE *legacy* format: one u32 address,
 * one u16 count with EOT in bit 15.  This differs from the AHCI PRDT.
 *
 * Command flow per transfer:
 *   1. stop the channel (BMIC.START=0), clear BMIS (write 1s)
 *   2. build the PRDT (bounce buffer, one entry, EOT set)
 *   3. program BMIDTP
 *   4. program the task file (LBA28, sector count) via the ATA regs
 *   5. issue READ DMA (0xC8) / WRITE DMA (0xCA)
 *   6. set BMIC = START | (rw ? RW : 0) and poll BMIS bit 0/1
 *   7. stop the channel, clear BMIS
 *
 * LBA28 is used (READ/WRITE DMA without EXT) because QEMU's PIIX3 BMDMA
 * predates LBA48 and real PIIX-class controllers are LBA28; the blk layer
 * caps all ATA devices at 2^28 sectors anyway.  FLUSH CACHE (0xE7) is
 * used after writes through the driver_block_flush hook.
 *
 * Fallback: if no BMDMA BAR exists, or the PRDT budget is exceeded, or a
 * DMA transfer times out, the caller (blk ops) returns an error and the
 * drive is registered/kept on the PIO path (ata.c).  driver_block_ata_register_blk()
 * skips drives already registered here, so PIO keeps serving the rest.
 */
#include "driver_block_ata_dma.h"
#include "driver_block_ata.h"
#include "types.h"
#include "mem_heap.h"
#include "lib_string.h"
#include "screen_console.h"
#include "driver_pci.h"
#include "block_svc.h"
#include "blk_drv_internal.h"
#include "core_registry.h"

/* ---- I/O port accessors ---- */
static inline void outb(u16 p, u8  v) { __asm__ volatile("outb %0, %1" :: "a"(v),  "Nd"(p)); }
static inline void outl(u16 p, u32 v) { __asm__ volatile("outl %0, %1" :: "a"(v),  "Nd"(p)); }
static inline u8   inb (u16 p)        { u8  v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline u32  inl (u16 p)        { u32 v; __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline void io_wait(void)      { outb(0x80, 0); }

/* ---- ATA task-file regs (same layout as ata.c) ---- */
#define ATA_REG_FEATURES 1
#define ATA_REG_ERROR    1
#define ATA_REG_COUNT    2
#define ATA_REG_LBA_LO   3
#define ATA_REG_LBA_MID  4
#define ATA_REG_LBA_HI   5
#define ATA_REG_DRIVE    6
#define ATA_REG_STATUS   7
#define ATA_REG_COMMAND  7
#define ATA_SR_BSY  0x80
#define ATA_SR_DRQ  0x08
#define ATA_SR_ERR  0x01

/* Commands. */
#define ATA_CMD_READ_DMA   0xC8u
#define ATA_CMD_WRITE_DMA  0xCAu
#define ATA_CMD_FLUSH      0xE7u
#define ATA_CMD_FLUSH_EXT  0xEAu

/* ---- BMDMA registers (from BAR4) ---- */
#define BMIC_OFF(chan)   ((chan) * 8u)      /* command   */
#define BMIS_OFF(chan)   ((chan) * 8u + 2u) /* status    */
#define BMIDTP_OFF(chan) ((chan) * 8u + 4u) /* PRDT ptr  */

#define BMIC_START  (1u << 0)
#define BMIC_RW_READ (1u << 3)  /* 1 = data flows device -> memory (READ DMA);
                                 * 0 = memory -> device (WRITE DMA). PCI IDE
                                 * spec names this bit "R/W" with 1 = Read. */

#define BMIS_DMA_INT   (1u << 0)
#define BMIS_DMA_ERROR (1u << 1)
#define BMIS_IDE_INT   (1u << 2)

/* ---- channel mapping: drive 0..3 -> (taskfile base, bmdma chan) ---- */
static const u16 g_chan_base[4] = { 0x1F0, 0x1F0, 0x170, 0x170 };
static const int g_drive_chan[4] = { 0, 0, 1, 1 };  /* BMDMA channel */
static const u8  g_drive_slave[4] = { 0, 1, 0, 1 };

/* PRDT: 8 entries x 8 bytes, 4-byte aligned.
 * BUG-0315 (A6-7): entry layout per the PCI IDE (Bus Master IDE) spec -
 * byte 0-3 address, byte 4-5 the EXACT byte count (0 encodes 64 KiB),
 * byte 6 reserved, byte 7 bit 7 = EOT. The old layout packed EOT into
 * the count field's bit 15 and stored bytes-1 (the AHCI DBC convention),
 * which real PIIX/ICH controllers read as a never-ending table whose
 * unused entries are heap garbage. */
#define PRDT_MAX 8
#define ATA_PRD_EOT 0x8000u
typedef struct __attribute__((packed)) {
    u32 addr;
    u16 byte_count;   /* exact byte count; 0 = 64 KiB */
    u16 eot_rsvd;     /* bit 15 (byte 7 bit 7) = EOT, rest reserved 0 */
} driver_block_ata_dma_prdt_t;

/* Command can move at most 64 KiB (PRDT 16-bit count) - we use one entry
 * of 8 sectors (4 KiB) from a shared bounce buffer to stay safe. */
#define DMA_BUF_SECTORS 8u

typedef struct {
    u32  bar4;          /* I/O base of the BMDMA registers (both chans) */
    u8   bus, dev, func;
    int  drive;         /* which drive (0..3) this controller serves via
                         * DMA, or -1 (a controller may serve one DMA
                         * drive per channel; we register the first) */
} driver_block_ata_dma_ctrl_t;

typedef struct {
    driver_block_ata_dma_ctrl_t *ctrl;
    driver_block_ata_dma_prdt_t *prdt;      /* 4-byte aligned */
    u8           *bounce;    /* 4 KiB */
    u64           sectors;
    int           active;    /* 1 = registered with blk via DMA ops */
    int           driver_block_idx;
} driver_block_ata_dma_drive_t;

#define BMDMA_MAX_CTRLS 2
static driver_block_ata_dma_ctrl_t g_ctrls[BMDMA_MAX_CTRLS];
static int          g_ctrl_count = 0;
static driver_block_ata_dma_drive_t g_drives[4];
static int          g_dma_drive_count = 0;

/* Shared staging pointers for the split loop (single-threaded kernel;
 * avoids passing src/dst through several call layers). */
static const u8 *g_xfer_src;
static u8       *g_xfer_dst;

/* ---- logging ---- */
static void dlog(const char *s) { screen_console_puts(s); }

static void dlog_hex(const char *prefix, u64 v, const char *suffix) {
    char buf[80]; char n[20];
    strcpy(buf, prefix);
    u64_to_hex(v, n, 0);
    strcpy(buf + strlen(buf), n);
    strcpy(buf + strlen(buf), suffix);
    dlog(buf);
}

static void dlog_dec(const char *prefix, u64 v, const char *suffix) {
    char buf[88]; char n[24];
    strcpy(buf, prefix);
    u64_to_str(v, n);
    strcpy(buf + strlen(buf), n);
    strcpy(buf + strlen(buf), suffix);
    dlog(buf);
}

static void dlog_bdf(const char *prefix, u8 bus, u8 dev, u8 func,
                     const char *suffix) {
    char buf[64]; char n[20];
    strcpy(buf, prefix);
    u64_to_hex((u64)bus, n, 2); strcpy(buf + strlen(buf), n);
    strcpy(buf + strlen(buf), ":");
    u64_to_hex((u64)dev, n, 2); strcpy(buf + strlen(buf), n);
    strcpy(buf + strlen(buf), ".");
    u64_to_hex((u64)func, n, 1); strcpy(buf + strlen(buf), n);
    strcpy(buf + strlen(buf), suffix);
    dlog(buf);
}

/* ---- low-level helpers ---- */

static int driver_block_ata_wait_not_bsy(u16 base) {
    for (int i = 0; i < 200000; i++) {
        u8 s = inb(base + ATA_REG_STATUS);
        if (!(s & ATA_SR_BSY)) return 0;
        io_wait();
    }
    return -1;
}

/* Program the task file for a DMA command (LBA28). */
static int driver_block_ata_dma_setup_taskfile(int drive, u64 lba, u8 sectors, u8 cmd) {
    u16 base = g_chan_base[drive];
    if (driver_block_ata_wait_not_bsy(base) != 0) return -1;
    outb(base + ATA_REG_DRIVE, (u8)(0xE0u | (u32)g_drive_slave[drive] << 4
                                    | ((lba >> 24) & 0x0Fu)));
    io_wait();
    outb(base + ATA_REG_FEATURES, 0x00);
    outb(base + ATA_REG_COUNT, sectors);
    outb(base + ATA_REG_LBA_LO, (u8)(lba & 0xFF));
    outb(base + ATA_REG_LBA_MID, (u8)((lba >> 8) & 0xFF));
    outb(base + ATA_REG_LBA_HI, (u8)((lba >> 16) & 0xFF));
    outb(base + ATA_REG_COMMAND, cmd);
    io_wait();
    return 0;
}

/* Stop DMA, clear status. */
static void driver_block_ata_dma_stop_clear(driver_block_ata_dma_drive_t *d, int chan) {
    u16 bmic = (u16)(d->ctrl->bar4 + BMIC_OFF(chan));
    u16 bmis = (u16)(d->ctrl->bar4 + BMIS_OFF(chan));
    outb(bmic, 0);                       /* START=0 */
    outb(bmis, BMIS_DMA_INT | BMIS_DMA_ERROR | BMIS_IDE_INT); /* w1c */
}

/* Run one DMA transfer of <=8 sectors through the bounce buffer.
 * fix3: on QEMU the very first READ DMA of a boot is silently dropped
 * roughly once in a few boots (BMIS stays 0 and the device returns to
 * idle DRDY, i.e. the command never runs).  The whole command sequence
 * is therefore retried a few times before the failure is reported -
 * standard block-layer practice, and it turns the intermittent
 * "read boot sector failed" mount loss into a deterministic boot. */
static int driver_block_ata_dma_xfer(driver_block_ata_dma_drive_t *d, int is_write, u64 lba, u32 sectors) {
    int drive = (int)(d - g_drives);
    int chan  = g_drive_chan[drive];
    u16 bmic  = (u16)(d->ctrl->bar4 + BMIC_OFF(chan));
    u16 bmis  = (u16)(d->ctrl->bar4 + BMIS_OFF(chan));

    u32 bytes = sectors * 512u;
    if (bytes == 0 || bytes > 0x10000u) return -1;
    if (is_write) memcpy(d->bounce, g_xfer_src, bytes);

    u8 cmd = is_write ? ATA_CMD_WRITE_DMA : ATA_CMD_READ_DMA;
    int rc = -1;
    u8 s_last = 0;

    for (int attempt = 0; attempt < 8; attempt++) {
        /* 1-2. stop + clear status. */
        driver_block_ata_dma_stop_clear(d, chan);

        /* 3. PRDT: one entry covering the whole bounce region, EOT set.
         * BUG-0315 (A6-7): spec encoding - exact byte count (0x10000 encodes
         * as 0) and EOT in byte 7 bit 7; unused entries are ZEROED so a
         * controller that walks past entry 0 never DMAs from heap garbage. */
        u64 phys = (u64)(uintptr_t)d->bounce;
        d->prdt[0].addr       = (u32)(phys & 0xFFFFFFF0u);
        d->prdt[0].byte_count = (u16)(bytes & 0xFFFFu);
        d->prdt[0].eot_rsvd   = (u16)(ATA_PRD_EOT);
        for (int i = 1; i < PRDT_MAX; i++) {
            d->prdt[i].addr = 0;
            d->prdt[i].byte_count = 0;
            d->prdt[i].eot_rsvd = 0;
        }

        /* 4. BMIDTP (physical PRDT address, bits 1:0 = 0). */
        outl((u16)(d->ctrl->bar4 + BMIDTP_OFF(chan)),
             (u32)((u64)(uintptr_t)d->prdt & 0xFFFFFFFCu));

        /* 5. task file + command (wait_not_bsy is bounded inside). */
        if (driver_block_ata_dma_setup_taskfile(drive, lba, (u8)sectors, cmd) != 0) {
            rc = -4;                     /* controller busy -> retry */
            for (volatile int stall = 0; stall < 20000; stall++) io_wait();
            continue;
        }

        /* 6. start DMA: RW bit = 1 for READ DMA (device -> memory). */
        outb(bmic, (u8)(BMIC_START | (is_write ? 0 : BMIC_RW_READ)));

        /* Poll BMIS: DMA interrupt (done) or DMA error.  While polling,
         * watch for the QEMU command-drop signature (device idle with no
         * BSY/DRQ and BMIS still 0): bail out early instead of burning
         * the full 50M polling budget, then run the sequence again. */
        rc = -1;
        s_last = 0;
        for (u64 i = 0; i < 50000000u; i++) {
            s_last = inb(bmis);
            if (s_last & BMIS_DMA_ERROR) { rc = -2; break; }
            if (s_last & BMIS_DMA_INT)   { rc = 0;  break; }
            if ((i & 0xFFFu) == 0xFFFu && s_last == 0) {
                u8 stq = inb(g_chan_base[drive] + ATA_REG_STATUS);
                if (!(stq & ATA_SR_BSY) && !(stq & ATA_SR_DRQ)) {
                    /* re-read once: a completing DMA may race the idle check */
                    s_last = inb(bmis);
                    if (s_last & BMIS_DMA_INT) { rc = 0; break; }
                    if (s_last & BMIS_DMA_ERROR) { rc = -2; break; }
                    rc = -5;            /* dropped: retry the sequence */
                    break;
                }
            }
        }

        /* 7. stop + clear. */
        driver_block_ata_dma_stop_clear(d, chan);

        if (rc == 0) break;
        /* command dropped / error: stall briefly, then run the whole
         * sequence again */
        for (volatile int stall = 0; stall < 20000; stall++) io_wait();
    }

    if (rc != 0) {
        /* fix3 diag: keep one line per exhausted transfer with the
         * controller state (rc negative-encoded; bmis/st/err raw). */
        u16 base = g_chan_base[drive];
        u8 st = inb(base + ATA_REG_STATUS);
        u8 err = inb(base + ATA_REG_ERROR);
        dlog_hex("atadma: xfer FAIL rc=", (u64)(rc < 0 ? -rc : rc), " ");
        dlog_hex("bmis=", s_last, " ");
        dlog_hex("st=", st, " ");
        dlog_hex("err=", err, "\n");
    }

    if (rc == 0 && !is_write) memcpy(g_xfer_dst, d->bounce, bytes);
    return rc;
}

/* Shared staging pointers for the split loop (single-threaded kernel;
 * avoids passing src/dst through several call layers). */
static const u8 *g_xfer_src;
static u8       *g_xfer_dst;

/* Public DMA read/write: split into <=8-sector transfers. */
int driver_block_ata_dma_read_sectors(int drive, u64 lba, int count, void *buf) {
    if (drive < 0 || drive > 3) return -1;
    driver_block_ata_dma_drive_t *d = &g_drives[drive];
    if (!d->active) return -1;
    if (count <= 0) return 0;
    if (lba + (u64)count > d->sectors) return -2;
    u8 *out = (u8 *)buf;
    u64 cur = lba;
    int remaining = count;
    while (remaining > 0) {
        u32 n = (remaining > (int)DMA_BUF_SECTORS) ? DMA_BUF_SECTORS : (u32)remaining;
        g_xfer_dst = out;
        if (driver_block_ata_dma_xfer(d, 0, cur, n) != 0) return (count - remaining > 0)
                                                   ? (count - remaining) : -3;
        out += n * 512u;
        cur += n;
        remaining -= (int)n;
    }
    return count;
}

int driver_block_ata_dma_write_sectors(int drive, u64 lba, int count, const void *buf) {
    if (drive < 0 || drive > 3) return -1;
    driver_block_ata_dma_drive_t *d = &g_drives[drive];
    if (!d->active) return -1;
    if (count <= 0) return 0;
    if (lba + (u64)count > d->sectors) return -2;
    const u8 *in = (const u8 *)buf;
    u64 cur = lba;
    int remaining = count;
    while (remaining > 0) {
        u32 n = (remaining > (int)DMA_BUF_SECTORS) ? DMA_BUF_SECTORS : (u32)remaining;
        g_xfer_src = in;
        if (driver_block_ata_dma_xfer(d, 1, cur, n) != 0) return (count - remaining > 0)
                                                   ? (count - remaining) : -3;
        in += n * 512u;
        cur += n;
        remaining -= (int)n;
    }
    return count;
}

int driver_block_ata_dma_flush(int drive) {
    if (drive < 0 || drive > 3) return -1;
    driver_block_ata_dma_drive_t *d = &g_drives[drive];
    if (!d->active) return -1;
    /* Prefer the 48-bit FLUSH EXT; fall back to legacy FLUSH. */
    u16 base = g_chan_base[drive];
    if (driver_block_ata_wait_not_bsy(base) != 0) return -1;
    outb(base + ATA_REG_DRIVE, (u8)(0xE0u | (u32)g_drive_slave[drive] << 4));
    io_wait();
    outb(base + ATA_REG_COMMAND, ATA_CMD_FLUSH_EXT);
    io_wait();
    if (driver_block_ata_wait_not_bsy(base) != 0) return -1;
    u8 st = inb(base + ATA_REG_STATUS);
    if (st & (ATA_SR_ERR | ATA_SR_BSY)) {
        /* Legacy-only drive: retry with FLUSH CACHE. */
        outb(base + ATA_REG_COMMAND, ATA_CMD_FLUSH);
        io_wait();
        if (driver_block_ata_wait_not_bsy(base) != 0) return -1;
        st = inb(base + ATA_REG_STATUS);
        if (st & (ATA_SR_ERR | ATA_SR_BSY)) return -1;
    }
    return 0;
}

int driver_block_ata_dma_available(int drive) {
    if (drive < 0 || drive > 3) return 0;
    return g_drives[drive].active;
}

/* ---- blk integration ----
 * priv encodes drive+1 (so 0 stays "unset"); the ops map it back. */

static int driver_block_ata_dma_blk_read_ops(driver_block_device_t *dev, u64 lba, u32 count, void *buf) {
    int drive = (int)(uintptr_t)dev->priv - 1;
    /* blk ops contract: 0 on success; driver_block_ata_dma returns count. */
    int rc = driver_block_ata_dma_read_sectors(drive, lba, (int)count, buf);
    return (rc == (int)count) ? 0 : -1;
}

static int driver_block_ata_dma_blk_write_ops(driver_block_device_t *dev, u64 lba, u32 count, const void *buf) {
    int drive = (int)(uintptr_t)dev->priv - 1;
    int rc = driver_block_ata_dma_write_sectors(drive, lba, (int)count, buf);
    return (rc == (int)count) ? 0 : -1;
}

static int driver_block_ata_dma_blk_flush_ops(driver_block_device_t *dev) {
    int drive = (int)(uintptr_t)dev->priv - 1;
    return driver_block_ata_dma_flush(drive);
}

static const driver_block_ops_t driver_block_ata_dma_blk_ops = {
    .read  = driver_block_ata_dma_blk_read_ops,
    .write = driver_block_ata_dma_blk_write_ops,
    .flush = driver_block_ata_dma_blk_flush_ops,
};

/* ---- init ---- */

/* WP-10a: bring up one IDE controller's BMDMA registers and its drives.
 * Returns 0 on success, -1 when the controller has no BMDMA BAR. */
static int driver_block_ata_dma_init_ctrl(u8 bus, u8 dev, u8 func) {
    /* BUG-0115 FIX: g_ctrls[BMDMA_MAX_CTRLS] was indexed with
     * g_ctrl_count++ and no bound check. A second init pass (dev
     * re-enumeration or an L1 extension calling the init hook again)
     * pushed g_ctrl_count past 2 and wrote out of the controller table.
     * Same guard the AHCI driver has had since its BUG-free rewrite
     * (ahci.c:567). */
    if (g_ctrl_count >= (int)(sizeof(g_ctrls) / sizeof(g_ctrls[0]))) {
        dlog_bdf("ata_dma: controller table full, ignoring IDE ctrl at ",
                 bus, dev, func, "\n");
        return -1;
    }
    driver_pci_enable_device(bus, dev, func);
    u32 bar4 = driver_pci_read_bar(bus, dev, func, 4);
    if (bar4 == 0) {
        dlog_bdf("ata_dma: IDE controller at ", bus, dev, func,
                 " has no BMDMA BAR (BAR4=0) - PIO only\n");
        return -1;
    }
    driver_block_ata_dma_ctrl_t *c = &g_ctrls[g_ctrl_count++];
    c->bar4 = bar4;
    c->bus = bus; c->dev = dev; c->func = func;
    c->drive = -1;
    dlog_bdf("ata_dma: IDE controller at ", bus, dev, func, " ");
    dlog_hex("BAR4=0x", bar4, "\n");

    /* One DMA drive per channel: primary master/slave (drives 0/1),
     * secondary master/slave (drives 2/3).  Register the first drive
     * of each channel that is actually present. */
    int chan_drives[2] = { -1, -1 };
    for (int drive = 0; drive < 4; drive++) {
        extern int driver_block_ata_detect(int drive);
        if (!driver_block_ata_detect(drive)) continue;
        int chan = g_drive_chan[drive];
        if (chan_drives[chan] < 0) chan_drives[chan] = drive;
    }
    for (int chan = 0; chan < 2; chan++) {
        int drive = chan_drives[chan];
        if (drive < 0) continue;
        driver_block_ata_dma_drive_t *d = &g_drives[drive];
        d->ctrl = c;
        d->prdt = (driver_block_ata_dma_prdt_t *)kmalloc(64);
        d->bounce = (u8 *)kmalloc(DMA_BUF_SECTORS * 512u);
        if (!d->prdt || !d->bounce) {
            d->ctrl = NULL;
            continue;
        }
        /* Read capacity via IDENTIFY (PIO path from ata.c). */
        u32 sectors = 0;
        if (driver_block_ata_identify_capacity(drive, &sectors) != 0 || sectors == 0) {
            d->ctrl = NULL;
            continue;
        }
        d->sectors = sectors;
        d->active = 1;
        c->drive = drive;

        /* Register (or upgrade) the blk device. */
        char name[8] = "hd?";
        name[2] = (char)('a' + drive);
        int idx = driver_block_find_device(name);
        if (idx >= 0) {
            blk_dev_set_ops(idx, &driver_block_ata_dma_blk_ops);
            driver_block_device_t *bd = driver_block_get_device(idx);
            if (bd) { bd->priv = (void *)(uintptr_t)(drive + 1); }
            d->driver_block_idx = idx;
        } else {
            d->driver_block_idx = blk_dev_add(name, BLK_TYPE_ATA,
                                             d->sectors, 512,
                                             &driver_block_ata_dma_blk_ops,
                                             (void *)(uintptr_t)(drive + 1));
        }
        if (d->driver_block_idx >= 0) {
            dlog("ata_dma: drive ");
            dlog(name);
            dlog(" registered (DMA, ");
            dlog_dec("", sectors, " sectors)\n");
            g_dma_drive_count++;
        }
    }
    return 0;
}

/* WP-10a L1 extension interface.  pdev == NULL enumerates every IDE
 * controller (class 0x0101, any prog-if); pdev != NULL brings up exactly
 * that PCI function after verifying its class. */
int driver_block_ata_dma_init(driver_pci_dev_t *pdev) {
    /* BUG-0115 FIX (re-entry half-reset): the first thing this function
     * does below is memset(g_drives, 0, ...) - on a SECOND call (device
     * re-enumeration, an L1 extension invoking the init hook again after
     * boot) that flips every live DMA drive to !active while the
     * controllers, the g_ctrls table and the already-registered blk
     * devices (with DMA ops) stay behind: a half-reset in which all
     * hda..hdd I/O fails while the drives look registered.  The
     * controller-table bound check fixed the OOB half of BUG-0115; this
     * refuses the re-entry itself, keeping the working state intact. */
    if (g_ctrl_count > 0) {
        dlog("ata_dma: init re-entered with live controllers - refusing, "
             "existing DMA drives left untouched\n");
        return -1;
    }
    memset(g_drives, 0, sizeof(g_drives));
    for (int i = 0; i < 4; i++) g_drives[i].driver_block_idx = -1;

    if (pdev) {
        u32 cls = (driver_pci_read_config(pdev->bus, pdev->dev, pdev->func, 0x08) >> 8) & 0xFFFFFFu;
        if ((cls & 0xFFFF00u) != 0x010100u) {
            dlog("ata_dma: requested PCI function is not class 0101xx\n");
            return -1;
        }
        return driver_block_ata_dma_init_ctrl(pdev->bus, pdev->dev, pdev->func) == 0 ? 1 : 0;
    }

    /* Find IDE controllers (class 0x0101, ANY prog-if: QEMU's PIIX3
     * reports 0x80, real PIIX-class chips 0x00/0x8A/...  The BMDMA
     * registers are at BAR4 in all modes). */
    for (int nth = 0; nth < BMDMA_MAX_CTRLS; nth++) {
        u8 bus = 0, dev = 0, func = 0;
        if (driver_pci_find_class_mask(0x010100u, 0xFFFF00u, nth, &bus, &dev, &func) != 0) break;
        driver_block_ata_dma_init_ctrl(bus, dev, func);
        if (g_ctrl_count >= BMDMA_MAX_CTRLS) break;
    }
    if (g_ctrl_count == 0) {
        dlog("ata_dma: no PCI IDE controller with BMDMA found - drives stay on PIO\n");
    }
    return g_ctrl_count;
}

void driver_block_ata_dma_print_state(void) {
    dlog_dec("ata_dma: controllers=", (u64)g_ctrl_count, "");
    dlog_dec(" dma_drives=", (u64)g_dma_drive_count, "\n");
    for (int i = 0; i < g_ctrl_count; i++) {
        driver_block_ata_dma_ctrl_t *c = &g_ctrls[i];
        dlog_bdf("  ctrl at PCI ", c->bus, c->dev, c->func, " ");
        dlog_hex("BAR4=0x", c->bar4, " dma_drive=");
        dlog(c->drive >= 0 ? "" : "none");
        if (c->drive >= 0) dlog_dec("", (u64)c->drive, "\n");
        else dlog("\n");
    }
    for (int drive = 0; drive < 4; drive++) {
        driver_block_ata_dma_drive_t *d = &g_drives[drive];
        char name[8] = "hd?";
        name[2] = (char)('a' + drive);
        char buf[80]; char n[24];
        strcpy(buf, "  ");
        strcpy(buf + strlen(buf), name);
        strcpy(buf + strlen(buf), ": ");
        strcpy(buf + strlen(buf), d->active ? "DMA" : "PIO/absent");
        if (d->active) {
            strcpy(buf + strlen(buf), " sectors=");
            u64_to_str(d->sectors, n);
            strcpy(buf + strlen(buf), n);
        }
        strcpy(buf + strlen(buf), "\n");
        dlog(buf);
    }
}

/* ---- v2.0.0 file-local driver identity (collected at boot) ---- */
static int blk_probe_ata_dma(void) { return driver_block_ata_dma_init(NULL); }
static const blk_drv_ops_t g_blk_probe_ata_dma = { blk_probe_ata_dma };
OC_DRIVER_DECL("block", "ata_dma", &g_blk_probe_ata_dma, "ATA Bus-Master DMA (PCI IDE)");
