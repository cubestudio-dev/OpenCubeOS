/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-07 / WP-10a
 * File: kernel/nvme.c
 * Purpose: NVMe PCI driver - controller init + admin/I/O queues + NVM I/O.
 *
 * Probe flow
 * ----------
 *  1. driver_pci_find_class(0x010802) -> NVMe controller BDF.
 *  2. driver_pci_enable_device + read BAR0 -> MMIO base.
 *  3. Read CAP (MQES, DSTRD), VS.
 *  4. Disable controller (CC=0), wait CSTS.RDY=0.
 *  5. Allocate + program admin SQ/CQ (page-aligned).
 *  6. CC = EN | AQS=63 | MPS=0 | AMS=0 | CSS=0 ; wait CSTS.RDY=1.
 *  7. Identify Controller (admin opcode 0x06, CNS=1) -> NN.
 *  8. Identify Namespace 1 (admin opcode 0x06, CNS=0, NSID=1)
 *     -> NSZE (capacity) + LBADS (sector size from FLBAS-indexed LBAF).
 *  9. Create I/O CQ + SQ pairs (WP-10a: TWO pairs, qid=1 and qid=2,
 *     both fully initialized and used by the I/O path).
 * 11. Register block device "nvme0" via the blk layer.
 *
 * I/O model (WP-10a)
 * ------------------
 *  - NVME_NUM_IOQ live I/O queue pairs (qid 1..NVME_NUM_IOQ), 64 deep
 *    each, polled (no interrupts).
 *  - One command outstanding per pair at a time (synchronous); the
 *    I/O path round-robins across the pairs (g_next_q), so every queue
 *    is exercised on real traffic.
 *  - A 4 KiB page-aligned bounce buffer is used for PRP1.  Transfers
 *    larger than (4096 / sector_size) sectors are split into chunks.
 *  - Each chunk uses PRP1=bounce phys, PRP2=0 (fits in one page).
 *  - driver_block_flush() issues the NVMe Flush command (opcode 0x0A, NSID=1).
 *
 * Heap note
 * ---------
 * kmalloc returns 16-byte aligned memory (HEAP_ALIGN=16, see heap.c),
 * but NVMe queues and PRP buffers must be 4 KiB aligned.  We over-
 * allocate by one page and align manually.
 */

#include "driver_block_nvme.h"
#include "types.h"
#include "mem_heap.h"
#include "lib_string.h"
#include "screen_console.h"
#include "driver_pci.h"
#include "driver_block_blk.h"

/* ---- MMIO accessors (file-local) ---- */
static inline u32 mmio_read32(volatile void *p) {
    return *(volatile u32 *)p;
}
static inline void mmio_write32(volatile void *p, u32 v) {
    *(volatile u32 *)p = v;
}

/* ---- NVMe controller register offsets (BAR0 MMIO) ----
 * Layout per the NVMe base spec (all revisions):
 *   0x00 CAP (64-bit) | 0x08 VS | 0x0C INTMS | 0x10 INTMC
 *   0x14 CC | 0x1C CSTS | 0x20 NSSR | 0x24 AQA | 0x28 ASQ | 0x30 ACQ
 *   0x1000+ doorbells.
 * WP-10a FIX: the WP-07 table had VS at 0x10 and everything above
 * shifted by 8, so CC/CSTS/AQA/ASQ/ACQ writes landed on the wrong
 * registers and the controller never came online when an IDE
 * controller shared the bus. */
#define NVME_REG_CAP        0x00u
#define NVME_REG_VS         0x08u
#define NVME_REG_INTMS      0x0Cu
#define NVME_REG_INTMC      0x10u
#define NVME_REG_CC         0x14u
#define NVME_REG_CSTS       0x1Cu
#define NVME_REG_NSSR       0x20u
#define NVME_REG_AQA        0x24u
#define NVME_REG_ASQ        0x28u
#define NVME_REG_ACQ        0x30u
#define NVME_REG_DBL_BASE   0x1000u

/* CC field bit positions. */
#define NVME_CC_EN_BIT      0u
#define NVME_CC_EN          (1u << NVME_CC_EN_BIT)
#define NVME_CC_CSS_SHIFT   3u
#define NVME_CC_MPS_SHIFT   4u
#define NVME_CC_AMS_SHIFT   7u
#define NVME_CC_IOSQES_SHIFT 16u   /* WP-10a: SQ entry size (6 = 64 B) */
#define NVME_CC_IOCQES_SHIFT 20u   /* WP-10a: CQ entry size (4 = 16 B) */

/* CSTS bits. */
#define NVME_CSTS_RDY       0x00000001u

/* CAP field extractors. */
#define NVME_CAP_MQES(cap)  ((cap) & 0xFFFFULL)
#define NVME_CAP_DSTRD(cap) (((cap) >> 32) & 0xFULL)
/* Admin opcodes. */
/* Admin opcodes (per NVMe base spec: Delete SQ 0x00, Create SQ 0x01,
 * Get Log Page 0x02, Delete CQ 0x04, Create CQ 0x05, Identify 0x06).
 * WP-10a FIX: Create CQ was 0x03 (an invalid/Get-Features-adjacent
 * value) and every I/O queue creation failed with INVALID_OPCODE. */
#define NVME_ADMIN_DELETE_SQ   0x00u
#define NVME_ADMIN_CREATE_SQ   0x01u
#define NVME_ADMIN_GET_LOG     0x02u
#define NVME_ADMIN_DELETE_CQ   0x04u
#define NVME_ADMIN_CREATE_CQ   0x05u
#define NVME_ADMIN_IDENTIFY    0x06u

/* NVM I/O opcodes. */
#define NVME_NVM_WRITE         0x01u
#define NVME_NVM_READ          0x02u
#define NVME_NVM_FLUSH         0x0Au   /* WP-10a: driver_block_flush hook */

/* Create I/O CQ / SQ cdw11 flag bits. */
#define NVME_CQ_PC             (1u << 0)   /* Physically Contiguous */
#define NVME_CQ_IEN            (1u << 1)   /* Interrupts Enabled */
#define NVME_SQ_PC             (1u << 0)

/* Sizes. */
#define NVME_ADMIN_QSIZE       64u
#define NVME_IO_QSIZE          64u
#define NVME_PAGE_SIZE         4096u
#define NVME_PAGE_SHIFT        12u

/* WP-10a: multiple I/O queue pairs.  QEMU's NVMe controller advertises
 * MQES=7 (8 entries) minimum; we ask for 2 pairs of 64 (or MQES+1 when
 * the controller is smaller).  The scheduler is single-threaded, but the
 * pairs are real: each is allocated, created via admin commands, and
 * used by the round-robin I/O path. */
#define NVME_NUM_IOQ           2u
#define NVME_MAX_IOQ           4u

/* Poll iteration budget.  NVMe controller reset can take up to CAP.TO*500ms
 * (typically a few hundred ms on QEMU).  With -O2 volatile reads this loop
 * is ~100ns/iter, so 50M iters ~= 5s. */
#define NVME_POLL_RESET_ITERS   50000000u
#define NVME_POLL_CMD_ITERS     50000000u

/* ---- on-wire structures (packed) ---- */
#pragma pack(push, 1)
typedef struct {
    u8  opcode;
    u8  flags;
    u16 cid;
    u32 nsid;
    u64 cdw2_3;
    u64 mptr;
    u64 prp1;
    u64 prp2;
    u32 cdw10;
    u32 cdw11;
    u32 cdw12;
    u32 cdw13;
    u32 cdw14;
    u32 cdw15;
} driver_block_nvme_sqe_t;   /* 64 bytes */

typedef struct {
    u32 cdw0;
    u32 rsvd;
    u16 sq_head;
    u16 sq_id;
    u16 cid;
    u16 status;   /* bit 0 = P (phase), bits 1:8 = SC, bits 9:11 = SCT, ... */
} driver_block_nvme_cqe_t;    /* 16 bytes */
#pragma pack(pop)

/* ---- per-device state ---- */

/* WP-10a: one I/O queue pair. */
typedef struct {
    driver_block_nvme_sqe_t *sq;
    driver_block_nvme_cqe_t *cq;
    u16 sq_tail;
    u16 cq_head;
    /* BUG-0114 FIX: the queue is created with min(NVME_IO_QSIZE, mqes+1)
     * entries, but the tails used to wrap with a fixed % NVME_IO_QSIZE.
     * On a device whose MQES < 63 the tails escaped the allocated rings
     * (out-of-bounds SQE write) and illegal doorbell values were rung.
     * Every queue now stores its real size and wraps against it. */
    u16 qsize;     /* actual entries in this queue (<= NVME_IO_QSIZE) */
    u8  cq_phase;
    u16 cid;
    u16 qid;       /* 1-based queue id (matches admin-created qid) */
} driver_block_nvme_ioq_t;

typedef struct {
    u64 mmio_base;
    u32 dstrd;          /* doorbell stride: 4 << dstrd bytes per doorbell */
    u16 mqes;           /* max queue entries (size = mqes+1) */

    /* Admin queue (qid=0). */
    driver_block_nvme_sqe_t *asq;
    driver_block_nvme_cqe_t *acq;
    u16 asq_tail;       /* next slot to write in ASQ */
    u16 acq_head;       /* next slot to consume in ACQ */
    u8  acq_phase;      /* expected phase bit for new ACQ entries */
    u16 admin_cid;

    /* I/O queues (qid=1..NVME_NUM_IOQ). */
    driver_block_nvme_ioq_t ioq[NVME_MAX_IOQ];
    u32  ioq_count;     /* live pairs */
    u32  next_q;        /* round-robin submit index */

    /* Device geometry. */
    u32 sector_size;
    u64 sectors;

    /* Bounce buffer (page-aligned, identity-mapped). */
    u8 *bounce;

    /* blk layer index (WP-10a, -1 until registered). */
    int driver_block_idx;
} driver_block_nvme_dev_t;

static driver_block_nvme_dev_t g_nvme;

/* Forward declarations for driver_block_ops. */
static int driver_block_nvme_blk_read (driver_block_device_t *dev, u64 lba, u32 count, void *buf);
static int driver_block_nvme_blk_write(driver_block_device_t *dev, u64 lba, u32 count, const void *buf);
static int driver_block_nvme_blk_flush(driver_block_device_t *dev);
static const driver_block_ops_t driver_block_nvme_blk_ops = {
    .read  = driver_block_nvme_blk_read,
    .write = driver_block_nvme_blk_write,
    .flush = driver_block_nvme_blk_flush,
};

/* ---- low-level helpers ---- */

static u32 driver_block_nvme_read(driver_block_nvme_dev_t *d, u32 off) {
    return mmio_read32((volatile void *)(d->mmio_base + off));
}
static void driver_block_nvme_write(driver_block_nvme_dev_t *d, u32 off, u32 v) {
    mmio_write32((volatile void *)(d->mmio_base + off), v);
}

/* Doorbell register offset. */
static u32 driver_block_nvme_sq_db(driver_block_nvme_dev_t *d, u16 qid) {
    return NVME_REG_DBL_BASE + (2u * (u32)qid) * (4u << d->dstrd);
}
static u32 driver_block_nvme_cq_db(driver_block_nvme_dev_t *d, u16 qid) {
    return NVME_REG_DBL_BASE + (2u * (u32)qid + 1u) * (4u << d->dstrd);
}

static void driver_block_nvme_log(const char *s) { screen_console_puts(s); }

static void driver_block_nvme_log_hex(const char *prefix, u64 v, const char *suffix) {
    char buf[64]; char n[20];
    strcpy(buf, prefix);
    u64_to_hex(v, n, 0);
    strcpy(buf + strlen(buf), n);
    strcpy(buf + strlen(buf), suffix);
    driver_block_nvme_log(buf);
}

static void driver_block_nvme_log_dec(const char *prefix, u64 v, const char *suffix) {
    char buf[80]; char n[24];
    strcpy(buf, prefix);
    u64_to_str(v, n);
    strcpy(buf + strlen(buf), n);
    strcpy(buf + strlen(buf), suffix);
    driver_block_nvme_log(buf);
}

/* Over-allocate then page-align.  Heap returns 16-byte aligned; we need
 * 4 KiB for NVMe queues and PRP buffers.  Small one-time waste. */
static void *driver_block_nvme_kmalloc_page_aligned(u64 size) {
    u8 *raw = (u8 *)kmalloc(size + NVME_PAGE_SIZE);
    if (!raw) return NULL;
    uintptr_t a = (uintptr_t)raw;
    a = (a + (NVME_PAGE_SIZE - 1)) & ~(uintptr_t)(NVME_PAGE_SIZE - 1);
    return (void *)a;
}

/* Wait for CSTS.RDY to equal `target` (1 = ready, 0 = disabled). */
static int driver_block_nvme_wait_rdy(driver_block_nvme_dev_t *d, int target) {
    for (u64 i = 0; i < NVME_POLL_RESET_ITERS; i++) {
        u32 csts = driver_block_nvme_read(d, NVME_REG_CSTS);
        if (target) {
            if (csts & NVME_CSTS_RDY) return 0;
        } else {
            if (!(csts & NVME_CSTS_RDY)) return 0;
        }
    }
    return -1;
}

/* Submit an admin command and synchronously wait for completion.
 * Returns 0 on success (SC=0), -1 on timeout/error.  The function
 * assigns a CID to the command. */
static int driver_block_nvme_admin_cmd(driver_block_nvme_dev_t *d, driver_block_nvme_sqe_t *cmd) {
    cmd->flags  = 0;
    cmd->cdw2_3 = 0;
    cmd->mptr   = 0;
    cmd->cid    = d->admin_cid;
    d->admin_cid = (u16)(d->admin_cid + 1);

    /* Memory barrier before publishing to the queue (x86 store order is
     * already TSO, but be explicit for the compiler). */
    __asm__ volatile("" ::: "memory");
    memcpy(&d->asq[d->asq_tail], cmd, sizeof(driver_block_nvme_sqe_t));
    d->asq_tail = (u16)((d->asq_tail + 1) % NVME_ADMIN_QSIZE);
    /* Doorbell write is volatile -> full barrier on x86. */
    driver_block_nvme_write(d, driver_block_nvme_sq_db(d, 0), d->asq_tail);

    u16 want_cid = cmd->cid;
    for (u64 i = 0; i < NVME_POLL_CMD_ITERS; i++) {
        volatile driver_block_nvme_cqe_t *cqe =
            (volatile driver_block_nvme_cqe_t *)&d->acq[d->acq_head];
        u16 status = cqe->status;
        u8  phase  = (u8)(status & 1u);
        if (phase != d->acq_phase) continue;   /* not a new entry */

        u16 cid = cqe->cid;
        u16 sc  = (u16)((status >> 1) & 0x7FFFu);

        /* Consume this CQE. */
        d->acq_head = (u16)((d->acq_head + 1) % NVME_ADMIN_QSIZE);
        if (d->acq_head == 0) d->acq_phase ^= 1;
        driver_block_nvme_write(d, driver_block_nvme_cq_db(d, 0), d->acq_head);

        if (cid != want_cid) {
            /* Stray completion (e.g. AEN we never submitted).  Drop it
             * and keep waiting for ours. */
            i = 0;
            continue;
        }
        if (sc != 0) {
            /* TEMP DIAG: report the NVMe status code. */
            driver_block_nvme_log_hex("nvme: admin cmd SC=0x", sc, "\n");
        }
        return (sc == 0) ? 0 : -1;
    }
    return -1;
}

/* Submit an I/O command on queue `q` and synchronously wait for completion. */
static int driver_block_nvme_io_cmd(driver_block_nvme_dev_t *d, driver_block_nvme_ioq_t *q, driver_block_nvme_sqe_t *cmd) {
    cmd->flags  = 0;
    cmd->cdw2_3 = 0;
    cmd->mptr   = 0;
    cmd->cid    = q->cid;
    q->cid      = (u16)(q->cid + 1);

    __asm__ volatile("" ::: "memory");
    memcpy(&q->sq[q->sq_tail], cmd, sizeof(driver_block_nvme_sqe_t));
    /* BUG-0114 FIX: wrap against the queue's real size, not the compile-time
     * maximum (see struct comment). */
    q->sq_tail = (u16)((q->sq_tail + 1) % q->qsize);
    driver_block_nvme_write(d, driver_block_nvme_sq_db(d, q->qid), q->sq_tail);

    u16 want_cid = cmd->cid;
    for (u64 i = 0; i < NVME_POLL_CMD_ITERS; i++) {
        volatile driver_block_nvme_cqe_t *cqe =
            (volatile driver_block_nvme_cqe_t *)&q->cq[q->cq_head];
        u16 status = cqe->status;
        u8  phase  = (u8)(status & 1u);
        if (phase != q->cq_phase) continue;

        u16 cid = cqe->cid;
        u16 sc  = (u16)((status >> 1) & 0x7FFFu);

        q->cq_head = (u16)((q->cq_head + 1) % q->qsize);   /* BUG-0114 FIX */
        if (q->cq_head == 0) q->cq_phase ^= 1;
        driver_block_nvme_write(d, driver_block_nvme_cq_db(d, q->qid), q->cq_head);

        if (cid != want_cid) {
            i = 0;
            continue;
        }
        return (sc == 0) ? 0 : -1;
    }
    return -1;
}

/* ---- init ---- */

/* WP-10a L1 extension interface.  pdev == NULL probes the PCI bus for an
 * NVMe controller (class 0x010802, boot path); pdev != NULL brings up
 * exactly that PCI function (extension path) after verifying its class.
 * Returns 0 when a controller was brought online, 1 when no controller
 * was found (pdev == NULL only), -1 on error / bad class. */
int driver_block_nvme_init(driver_pci_dev_t *pdev) {
    u8 bus = 0, dev = 0, func = 0;
    g_nvme.driver_block_idx = -1;   /* WP-10a: default until registration succeeds */
    if (pdev) {
        u32 cls = (driver_pci_read_config(pdev->bus, pdev->dev, pdev->func, 0x08) >> 8) & 0xFFFFFFu;
        if (cls != 0x010802u) {
            driver_block_nvme_log("nvme: requested PCI function is not class 010802\n");
            return -1;
        }
        bus = pdev->bus; dev = pdev->dev; func = pdev->func;
    } else {
    /* WP-10a FIX: use the EXACT class triple (0x010802).  The old
     * driver_pci_find_class() matched ANY mass-storage device (base class 0x01),
     * so with an IDE controller present the NVMe probe latched onto the
     * IDE controller and never found the real NVMe device. */
    if (driver_pci_find_class_exact(0x010802u, 0, &bus, &dev, &func) != 0) {
        driver_block_nvme_log("nvme: no controller found\n");
        return 1;
    }
    }
    {
        char buf[64]; char n[20];
        strcpy(buf, "nvme: found at ");
        u64_to_hex((u64)bus, n, 2); strcpy(buf + strlen(buf), n);
        strcpy(buf + strlen(buf), ":");
        u64_to_hex((u64)dev, n, 2); strcpy(buf + strlen(buf), n);
        strcpy(buf + strlen(buf), ".");
        u64_to_hex((u64)func, n, 1); strcpy(buf + strlen(buf), n);
        strcpy(buf + strlen(buf), "\n");
        driver_block_nvme_log(buf);
    }

    driver_pci_enable_device(bus, dev, func);

    u64 mmio_base = (u64)driver_pci_read_bar(bus, dev, func, 0);
    if (mmio_base == 0) {
        driver_block_nvme_log("nvme: BAR0 invalid\n");
        return -1;
    }
    g_nvme.mmio_base = mmio_base;

    /* Read CAP (64-bit). */
    u32 cap_lo = driver_block_nvme_read(&g_nvme, NVME_REG_CAP);
    u32 cap_hi = driver_block_nvme_read(&g_nvme, NVME_REG_CAP + 4);
    u64 cap = (u64)cap_lo | ((u64)cap_hi << 32);
    g_nvme.mqes  = (u16)NVME_CAP_MQES(cap);
    g_nvme.dstrd = (u32)NVME_CAP_DSTRD(cap);
    driver_block_nvme_log_hex("nvme: CAP=0x", cap, "\n");
    driver_block_nvme_log_dec("nvme: MQES=", g_nvme.mqes, "");
    driver_block_nvme_log_dec(" DSTRD=", g_nvme.dstrd, "\n");

    u32 vs = driver_block_nvme_read(&g_nvme, NVME_REG_VS);
    driver_block_nvme_log_hex("nvme: VS=0x", (u64)vs, "\n");
    (void)vs;

    /* Disable controller first; wait for RDY=0. */
    driver_block_nvme_write(&g_nvme, NVME_REG_CC, 0);
    if (driver_block_nvme_wait_rdy(&g_nvme, 0) != 0) {
        driver_block_nvme_log("nvme: timeout waiting for disable\n");
        return -1;
    }

    /* Allocate admin queues (page-aligned). */
    g_nvme.asq = (driver_block_nvme_sqe_t *)driver_block_nvme_kmalloc_page_aligned(
        NVME_ADMIN_QSIZE * sizeof(driver_block_nvme_sqe_t));
    g_nvme.acq = (driver_block_nvme_cqe_t *)driver_block_nvme_kmalloc_page_aligned(
        NVME_ADMIN_QSIZE * sizeof(driver_block_nvme_cqe_t));
    if (!g_nvme.asq || !g_nvme.acq) {
        driver_block_nvme_log("nvme: out of memory (admin queue)\n");
        return -1;
    }
    memset(g_nvme.asq, 0, NVME_ADMIN_QSIZE * sizeof(driver_block_nvme_sqe_t));
    memset(g_nvme.acq, 0, NVME_ADMIN_QSIZE * sizeof(driver_block_nvme_cqe_t));
    g_nvme.asq_tail   = 0;
    g_nvme.acq_head   = 0;
    g_nvme.acq_phase  = 1;   /* first completion phase after CC.EN=1 */
    g_nvme.admin_cid  = 1;

    /* Program AQA, ASQ, ACQ. */
    u32 aqa = ((NVME_ADMIN_QSIZE - 1u) << 16) | (NVME_ADMIN_QSIZE - 1u);
    driver_block_nvme_write(&g_nvme, NVME_REG_AQA, aqa);
    {
        u64 asq_phys = (u64)(uintptr_t)g_nvme.asq;
        u64 acq_phys = (u64)(uintptr_t)g_nvme.acq;
        driver_block_nvme_write(&g_nvme, NVME_REG_ASQ,     (u32)(asq_phys & 0xFFFFFFFFu));
        driver_block_nvme_write(&g_nvme, NVME_REG_ASQ + 4, (u32)(asq_phys >> 32));
        driver_block_nvme_write(&g_nvme, NVME_REG_ACQ,     (u32)(acq_phys & 0xFFFFFFFFu));
        driver_block_nvme_write(&g_nvme, NVME_REG_ACQ + 4, (u32)(acq_phys >> 32));
    }

    /* Enable controller.
     * WP-10a FIX: CC must set IOSQES = 6 (64-byte SQE, bits 19:16) and
     * IOCQES = 4 (16-byte CQE, bits 23:20).  The old code wrote an
     * "AQS" admin-queue-size value into bits 19:16 (there is no such
     * field in CC - the admin queue size lives in AQA), which made every
     * Create CQ fail with MAX_QSIZE_EXCEEDED. */
    {
        u32 cc = NVME_CC_EN
               | (0u << NVME_CC_CSS_SHIFT)   /* round-robin ARB */
               | (0u << NVME_CC_MPS_SHIFT)   /* MPS = 0 (4 KiB page) */
               | (0u << NVME_CC_AMS_SHIFT)   /* round-robin */
               | (6u << 16)                  /* IOSQES = 6 (64-byte SQE) */
               | (4u << 20);                 /* IOCQES = 4 (16-byte CQE) */
        driver_block_nvme_write(&g_nvme, NVME_REG_CC, cc);
    }
    if (driver_block_nvme_wait_rdy(&g_nvme, 1) != 0) {
        driver_block_nvme_log("nvme: timeout waiting for ready\n");
        return -1;
    }
    driver_block_nvme_log("nvme: controller enabled\n");

    /* Identify Controller (CNS=1). */
    {
        u8 *id_ctrl = (u8 *)driver_block_nvme_kmalloc_page_aligned(4096);
        if (!id_ctrl) { driver_block_nvme_log("nvme: oom id_ctrl\n"); return -1; }
        driver_block_nvme_sqe_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.opcode = NVME_ADMIN_IDENTIFY;
        cmd.nsid   = 0;
        cmd.prp1   = (u64)(uintptr_t)id_ctrl;
        cmd.prp2   = 0;
        cmd.cdw10  = 1;   /* CNS=1 (Identify Controller) */
        if (driver_block_nvme_admin_cmd(&g_nvme, &cmd) != 0) {
            driver_block_nvme_log("nvme: Identify Controller failed\n");
            return -1;
        }
        u32 nn = *(u32 *)(id_ctrl + 516);   /* Number of Namespaces */
        driver_block_nvme_log_dec("nvme: NN=", nn, " namespaces\n");
        (void)nn;
    }

    /* Identify Namespace 1 (CNS=0, NSID=1). */
    u8 *id_ns = (u8 *)driver_block_nvme_kmalloc_page_aligned(4096);
    if (!id_ns) { driver_block_nvme_log("nvme: oom id_ns\n"); return -1; }
    {
        driver_block_nvme_sqe_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.opcode = NVME_ADMIN_IDENTIFY;
        cmd.nsid   = 1;
        cmd.prp1   = (u64)(uintptr_t)id_ns;
        cmd.prp2   = 0;
        cmd.cdw10  = 0;   /* CNS=0 (Identify Namespace) */
        if (driver_block_nvme_admin_cmd(&g_nvme, &cmd) != 0) {
            driver_block_nvme_log("nvme: Identify Namespace failed\n");
            return -1;
        }
        u64 nsze = *(u64 *)(id_ns + 0);          /* Namespace Size (blocks) */
        u8  flbas = id_ns[26];                   /* Formatted LBA Size */
        u8  lbads = id_ns[128 + 4 * (flbas & 0x0F) + 2];   /* LBAF[flbas].LBADS */
        /* P0fix2 BUG-0040 (A6-1): the block and cache layers are hardwired
         * to 512-byte sectors (BLK_SECTOR_SIZE / cache slot data[512]).
         * Registering a device whose formatted LBA size is not 512B made
         * every driver_block_read_sectors() copy sector_size bytes into
         * 512-byte caller buffers — 3584 bytes of heap overflow per read
         * on a 4Kn drive.  Until the block layer is made sector-size-aware,
         * refuse the namespace with a clear error instead of corrupting
         * memory.  (Old code fell back to 512 only for lbads > 16.) */
        if (lbads != 9) {
            u32 reported = (lbads > 0 && lbads <= 16) ? (1u << lbads) : 0;
            driver_block_nvme_log("nvme: unsupported formatted LBA size (only 512B supported; got ");
            if (reported) {
                char nb[16];
                u64_to_str(reported, nb);
                driver_block_nvme_log(nb);
                driver_block_nvme_log("B");
            } else {
                driver_block_nvme_log("invalid LBADS");
            }
            driver_block_nvme_log(") - device disabled\n");
            return -1;
        }
        u32 sector_size = 512u;
        g_nvme.sector_size = sector_size;
        g_nvme.sectors     = nsze;
        driver_block_nvme_log_dec("nvme: NSZE=", nsze, " blocks\n");
        driver_block_nvme_log_dec("nvme: sector_size=", sector_size, "\n");
    }

    /* Allocate + create the I/O queue pairs (WP-10a: NVME_NUM_IOQ pairs,
     * qid=1..N; each CQ is created before its SQ, per spec order). */
    for (u32 qi = 0; qi < NVME_NUM_IOQ && qi < NVME_MAX_IOQ; qi++) {
        u16 qid = (u16)(qi + 1);
        u16 qsize = (u16)NVME_IO_QSIZE;
        if ((u32)g_nvme.mqes + 1u < qsize) qsize = (u16)(g_nvme.mqes + 1u);

        driver_block_nvme_ioq_t *q = &g_nvme.ioq[qi];
        memset(q, 0, sizeof(*q));
        q->sq = (driver_block_nvme_sqe_t *)driver_block_nvme_kmalloc_page_aligned(qsize * sizeof(driver_block_nvme_sqe_t));
        q->cq = (driver_block_nvme_cqe_t *)driver_block_nvme_kmalloc_page_aligned(qsize * sizeof(driver_block_nvme_cqe_t));
        if (!q->sq || !q->cq) {
            driver_block_nvme_log("nvme: out of memory (io queue)\n");
            break;
        }
        memset(q->sq, 0, qsize * sizeof(driver_block_nvme_sqe_t));
        memset(q->cq, 0, qsize * sizeof(driver_block_nvme_cqe_t));
        q->qsize    = qsize;   /* BUG-0114 FIX: remember the real size */
        q->sq_tail  = 0;
        q->cq_head  = 0;
        q->cq_phase = 1;   /* first completion phase after queue creation */
        q->cid      = 1;
        q->qid      = qid;

        /* Create I/O Completion Queue.
         * CDW10: QID = bits 15:0, QSIZE = bits 31:16 (per NVMe spec). */
        {
            driver_block_nvme_sqe_t cmd;
            memset(&cmd, 0, sizeof(cmd));
            cmd.opcode = NVME_ADMIN_CREATE_CQ;
            cmd.prp1   = (u64)(uintptr_t)q->cq;
            cmd.cdw10  = (u32)qid | ((u32)(qsize - 1u) << 16);
            cmd.cdw11  = NVME_CQ_PC | NVME_CQ_IEN;
            if (driver_block_nvme_admin_cmd(&g_nvme, &cmd) != 0) {
                driver_block_nvme_log("nvme: Create I/O CQ failed\n");
                break;
            }
        }

        /* Create I/O Submission Queue (mapped to CQ qid).
         * CDW10: QID = bits 15:0, QSIZE = bits 31:16.
         * CDW11: CQID = bits 31:16, PC = bit 0. */
        {
            driver_block_nvme_sqe_t cmd;
            memset(&cmd, 0, sizeof(cmd));
            cmd.opcode = NVME_ADMIN_CREATE_SQ;
            cmd.prp1   = (u64)(uintptr_t)q->sq;
            cmd.cdw10  = (u32)qid | ((u32)(qsize - 1u) << 16);
            cmd.cdw11  = NVME_SQ_PC | ((u32)qid << 16);   /* PC=1, CQID=qid */
            if (driver_block_nvme_admin_cmd(&g_nvme, &cmd) != 0) {
                driver_block_nvme_log("nvme: Create I/O SQ failed\n");
                break;
            }
        }
        g_nvme.ioq_count = qi + 1;
    }
    if (g_nvme.ioq_count == 0) {
        driver_block_nvme_log("nvme: no I/O queues created\n");
        return -1;
    }

    /* Allocate page-aligned bounce buffer for I/O. */
    g_nvme.bounce = (u8 *)driver_block_nvme_kmalloc_page_aligned(NVME_PAGE_SIZE);
    if (!g_nvme.bounce) {
        driver_block_nvme_log("nvme: out of memory (bounce)\n");
        return -1;
    }
    memset(g_nvme.bounce, 0, NVME_PAGE_SIZE);

    driver_block_nvme_log("nvme: I/O queues ready");
    driver_block_nvme_log_dec(" (", g_nvme.ioq_count, " pairs)\n");
    g_nvme.next_q  = 0;
    g_nvme.driver_block_idx = driver_block_register_device("nvme0", BLK_TYPE_NVME, g_nvme.sectors,
                                         g_nvme.sector_size, &driver_block_nvme_blk_ops,
                                         &g_nvme);
    return 0;
}

/* ---- I/O implementation ---- */

/* Submit a single NVMe I/O command on the round-robin queue using the
 * bounce buffer (already populated for writes).  nblocks must fit within
 * one page (=> nblocks * sector_size <= NVME_PAGE_SIZE).  Returns 0 on
 * success. */
static int driver_block_nvme_submit_io(driver_block_nvme_dev_t *d, u32 opcode, u64 lba, u32 nblocks) {
    if (d->ioq_count == 0) return -1;
    driver_block_nvme_ioq_t *q = &d->ioq[d->next_q % d->ioq_count];
    d->next_q = (d->next_q + 1u) % d->ioq_count;

    driver_block_nvme_sqe_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = (u8)opcode;
    cmd.nsid   = 1;
    cmd.prp1   = (u64)(uintptr_t)d->bounce;
    cmd.prp2   = 0;
    cmd.cdw10  = (u32)(lba & 0xFFFFFFFFu);
    cmd.cdw11  = (u32)(lba >> 32);
    cmd.cdw12  = (nblocks - 1u) & 0xFFFFu;   /* NLB-1, control bits = 0 */
    return driver_block_nvme_io_cmd(d, q, &cmd);
}

static int driver_block_nvme_blk_read(driver_block_device_t *dev, u64 lba, u32 count, void *buf) {
    driver_block_nvme_dev_t *d = (driver_block_nvme_dev_t *)dev->priv;
    if (!d || !d->bounce || d->sector_size == 0) return -1;
    if (count == 0) return 0;

    u32 max_per_chunk = NVME_PAGE_SIZE / d->sector_size;
    if (max_per_chunk == 0) max_per_chunk = 1;
    u8 *out = (u8 *)buf;
    u64 cur = lba;
    u32 remaining = count;
    while (remaining > 0) {
        u32 n = (remaining > max_per_chunk) ? max_per_chunk : remaining;
        if (driver_block_nvme_submit_io(d, NVME_NVM_READ, cur, n) != 0) return -1;
        memcpy(out, d->bounce, (u64)n * d->sector_size);
        out       += n * d->sector_size;
        cur       += n;
        remaining -= n;
    }
    return 0;
}

static int driver_block_nvme_blk_write(driver_block_device_t *dev, u64 lba, u32 count, const void *buf) {
    driver_block_nvme_dev_t *d = (driver_block_nvme_dev_t *)dev->priv;
    if (!d || !d->bounce || d->sector_size == 0) return -1;
    if (count == 0) return 0;

    u32 max_per_chunk = NVME_PAGE_SIZE / d->sector_size;
    if (max_per_chunk == 0) max_per_chunk = 1;
    const u8 *in = (const u8 *)buf;
    u64 cur = lba;
    u32 remaining = count;
    while (remaining > 0) {
        u32 n = (remaining > max_per_chunk) ? max_per_chunk : remaining;
        memcpy(d->bounce, in, (u64)n * d->sector_size);
        if (driver_block_nvme_submit_io(d, NVME_NVM_WRITE, cur, n) != 0) return -1;
        in        += n * d->sector_size;
        cur       += n;
        remaining -= n;
    }
    return 0;
}

/* WP-10a: flush hook - NVMe Flush command (opcode 0x0A, NSID=1). */
static int driver_block_nvme_blk_flush(driver_block_device_t *dev) {
    driver_block_nvme_dev_t *d = (driver_block_nvme_dev_t *)dev->priv;
    if (!d || !d->bounce || d->ioq_count == 0) return -1;
    driver_block_nvme_ioq_t *q = &d->ioq[d->next_q % d->ioq_count];
    d->next_q = (d->next_q + 1u) % d->ioq_count;
    driver_block_nvme_sqe_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = NVME_NVM_FLUSH;
    cmd.nsid   = 1;
    return driver_block_nvme_io_cmd(d, q, &cmd);
}

/* ---- WP-10a public state API ---- */

void driver_block_nvme_print_state(void) {
    if (!g_nvme.mmio_base || g_nvme.ioq_count == 0) {
        driver_block_nvme_log("nvme: no controller online\n");
        return;
    }
    char buf[96]; char n[24];
    driver_block_nvme_log("nvme: controller online, MMIO=0x");
    u64_to_hex(g_nvme.mmio_base, n, 0);
    driver_block_nvme_log(n);
    driver_block_nvme_log("\n");
    driver_block_nvme_log_dec("  sector_size=", g_nvme.sector_size, "\n");
    driver_block_nvme_log_dec("  namespaces capacity: ", g_nvme.sectors, " blocks\n");
    driver_block_nvme_log_dec("  I/O queue pairs: ", g_nvme.ioq_count, " (round-robin)\n");
    for (u32 i = 0; i < g_nvme.ioq_count; i++) {
        driver_block_nvme_ioq_t *q = &g_nvme.ioq[i];
        strcpy(buf, "  qid=");
        u64_to_str(q->qid, n); strcpy(buf + strlen(buf), n);
        strcpy(buf + strlen(buf), " size=");
        u64_to_str((u64)NVME_IO_QSIZE, n);
        strcpy(buf + strlen(buf), n);
        strcpy(buf + strlen(buf), " sq_tail=");
        u64_to_str(q->sq_tail, n); strcpy(buf + strlen(buf), n);
        strcpy(buf + strlen(buf), " cq_head=");
        u64_to_str(q->cq_head, n); strcpy(buf + strlen(buf), n);
        strcpy(buf + strlen(buf), "\n");
        driver_block_nvme_log(buf);
    }
    driver_block_nvme_log_dec("  blk device: nvme0 (index ", (u64)(g_nvme.driver_block_idx < 0 ? -1 : g_nvme.driver_block_idx), ")\n");
}

int driver_block_nvme_num_io_queues(void) {
    return (int)g_nvme.ioq_count;
}

int driver_block_nvme_blk_index(void) {
    return g_nvme.driver_block_idx;
}
