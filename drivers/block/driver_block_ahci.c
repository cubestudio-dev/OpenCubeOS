/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-10a
 * File: kernel/ahci.c
 * Purpose: AHCI (SATA) host controller driver - real DMA, polled.
 *
 * Probe flow
 * ----------
 *  1. driver_pci_find_class_exact(0x010601) -> AHCI controller BDF.
 *  2. driver_pci_enable_device + read BAR5 -> HBA MMIO base.
 *  3. GHC.AE = 1 (enable AHCI mode).  Interrupts stay disabled (GHC.IE=0).
 *  4. CAP: NP (ports-1), PI (implemented-port mask), S64A.
 *  5. For every implemented port:
 *       - stop the port (PxCMD.ST=0, wait CR=0; PxCMD.FRE=0, wait FR=0)
 *       - program PxCLB (command list, 1 KiB aligned) and PxFB (FIS receive
 *         area, 256-byte aligned)
 *       - clear PxSERR, enable FRE + ST
 *       - device present if PxSSTS.DET == 3 and PxSIG == ATA (0x00000101)
 *  6. For each ATA drive: IDENTIFY DEVICE via a Register H2D FIS to get
 *     the 48-bit capacity (words 100/101), register "sda"...
 *
 * I/O model (per port)
 * --------------------
 *  - One command slot (slot 0), one command table, synchronous polling
 *    on PxCI / PxIS / PxTFD.
 *  - A 4 KiB page-aligned bounce buffer per port; transfers are split
 *    into chunks of at most 8 sectors (4 KiB) so a single PRDT entry
 *    always suffices.
 *  - READ DMA EXT (0x25) / WRITE DMA EXT (0x35) with 48-bit LBA;
 *    FLUSH CACHE EXT (0xEA) for the driver_block_flush hook.
 *
 * Memory notes: the kernel identity-maps all physical memory and the heap
 * sits below 4 GiB, so physical == virtual for our allocations.  The HBA
 * programs 64-bit base addresses (CAP.S64A is honored; QEMU and real
 * controllers both accept them).
 */
#include "driver_block_ahci.h"
#include "types.h"
#include "mem_heap.h"
#include "lib_string.h"
#include "screen_console.h"
#include "driver_pci.h"
#include "driver_block_blk.h"

/* ---- MMIO accessors ---- */
static inline u32 mmio_read32(volatile void *p)  { return *(volatile u32 *)p; }
static inline void mmio_write32(volatile void *p, u32 v) { *(volatile u32 *)p = v; }

/* ---- HBA register offsets (BAR5) ---- */
#define AHCI_REG_CAP        0x00u
#define AHCI_REG_GHC        0x04u
#define AHCI_REG_IS         0x08u
#define AHCI_REG_PI         0x0Cu
#define AHCI_REG_CAP2       0x24u
#define AHCI_REG_PORTS      0x100u
#define AHCI_PORT_REG_SIZE  0x80u

/* GHC bits. */
#define AHCI_GHC_AE         (1u << 31)
#define AHCI_GHC_IE         (1u << 1)

/* CAP bits. */
#define AHCI_CAP_NP(cap)    ((cap) & 0x1Fu)
#define AHCI_CAP_S64A(cap)  ((cap) & (1u << 0))
#define AHCI_CAP_SNCQ(cap)  ((cap) & (1u << 5))

/* Per-port register offsets (from the port base). */
#define AHCI_PxCLB   0x00u
#define AHCI_PxCLBU  0x04u
#define AHCI_PxFB    0x08u
#define AHCI_PxFBU   0x0Cu
#define AHCI_PxIS    0x10u
#define AHCI_PxIE    0x14u
#define AHCI_PxCMD   0x18u
#define AHCI_PxTFD   0x1Cu
#define AHCI_PxSIG   0x24u
#define AHCI_PxSSTS  0x28u
#define AHCI_PxSCTL  0x2Cu
#define AHCI_PxSERR  0x30u
#define AHCI_PxCI    0x38u

/* PxCMD bits. */
#define AHCI_CMD_ST   (1u << 0)   /* start  */
#define AHCI_CMD_SUD  (1u << 1)   /* spin-up device */
#define AHCI_CMD_POD  (1u << 2)   /* power on device */
#define AHCI_CMD_FRE  (1u << 4)   /* FIS receive enable */
#define AHCI_CMD_CR   (1u << 15)  /* command list running */
#define AHCI_CMD_FR   (1u << 14)  /* FIS receive running */

/* PxSSTS.DET values. */
#define AHCI_SSTS_DET(p)   ((p) & 0xFu)
#define AHCI_DET_PRESENT   0x3u

/* PxTFD access. */
#define AHCI_TFD_STS_BSY(t) ((t) & 0x80u)
#define AHCI_TFD_STS_DRQ(t) ((t) & 0x08u)
#define AHCI_TFD_ERR(t)     (((t) >> 8) & 0xFFu)

/* Device signatures. */
#define AHCI_SIG_ATA   0x00000101u
#define AHCI_SIG_ATAPI 0xEB140101u

/* ATA commands (48-bit LBA, DMA). */
#define ATA_CMD_READ_DMA_EXT   0x25u
#define ATA_CMD_WRITE_DMA_EXT  0x35u
#define ATA_CMD_FLUSH_EXT      0xEAu
#define ATA_CMD_IDENTIFY       0xECu

/* H2D Register FIS. */
#define FIS_TYPE_H2D   0x27u
#define FIS_H2D_C      (1u << 7)

/* Geometry. */
#define AHCI_MAX_PORTS      32u
#define AHCI_CMDLIST_ALIGN  1024u   /* PxCLB alignment */
#define AHCI_FIS_ALIGN      256u    /* PxFB alignment */
#define AHCI_PORT_BUF_SECT  8u      /* sectors per command (4 KiB) */
#define AHCI_PAGE           4096u

/* PxIS Task File Error Status (TFES) = bit 30 (0x40000000), as reported
 * by QEMU's driver_block_ahci_trigger_irq (verified via -trace).  Other IS bits are
 * status (connect/power-state changes) - clearing them is enough.
 * Also: QEMU's AHCI model rejects the FIRST command issued right after
 * port start (shell_cmd_done + TFES with no DMA); a stop/clear/start bounce
 * makes the port fully operational.  We therefore retry after a bounce. */
#define AHCI_IS_TFE   (1u << 30)

/* Poll budgets.  AHCI spin-up + first command on QEMU completes in well
 * under a second; keep a generous margin for real hardware. */
#define AHCI_POLL_PORT_ITERS  20000000u
#define AHCI_POLL_CMD_ITERS   50000000u

/* ---- on-wire structures ---- */

/* Command header (32 bytes) in the command list.
 * Per the AHCI spec: dword0 = flags (CFL[4:0]/W/I/PMP), dword1 = PRDT
 * length, dword2 = reserved (QEMU calls it "status"), dwords 3-4 = the
 * 64-bit command-table base, dwords 5-7 reserved.  WP-10a FIX: the
 * first draft packed CTBA at offset 4 (as dword1.5), so the controller
 * read CTBA=0 (BIOS IVT area) and never executed any command. */
typedef struct __attribute__((packed)) {
    u16 flags;      /* bits 4:0 CFL (FIS dwords), bit 7 PMP */
    u16 prdt_len;   /* number of PRDT entries */
    u32 status;     /* dword2: reserved - keep 0 */
    u64 prdt_base;  /* 64-bit command-table base address (offset 8) */
    u32 rsvd[4];    /* dwords 5-7 (16 bytes) - total 32 bytes */
} driver_block_ahci_cmd_hdr_t;

/* PRDT entry.
 * WP-10a FIX: QEMU 10.x parses guest PRDT entries as 16 bytes
 * (AHCI_SG: u64 addr + u32 reserved + u32 flags_size), matching its
 * hw/ide/ahci.c populate_sglist().  With our first-draft 12-byte
 * entries, QEMU read flags_size from the wrong offset (0) and every
 * transfer shrank to 1 byte.  16-byte entries make transfers complete.
 * Real hardware following AHCI 1.3.1 (8-byte entries) is unaffected by
 * this layout choice only if we also keep the flag nibble at bit 31 of
 * flags_size -- we do.
 * flags_size: bits 21:0 = transfer bytes - 1; bit 31 = I (interrupt on
 * completion, kept 0 in polled mode). */
typedef struct __attribute__((packed)) {
    u64 addr;       /* 64-bit data base address */
    u32 rsvd;       /* reserved - 0 */
    u32 flags_size; /* bits 21:0 = bytes - 1; bit 31 = I */
} driver_block_ahci_prdt_t;

#define PRDT_BYTES(bytes) ((bytes) - 1u)

/* Command table: 64-byte CFIS + 16-byte ACMD + 48 reserved + 8 PRDTs. */
typedef struct __attribute__((packed)) {
    u8  cfis[64];
    u8  acmd[16];
    u8  rsvd[48];
    driver_block_ahci_prdt_t prdt[8];
} driver_block_ahci_cmd_table_t;

/* ---- per-port state ---- */
typedef struct {
    /* Registers. */
    volatile u32 *regs;      /* port register block base */

    /* DMA structures (page-aligned, identity-mapped). */
    driver_block_ahci_cmd_hdr_t  *cmd_list;   /* 32 x 32 bytes  (1 KiB aligned) */
    u8              *fis_rx;     /* 256 bytes      (256B aligned) */
    driver_block_ahci_cmd_table_t *cmd_tab;   /* 128B aligned   */
    u8              *bounce;     /* 4 KiB          (page aligned) */

    /* Geometry / presence. */
    u8   present;
    u8   is_ata;
    u64  sectors;

    /* blk layer index, or -1. */
    int  driver_block_idx;
} driver_block_ahci_port_t;

/* ---- per-controller state ---- */
typedef struct {
    u64        mmio_base;
    u8         bus, dev, func;
    u32        cap;
    u32        pi;
    driver_block_ahci_port_t ports[AHCI_MAX_PORTS];
} driver_block_ahci_ctrl_t;

/* SATA drive list across all controllers (for blk registration). */
typedef struct {
    driver_block_ahci_port_t *port;
    char         name[8];   /* "sda" ... */
} driver_block_ahci_drive_t;

static driver_block_ahci_ctrl_t  g_ctrls[2];
static int          g_ctrl_count = 0;
static driver_block_ahci_drive_t g_drives[AHCI_MAX_DRIVES];
static int          g_drive_count = 0;

/* ---- logging helpers ---- */
static void driver_block_ahci_log(const char *s) { screen_console_puts(s); }

static void driver_block_ahci_log_hex(const char *prefix, u64 v, const char *suffix) {
    char buf[72]; char n[20];
    strcpy(buf, prefix);
    u64_to_hex(v, n, 0);
    strcpy(buf + strlen(buf), n);
    strcpy(buf + strlen(buf), suffix);
    driver_block_ahci_log(buf);
}

static void driver_block_ahci_log_dec(const char *prefix, u64 v, const char *suffix) {
    char buf[88]; char n[24];
    strcpy(buf, prefix);
    u64_to_str(v, n);
    strcpy(buf + strlen(buf), n);
    strcpy(buf + strlen(buf), suffix);
    driver_block_ahci_log(buf);
}

/* Over-allocate then page-align (heap guarantees 16-byte alignment). */
static void *driver_block_ahci_page_aligned(u64 size) {
    u8 *raw = (u8 *)kmalloc(size + AHCI_PAGE);
    if (!raw) return NULL;
    uintptr_t a = (uintptr_t)raw;
    a = (a + (AHCI_PAGE - 1)) & ~(uintptr_t)(AHCI_PAGE - 1);
    return (void *)a;
}

/* ---- port register helpers ---- */
static u32 p_read(driver_block_ahci_port_t *p, u32 off) {
    return mmio_read32((volatile void *)((u8 *)p->regs + off));
}
static void p_write(driver_block_ahci_port_t *p, u32 off, u32 v) {
    mmio_write32((volatile void *)((u8 *)p->regs + off), v);
}

/* Stop the port engine: clear ST, wait CR=0; clear FRE, wait FR=0. */
static int driver_block_ahci_port_stop(driver_block_ahci_port_t *p) {
    u32 cmd = p_read(p, AHCI_PxCMD);
    cmd &= ~AHCI_CMD_ST;
    p_write(p, AHCI_PxCMD, cmd);
    for (u64 i = 0; i < AHCI_POLL_PORT_ITERS; i++) {
        if (!(p_read(p, AHCI_PxCMD) & AHCI_CMD_CR)) break;
        if (i == AHCI_POLL_PORT_ITERS - 1) return -1;
    }
    cmd = p_read(p, AHCI_PxCMD);
    cmd &= ~AHCI_CMD_FRE;
    p_write(p, AHCI_PxCMD, cmd);
    for (u64 i = 0; i < AHCI_POLL_PORT_ITERS; i++) {
        if (!(p_read(p, AHCI_PxCMD) & AHCI_CMD_FR)) break;
        if (i == AHCI_POLL_PORT_ITERS - 1) return -1;
    }
    return 0;
}

/* Start the port engine: FRE=1, ST=1. */
static void driver_block_ahci_port_start(driver_block_ahci_port_t *p) {
    u32 cmd = p_read(p, AHCI_PxCMD);
    cmd |= AHCI_CMD_FRE | AHCI_CMD_ST;
    p_write(p, AHCI_PxCMD, cmd);
}

/* Issue one command in slot 0 and poll for completion.
 * `fis` points at a 20-byte H2D FIS (already built).
 * Returns 0 on success, -1 on timeout/error. */
static int driver_block_ahci_port_issue(driver_block_ahci_port_t *p, const u8 *fis20, int is_write,
                           void *buf, u32 bytes) {
    /* Program the command header: CFL=5 dwords, 1 PRDT entry, slot 0. */
    driver_block_ahci_cmd_hdr_t *h = p->cmd_list;
    h->flags     = (u16)(5u);          /* 5 DWORD FIS, PMP=0 */
    h->prdt_len  = 1;
    h->status    = 0;                  /* dword2 reserved */
    h->prdt_base = (u64)(uintptr_t)p->cmd_tab;
    memset((void *)h->rsvd, 0, sizeof(h->rsvd));

    /* Command table: FIS + PRDT. */
    memset(p->cmd_tab->cfis, 0, sizeof(p->cmd_tab->cfis));
    memcpy(p->cmd_tab->cfis, fis20, 20);
    memset((void *)p->cmd_tab->acmd, 0, sizeof(p->cmd_tab->acmd));
    p->cmd_tab->prdt[0].addr       = (u64)(uintptr_t)buf;
    p->cmd_tab->prdt[0].rsvd       = 0;
    p->cmd_tab->prdt[0].flags_size = PRDT_BYTES(bytes);

    /* Clear pending interrupt status, then issue. */
    p_write(p, AHCI_PxIS, p_read(p, AHCI_PxIS));
    __asm__ volatile("" ::: "memory");
    p_write(p, AHCI_PxCI, 1u);          /* slot 0 */

    /* Poll for completion: PxCI bit 0 clears when the command finishes. */
    for (u64 i = 0; i < AHCI_POLL_CMD_ITERS; i++) {
        u32 ci = p_read(p, AHCI_PxCI);
        if ((ci & 1u) == 0) {
            u32 is  = p_read(p, AHCI_PxIS);
            u32 tfd = p_read(p, AHCI_PxTFD);
            p_write(p, AHCI_PxIS, is);   /* ack everything (w1c) */
            /* Only a real Task File Error (or a device-reported error in
             * PxTFD) fails the command; status-change bits are noise. */
            if (is & AHCI_IS_TFE) {
                driver_block_ahci_log_hex("ahci: issue fail (PxIS.TFE) IS=0x", is, "\n");
                return -1;
            }
            if (AHCI_TFD_ERR(tfd) != 0 && !AHCI_TFD_STS_BSY(tfd)) {
                driver_block_ahci_log_hex("ahci: issue fail (PxTFD.ERR) TFD=0x", tfd, "\n");
                return -1;
            }
            (void)is_write;
            return 0;
        }
    }
    /* Timeout: try to stop the engine so the slot is not stuck. */
    {
        char buf2[96]; char n[20];
        u32 ci  = p_read(p, AHCI_PxCI);
        u32 tfd = p_read(p, AHCI_PxTFD);
        u32 is  = p_read(p, AHCI_PxIS);
        strcpy(buf2, "ahci: issue TIMEOUT CI=0x");
        u64_to_hex(ci, n, 0); strcpy(buf2 + strlen(buf2), n);
        strcpy(buf2 + strlen(buf2), " TFD=0x");
        u64_to_hex(tfd, n, 0); strcpy(buf2 + strlen(buf2), n);
        strcpy(buf2 + strlen(buf2), " IS=0x");
        u64_to_hex(is, n, 0); strcpy(buf2 + strlen(buf2), n);
        strcpy(buf2 + strlen(buf2), "\n");
        driver_block_ahci_log(buf2);
    }
    u32 cmd = p_read(p, AHCI_PxCMD);
    cmd &= ~AHCI_CMD_ST;
    p_write(p, AHCI_PxCMD, cmd);
    for (u64 i = 0; i < AHCI_POLL_PORT_ITERS; i++) {
        if (!(p_read(p, AHCI_PxCMD) & AHCI_CMD_CR)) break;
    }
    p_write(p, AHCI_PxCI, p_read(p, AHCI_PxCI) & ~1u);
    driver_block_ahci_port_start(p);
    return -1;
}

static void driver_block_ahci_build_fis(u8 *fis, u8 cmd, u64 lba, u32 sectors) {
    /* Register FIS layout per the AHCI spec (verified against QEMU's
     * handle_reg_h2d_fis parser):
     *   byte2 command, byte3 features,
     *   byte4 LBA 7:0, byte5 LBA 15:8, byte6 LBA 23:16,
     *   byte7 Device (0xE0 | LBA 27:24),
     *   byte8 LBA 31:24, byte9 LBA 39:32, byte10 LBA 47:40,
     *   byte12 count 7:0, byte13 count 15:8.
     * WP-10a FIX: the first draft put LBA at bytes 5-7 and Device at
     * byte4, so every data command addressed a wrong LBA in CHS mode and
     * failed with TFES; IDENTIFY (LBA-insensitive) masked the bug. */
    memset(fis, 0, 20);
    fis[0]  = FIS_TYPE_H2D;
    fis[1]  = FIS_H2D_C;
    fis[2]  = cmd;
    fis[3]  = 0x00;                        /* features */
    fis[4]  = (u8)(lba & 0xFF);            /* LBA 7:0 */
    fis[5]  = (u8)((lba >> 8) & 0xFF);     /* LBA 15:8 */
    fis[6]  = (u8)((lba >> 16) & 0xFF);    /* LBA 23:16 */
    fis[7]  = (u8)(0xE0u | ((lba >> 24) & 0x0Fu)); /* Device: LBA mode,
                                                    * master, LBA 27:24 */
    fis[8]  = (u8)((lba >> 24) & 0xFF);    /* LBA 31:24 */
    fis[9]  = (u8)((lba >> 32) & 0xFF);    /* LBA 39:32 */
    fis[10] = (u8)((lba >> 40) & 0xFF);    /* LBA 47:40 */
    fis[11] = 0x00;                        /* features exp */
    fis[12] = (u8)(sectors & 0xFF);        /* count low */
    fis[13] = (u8)((sectors >> 8) & 0xFF); /* count high */
}

/* Split a request into <=8-sector DMA commands through the bounce buffer. */
static int driver_block_ahci_port_rw(driver_block_ahci_port_t *p, int is_write, u64 lba, u32 count,
                        void *buf) {
    u8 *out = (u8 *)buf;
    u64 cur = lba;
    u32 remaining = count;
    while (remaining > 0) {
        u32 n = (remaining > AHCI_PORT_BUF_SECT) ? AHCI_PORT_BUF_SECT : remaining;
        u32 bytes = n * 512u;
        if (is_write) memcpy(p->bounce, out, bytes);
        u8 fis[20];
        driver_block_ahci_build_fis(fis, is_write ? ATA_CMD_WRITE_DMA_EXT : ATA_CMD_READ_DMA_EXT,
                       cur, n);
        if (driver_block_ahci_port_issue(p, fis, is_write, p->bounce, bytes) != 0) return -1;
        if (!is_write) memcpy(out, p->bounce, bytes);
        out += bytes;
        cur += n;
        remaining -= n;
    }
    return 0;
}

/* ---- IDENTIFY + capacity ---- */

static int driver_block_ahci_port_identify(driver_block_ahci_port_t *p) {
    u8 fis[20];
    driver_block_ahci_build_fis(fis, ATA_CMD_IDENTIFY, 0, 1);
    if (driver_block_ahci_port_issue(p, fis, 0, p->bounce, 512) != 0) {
        /* Diagnose: dump PxCI / PxTFD / PxIS / PxCMD at failure time. */
        char buf[96]; char n[20];
        u32 ci  = p_read(p, AHCI_PxCI);
        u32 tfd = p_read(p, AHCI_PxTFD);
        u32 is  = p_read(p, AHCI_PxIS);
        u32 cmd = p_read(p, AHCI_PxCMD);
        strcpy(buf, "ahci: ident diag CI=0x");
        u64_to_hex(ci, n, 0); strcpy(buf + strlen(buf), n);
        strcpy(buf + strlen(buf), " TFD=0x");
        u64_to_hex(tfd, n, 0); strcpy(buf + strlen(buf), n);
        strcpy(buf + strlen(buf), " IS=0x");
        u64_to_hex(is, n, 0); strcpy(buf + strlen(buf), n);
        strcpy(buf + strlen(buf), " CMD=0x");
        u64_to_hex(cmd, n, 0); strcpy(buf + strlen(buf), n);
        strcpy(buf + strlen(buf), "\n");
        driver_block_ahci_log(buf);
        return -1;
    }
    /* Capacity: words 100/101 (48-bit LBA user-addressable sectors). */
    u16 *id = (u16 *)p->bounce;
    u64 lo = id[100];
    u64 hi = id[101];
    p->sectors = lo | (hi << 16);
    /* Fall back to 28-bit capacity (words 60/61) if EXT is 0. */
    if (p->sectors == 0) {
        p->sectors = (u64)id[60] | ((u64)id[61] << 16);
    }
    return (p->sectors > 0) ? 0 : -1;
}

/* ---- blk integration ---- */

static int driver_block_ahci_blk_read(driver_block_device_t *dev, u64 lba, u32 count, void *buf) {
    driver_block_ahci_port_t *p = (driver_block_ahci_port_t *)dev->priv;
    if (!p || !p->present || !p->is_ata) return -1;
    if (count == 0) return 0;
    if (lba + count > p->sectors) return -1;
    return driver_block_ahci_port_rw(p, 0, lba, count, buf);
}

static int driver_block_ahci_blk_write(driver_block_device_t *dev, u64 lba, u32 count, const void *buf) {
    driver_block_ahci_port_t *p = (driver_block_ahci_port_t *)dev->priv;
    if (!p || !p->present || !p->is_ata) return -1;
    if (count == 0) return 0;
    if (lba + count > p->sectors) return -1;
    return driver_block_ahci_port_rw(p, 1, lba, count, (void *)buf);
}

static int driver_block_ahci_blk_flush(driver_block_device_t *dev) {
    driver_block_ahci_port_t *p = (driver_block_ahci_port_t *)dev->priv;
    if (!p || !p->present || !p->is_ata) return -1;
    u8 fis[20];
    driver_block_ahci_build_fis(fis, ATA_CMD_FLUSH_EXT, 0, 0);
    return driver_block_ahci_port_issue(p, fis, 1, p->bounce, 512);
}

static const driver_block_ops_t driver_block_ahci_blk_ops = {
    .read  = driver_block_ahci_blk_read,
    .write = driver_block_ahci_blk_write,
    .flush = driver_block_ahci_blk_flush,
};

/* Engine bounce: stop the port, clear SERR/IS, start again.  Empirically
 * required once after port start on QEMU before commands execute. */
static int driver_block_ahci_port_bounce(driver_block_ahci_port_t *p) {
    if (driver_block_ahci_port_stop(p) != 0) return -1;
    p_write(p, AHCI_PxSERR, 0xFFFFFFFFu);
    p_write(p, AHCI_PxIS, p_read(p, AHCI_PxIS));
    driver_block_ahci_port_start(p);
    return 0;
}

/* IDENTIFY with up to 2 bounces (QEMU's first-command quirk). */
static int driver_block_ahci_port_identify_retry(driver_block_ahci_port_t *p) {
    for (int attempt = 0; attempt < 3; attempt++) {
        if (driver_block_ahci_port_identify(p) == 0) return 0;
        if (attempt < 2) driver_block_ahci_port_bounce(p);
    }
    return -1;
}

/* ---- init ---- */

static void driver_block_ahci_setup_port(driver_block_ahci_ctrl_t *c, u32 pi) {
    driver_block_ahci_port_t *p = &c->ports[pi];
    p->regs = (volatile u32 *)(c->mmio_base + AHCI_REG_PORTS
                               + (u64)pi * AHCI_PORT_REG_SIZE);

    u32 ssts = p_read(p, AHCI_PxSSTS);
    u32 sig  = p_read(p, AHCI_PxSIG);
    if (AHCI_SSTS_DET(ssts) != AHCI_DET_PRESENT) return;
    if (sig != AHCI_SIG_ATA) {
        if (sig == AHCI_SIG_ATAPI) {
            driver_block_ahci_log("ahci: port ");
            driver_block_ahci_log_dec("", pi, ": ATAPI (not supported)\n");
        }
        return;
    }
    p->present = 1;
    p->is_ata  = 1;

    /* Stop the engine, then program the DMA structures. */
    if (driver_block_ahci_port_stop(p) != 0) {
        driver_block_ahci_log_dec("ahci: port ", pi, ": failed to stop engine\n");
        p->present = 0;
        return;
    }
    p->cmd_list = (driver_block_ahci_cmd_hdr_t *)driver_block_ahci_page_aligned(AHCI_PAGE);
    p->fis_rx   = (u8 *)driver_block_ahci_page_aligned(AHCI_PAGE);
    p->cmd_tab  = (driver_block_ahci_cmd_table_t *)driver_block_ahci_page_aligned(AHCI_PAGE);
    p->bounce   = (u8 *)driver_block_ahci_page_aligned(AHCI_PAGE);
    if (!p->cmd_list || !p->fis_rx || !p->cmd_tab || !p->bounce) {
        driver_block_ahci_log_dec("ahci: port ", pi, ": out of memory\n");
        p->present = 0;
        return;
    }
    /* cmd list must be 1 KiB aligned; our page alignment satisfies it.
     * Clear all structures. */
    memset(p->cmd_list, 0, 32 * sizeof(driver_block_ahci_cmd_hdr_t));
    memset(p->fis_rx, 0, 256);
    memset(p->cmd_tab, 0, sizeof(driver_block_ahci_cmd_table_t));
    memset(p->bounce, 0, 512);

    u64 clb = (u64)(uintptr_t)p->cmd_list;
    u64 fb  = (u64)(uintptr_t)p->fis_rx;
    p_write(p, AHCI_PxCLB,  (u32)(clb & 0xFFFFFFFFu));
    p_write(p, AHCI_PxCLBU, (u32)(clb >> 32));
    p_write(p, AHCI_PxFB,   (u32)(fb & 0xFFFFFFFFu));
    p_write(p, AHCI_PxFBU,  (u32)(fb >> 32));

    /* Clear SERR (write-1-to-clear) and any pending IS. */
    p_write(p, AHCI_PxSERR, 0xFFFFFFFFu);
    p_write(p, AHCI_PxIS, p_read(p, AHCI_PxIS));
    /* No per-port interrupts (polled driver). */
    p_write(p, AHCI_PxIE, 0);

    /* Restart the engine. */
    driver_block_ahci_port_start(p);

    /* Small settle delay: wait for TFD to report no BSY/DRQ. */
    for (u64 i = 0; i < AHCI_POLL_PORT_ITERS; i++) {
        u32 tfd = p_read(p, AHCI_PxTFD);
        if (!AHCI_TFD_STS_BSY(tfd) && !AHCI_TFD_STS_DRQ(tfd)) break;
    }

    /* IDENTIFY for capacity (with a bounce-retry for the QEMU
     * first-command quirk; see driver_block_ahci_port_bounce). */
    if (driver_block_ahci_port_identify_retry(p) != 0) {
        driver_block_ahci_log_dec("ahci: port ", pi, ": IDENTIFY failed\n");
        p->present = 0;
        return;
    }
}

/* WP-10a: bring up one AHCI controller at the given PCI location.
 * Returns 0 on success, -1 on failure (BAR5 invalid). */
static int driver_block_ahci_init_ctrl(u8 bus, u8 dev, u8 func) {
    if (g_ctrl_count >= (int)(sizeof(g_ctrls) / sizeof(g_ctrls[0]))) return -1;

    driver_block_ahci_ctrl_t *c = &g_ctrls[g_ctrl_count];
    c->bus = bus; c->dev = dev; c->func = func;

    {
        char buf[64]; char n[20];
        strcpy(buf, "ahci: controller at ");
        u64_to_hex((u64)bus, n, 2); strcpy(buf + strlen(buf), n);
        strcpy(buf + strlen(buf), ":");
        u64_to_hex((u64)dev, n, 2); strcpy(buf + strlen(buf), n);
        strcpy(buf + strlen(buf), ".");
        u64_to_hex((u64)func, n, 1); strcpy(buf + strlen(buf), n);
        strcpy(buf + strlen(buf), "\n");
        driver_block_ahci_log(buf);
    }

    driver_pci_enable_device(bus, dev, func);
    u64 bar = (u64)driver_pci_read_bar(bus, dev, func, 5);
    if (bar == 0) {
        driver_block_ahci_log("ahci: BAR5 invalid\n");
        return -1;
    }
    c->mmio_base = bar;

    /* Enable AHCI mode, keep host interrupts off. */
    u32 ghc = mmio_read32((volatile void *)(bar + AHCI_REG_GHC));
    ghc |= AHCI_GHC_AE;
    ghc &= ~AHCI_GHC_IE;
    mmio_write32((volatile void *)(bar + AHCI_REG_GHC), ghc);

    c->cap = mmio_read32((volatile void *)(bar + AHCI_REG_CAP));
    c->pi  = mmio_read32((volatile void *)(bar + AHCI_REG_PI));
    u32 np = AHCI_CAP_NP(c->cap) + 1u;
    driver_block_ahci_log_hex("ahci: CAP=0x", c->cap, "\n");
    driver_block_ahci_log_dec("ahci: ports implemented=", np, " PI=0x");
    {
        char n[20];
        u64_to_hex(c->pi, n, 0);
        driver_block_ahci_log(n);
        driver_block_ahci_log("\n");
    }

    memset(c->ports, 0, sizeof(c->ports));
    for (u32 i = 0; i < np && i < AHCI_MAX_PORTS; i++) {
        if (!(c->pi & (1u << i))) continue;
        driver_block_ahci_setup_port(c, i);
    }

    /* Register detected drives. */
    for (u32 i = 0; i < np && i < AHCI_MAX_PORTS; i++) {
        driver_block_ahci_port_t *p = &c->ports[i];
        if (!p->present || !p->is_ata || g_drive_count >= AHCI_MAX_DRIVES)
            continue;
        driver_block_ahci_drive_t *d = &g_drives[g_drive_count];
        d->port = p;
        char nm[8];
        nm[0] = 's'; nm[1] = 'd';
        nm[2] = (char)('a' + g_drive_count);
        nm[3] = 0;
        strcpy(d->name, nm);
        p->driver_block_idx = driver_block_register_device(d->name, BLK_TYPE_AHCI,
                                         p->sectors, 512,
                                         &driver_block_ahci_blk_ops, p);
        if (p->driver_block_idx >= 0) {
            driver_block_ahci_log("ahci: registered ");
            driver_block_ahci_log(d->name);
            driver_block_ahci_log_dec(" (", p->sectors, " sectors)\n");
            g_drive_count++;
        }
    }
    g_ctrl_count++;
    return 0;
}

/* WP-10a L1 extension interface.  pdev == NULL enumerates every AHCI
 * controller on the PCI bus (boot path); pdev != NULL brings up exactly
 * that PCI function (extension path) after verifying its class code. */
int driver_block_ahci_init(driver_pci_dev_t *pdev) {
    if (pdev) {
        u32 cls = (driver_pci_read_config(pdev->bus, pdev->dev, pdev->func, 0x08) >> 8) & 0xFFFFFFu;
        if (cls != 0x010601u) {
            driver_block_ahci_log("ahci: requested PCI function is not class 010601\n");
            return -1;
        }
        return driver_block_ahci_init_ctrl(pdev->bus, pdev->dev, pdev->func) == 0 ? 1 : 0;
    }
    /* Find up to 2 AHCI controllers. */
    for (int nth = 0; nth < 2; nth++) {
        u8 bus = 0, dev = 0, func = 0;
        if (driver_pci_find_class_exact(0x010601u, nth, &bus, &dev, &func) != 0) break;
        driver_block_ahci_init_ctrl(bus, dev, func);
    }
    if (g_ctrl_count == 0) {
        driver_block_ahci_log("ahci: no controller found\n");
    }
    return g_ctrl_count;
}

int driver_block_ahci_num_drives(void) { return g_drive_count; }

int driver_block_ahci_blk_index(int n) {
    if (n < 0 || n >= g_drive_count) return -1;
    return g_drives[n].port->driver_block_idx;
}

void driver_block_ahci_print_state(void) {
    if (g_ctrl_count == 0) {
        driver_block_ahci_log("ahci: no controller detected\n");
        return;
    }
    for (int ci = 0; ci < g_ctrl_count; ci++) {
        driver_block_ahci_ctrl_t *c = &g_ctrls[ci];
        char buf[96]; char n[24];
        strcpy(buf, "controller ");
        u64_to_str((u64)ci, n); strcpy(buf + strlen(buf), n);
        strcpy(buf + strlen(buf), " at PCI ");
        u64_to_hex((u64)c->bus, n, 2); strcpy(buf + strlen(buf), n);
        strcpy(buf + strlen(buf), ":");
        u64_to_hex((u64)c->dev, n, 2); strcpy(buf + strlen(buf), n);
        strcpy(buf + strlen(buf), ".");
        u64_to_hex((u64)c->func, n, 1); strcpy(buf + strlen(buf), n);
        strcpy(buf + strlen(buf), " BAR5=0x");
        u64_to_hex(c->mmio_base, n, 0); strcpy(buf + strlen(buf), n);
        strcpy(buf + strlen(buf), "\n");
        driver_block_ahci_log(buf);
        driver_block_ahci_log_hex("  CAP=0x", c->cap, "  PI=0x");
        u64_to_hex(c->pi, n, 0);
        driver_block_ahci_log(n);
        driver_block_ahci_log("\n  NCQ support: ");
        driver_block_ahci_log(AHCI_CAP_SNCQ(c->cap) ? "yes\n" : "no\n");
        for (u32 i = 0; i < AHCI_MAX_PORTS; i++) {
            driver_block_ahci_port_t *p = &c->ports[i];
            if (!p->present) continue;
            u32 ssts = p_read(p, AHCI_PxSSTS);
            u32 tfd  = p_read(p, AHCI_PxTFD);
            strcpy(buf, "  port ");
            u64_to_str(i, n); strcpy(buf + strlen(buf), n);
            strcpy(buf + strlen(buf), ": SATA present, SSTS=0x");
            u64_to_hex(ssts, n, 0); strcpy(buf + strlen(buf), n);
            strcpy(buf + strlen(buf), " TFD=0x");
            u64_to_hex(tfd, n, 0); strcpy(buf + strlen(buf), n);
            strcpy(buf + strlen(buf), " sectors=");
            u64_to_str(p->sectors, n); strcpy(buf + strlen(buf), n);
            strcpy(buf + strlen(buf), "\n");
            driver_block_ahci_log(buf);
        }
    }
    driver_block_ahci_log_dec("sata drives registered: ", (u64)g_drive_count, "\n");
}
