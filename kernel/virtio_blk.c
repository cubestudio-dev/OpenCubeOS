/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-07
 * File: kernel/virtio_blk.c
 * Purpose: virtio-blk PCI legacy transport driver.
 *
 * Probes for a virtio-blk PCI device (vendor 0x1AF4, device 0x1001 legacy
 * or 0x1042 modern-transitional), brings it online via the legacy I/O
 * transport, sets up the request virtqueue (queue 0), and registers a
 * block device "vda" with the blk layer.
 *
 * Transport notes
 * ---------------
 * QEMU's virtio-blk-pci exposes a legacy I/O BAR at BAR0.  All registers
 * listed below are accessed via inl/outl on (io_base + offset).  The
 * legacy device-config region for virtio-blk lives at I/O offset 0x64.
 *
 * Virtqueue layout (legacy, page-aligned)
 * ---------------------------------------
 *   [desc table]            qsz * 16 bytes
 *   [avail ring]            6 + qsz*2 bytes
 *   (padding to next page)
 *   [used ring]             6 + qsz*8 bytes
 *
 * The QueuePFN register takes the physical page number (phys >> 12) of
 * the descriptor table; the avail and used rings are at fixed offsets
 * from it.
 *
 * The driver is single-threaded: it submits one 3-descriptor request at
 * a time and busy-polls the used ring until completion.  No locking.
 */

#include "virtio_blk.h"
#include "types.h"
#include "heap.h"
#include "string.h"
#include "console.h"
#include "pci.h"
#include "blk.h"

/* ---- I/O port accessors (static inline, file-local) ---- */
static inline void outb(u16 p, u8 v)  { __asm__ volatile("outb %0, %1" :: "a"(v),  "Nd"(p)); }
static inline void outl(u16 p, u32 v) { __asm__ volatile("outl %0, %1" :: "a"(v),  "Nd"(p)); }
static inline u8   inb(u16 p)         { u8 v;  __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline u32  inl(u16 p)         { u32 v; __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(p)); return v; }

/* ---- virtio legacy register offsets within BAR0 I/O space ---- */
#define VIRTIO_REG_HOST_FEATURES   0x00u
#define VIRTIO_REG_GUEST_FEATURES  0x04u
#define VIRTIO_REG_QUEUE_PFN       0x08u
#define VIRTIO_REG_QUEUE_SIZE      0x0Cu
#define VIRTIO_REG_QUEUE_SELECT    0x10u
#define VIRTIO_REG_QUEUE_NOTIFY    0x14u
#define VIRTIO_REG_STATUS          0x18u
#define VIRTIO_REG_ISR             0x1Cu
#define VIRTIO_REG_BLK_CFG         0x64u

/* Status bits. */
#define VIRTIO_STATUS_ACK          0x01u
#define VIRTIO_STATUS_DRIVER       0x02u
#define VIRTIO_STATUS_DRIVER_OK    0x04u
#define VIRTIO_STATUS_FAILED       0x80u

/* Descriptor flags. */
#define VRING_DESC_F_NEXT          0x01u
#define VRING_DESC_F_WRITE         0x02u

/* virtio-blk request types. */
#define VIRTIO_BLK_T_IN            0u   /* read  */
#define VIRTIO_BLK_T_OUT           1u   /* write */

#define VIRTIO_BLK_SECTOR_SIZE     512u
#define VIRTIO_BLK_QUEUE_SIZE_MAX  1024u
#define VIRTIO_PAGE_SIZE           4096u

/* ---- on-wire structures (packed) ---- */

/* virtio-blk legacy device config (relevant prefix only). */
typedef struct __attribute__((packed)) {
    u64 capacity;   /* total sectors (read-only) */
    u32 size_max;
    u32 seg_max;
} virtio_blk_cfg_t;

/* Virtqueue descriptor (16 bytes). */
typedef struct __attribute__((packed)) {
    u64 addr;
    u32 len;
    u16 flags;
    u16 next;
} virtq_desc_t;

/* Available ring header.  Followed by qsz u16 ring entries. */
typedef struct __attribute__((packed)) {
    u16 flags;
    u16 idx;
    u16 ring[1];   /* flexible placeholder; index with ring[idx % qsz] */
} virtq_avail_t;

/* Used ring element (8 bytes). */
typedef struct __attribute__((packed)) {
    u32 id;
    u32 len;
} virtq_used_elem_t;

/* Used ring header.  Followed by qsz used_elem entries. */
typedef struct __attribute__((packed)) {
    u16 flags;
    u16 idx;
    virtq_used_elem_t ring[1];  /* flexible placeholder */
} virtq_used_t;

/* Block request header (16 bytes, sent to device). */
typedef struct __attribute__((packed)) {
    u32 type;
    u32 reserved;
    u64 sector;
} virtio_blk_outhdr_t;

/* ---- per-device state ---- */
typedef struct {
    u32  io_base;          /* BAR0 I/O base */
    u16  qsz;              /* negotiated queue size */
    void *vq_mem_raw;      /* original kmalloc pointer (for potential kfree) */
    volatile virtq_desc_t  *desc;
    volatile virtq_avail_t *avail;
    volatile virtq_used_t  *used;
    u16  avail_idx;        /* monotonic next slot in avail ring */
    u16  last_used_idx;    /* last seen used->idx */
    /* Per-request scratch (single-threaded; addresses must persist). */
    volatile virtio_blk_outhdr_t hdr;
    volatile u8  status;
} virtio_blk_dev_t;

static virtio_blk_dev_t g_dev;

/* Forward declarations for blk_ops. */
static int virtio_blk_read (blk_device_t *dev, u64 lba, u32 count, void *buf);
static int virtio_blk_write(blk_device_t *dev, u64 lba, u32 count, const void *buf);
static const blk_ops_t virtio_blk_ops = {
    .read  = virtio_blk_read,
    .write = virtio_blk_write,
};

/* ---- helpers ---- */

/* Over-allocate then page-align: kmalloc returns 16-byte aligned memory
 * (see heap.c, HEAP_ALIGN=16), but the virtqueue requires 4 KiB
 * alignment.  We lose track of the original pointer (small one-time
 * leak; the queue lives for the kernel's lifetime). */
static void *kmalloc_page_aligned(u64 size) {
    u8 *raw = (u8 *)kmalloc(size + VIRTIO_PAGE_SIZE);
    if (!raw) return NULL;
    uintptr_t a = (uintptr_t)raw;
    a = (a + (VIRTIO_PAGE_SIZE - 1)) & ~(uintptr_t)(VIRTIO_PAGE_SIZE - 1);
    return (void *)a;
}

static void vblk_status_set(u32 io_base, u8 v) {
    outb((u16)(io_base + VIRTIO_REG_STATUS), v);
}

static void vblk_log(const char *s) { oc_console_puts(s); }

static void vblk_log_pci(const char *prefix, u8 bus, u8 dev, u8 func) {
    char buf[64]; char n[20];
    oc_strcpy(buf, prefix);
    oc_u64_to_hex((u64)bus, n, 2); oc_strcpy(buf + oc_strlen(buf), n);
    oc_strcpy(buf + oc_strlen(buf), ":");
    oc_u64_to_hex((u64)dev, n, 2); oc_strcpy(buf + oc_strlen(buf), n);
    oc_strcpy(buf + oc_strlen(buf), ".");
    oc_u64_to_hex((u64)func, n, 1); oc_strcpy(buf + oc_strlen(buf), n);
    oc_strcpy(buf + oc_strlen(buf), "\n");
    vblk_log(buf);
}

/* ---- init ---- */

void virtio_blk_init(void) {
    u8 bus = 0, dev = 0, func = 0;
    /* P1 FIX: only accept legacy device IDs (0x1001 legacy, 0x1042 transitional).
     * The modern non-transitional ID (0x1041) uses MMIO transport which our
     * legacy I/O driver doesn't support. QEMU's virtio-blk-pci with
     * disable-modern=on exposes the legacy/transitional ID. */
    int found = pci_find_device(0x1AF4, 0x1001, &bus, &dev, &func);
    if (found != 0) {
        found = pci_find_device(0x1AF4, 0x1042, &bus, &dev, &func);
    }
    if (found != 0) {
        vblk_log("virtio-blk: no legacy PCI device found (modern-only devices not supported)\n");
        return;
    }
    vblk_log_pci("virtio-blk: found at ", bus, dev, func);

    /* Enable bus mastering + I/O + MEM access. */
    pci_enable_device(bus, dev, func);

    /* BAR0 = I/O base. */
    u32 io_base = pci_read_bar(bus, dev, func, 0);
    if (io_base == 0) {
        vblk_log("virtio-blk: BAR0 invalid\n");
        return;
    }
    g_dev.io_base = io_base;

    /* 1. Reset. */
    vblk_status_set(io_base, 0);
    /* 2-3. ACK | DRIVER. */
    vblk_status_set(io_base, (u8)(VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER));

    /* 4. Feature negotiation: read host features, accept none (simplest
     * configuration that still works for legacy QEMU virtio-blk). */
    (void)inl((u16)(io_base + VIRTIO_REG_HOST_FEATURES));
    outl((u16)(io_base + VIRTIO_REG_GUEST_FEATURES), 0);

    /* 5. DRIVER_OK. */
    vblk_status_set(io_base,
                    (u8)(VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER |
                         VIRTIO_STATUS_DRIVER_OK));

    /* 6. Select queue 0, read size. */
    outl((u16)(io_base + VIRTIO_REG_QUEUE_SELECT), 0);
    u32 qsz = inl((u16)(io_base + VIRTIO_REG_QUEUE_SIZE));
    if (qsz == 0 || qsz > VIRTIO_BLK_QUEUE_SIZE_MAX) {
        vblk_log("virtio-blk: invalid queue size\n");
        vblk_status_set(io_base, VIRTIO_STATUS_FAILED);
        return;
    }
    g_dev.qsz = (u16)qsz;

    /* 7. Allocate the virtqueue with the legacy page-aligned layout. */
    u32 desc_size  = qsz * 16u;
    u32 avail_size = 6u + qsz * 2u;
    u32 used_off   = (desc_size + avail_size + (VIRTIO_PAGE_SIZE - 1)) &
                     ~(VIRTIO_PAGE_SIZE - 1);
    u32 used_size  = 6u + qsz * 8u;
    u32 total      = used_off + used_size;

    u8 *vq = (u8 *)kmalloc_page_aligned(total);
    if (!vq) {
        vblk_log("virtio-blk: out of memory\n");
        vblk_status_set(io_base, VIRTIO_STATUS_FAILED);
        return;
    }
    oc_memset(vq, 0, total);
    g_dev.vq_mem_raw = vq;
    g_dev.desc  = (volatile virtq_desc_t *)vq;
    g_dev.avail = (volatile virtq_avail_t *)(vq + desc_size);
    g_dev.used  = (volatile virtq_used_t *)(vq + used_off);
    g_dev.avail_idx = 0;
    g_dev.last_used_idx = 0;
    g_dev.hdr.type = 0;
    g_dev.hdr.reserved = 0;
    g_dev.hdr.sector = 0;
    g_dev.status = 0xFFu;

    /* 8. Write PFN to activate the queue. */
    outl((u16)(io_base + VIRTIO_REG_QUEUE_PFN), (u32)((u64)vq >> 12));

    /* Read device config (capacity). */
    u32 cap_lo = inl((u16)(io_base + VIRTIO_REG_BLK_CFG + 0));
    u32 cap_hi = inl((u16)(io_base + VIRTIO_REG_BLK_CFG + 4));
    u64 capacity = (u64)cap_lo | ((u64)cap_hi << 32);
    /* P1-26 FIX: if capacity reads as all-ones, the device config is not
     * accessible via legacy I/O. Fall back to a default (0 = unknown). */
    if (capacity == 0xFFFFFFFFFFFFFFFFULL) {
        vblk_log("virtio-blk: WARNING: capacity read failed (legacy config not available?)\n");
        capacity = 0;
    }

    {
        char buf[80]; char n[24];
        oc_strcpy(buf, "virtio-blk: capacity=");
        oc_u64_to_str(capacity, n); oc_strcpy(buf + oc_strlen(buf), n);
        oc_strcpy(buf + oc_strlen(buf), " sectors, qsz=");
        oc_u64_to_str((u64)qsz, n); oc_strcpy(buf + oc_strlen(buf), n);
        oc_strcpy(buf + oc_strlen(buf), "\n");
        vblk_log(buf);
    }

    blk_register_device("vda", BLK_TYPE_VIRTIO, capacity,
                        VIRTIO_BLK_SECTOR_SIZE, &virtio_blk_ops, &g_dev);
}

/* ---- I/O ---- */

/* Submit one 3-descriptor request and poll for completion.
 * type      : VIRTIO_BLK_T_IN (read) or VIRTIO_BLK_T_OUT (write).
 * sector    : starting LBA.
 * data_buf  : identity-mapped buffer of `data_len` bytes (512*count).
 * data_len  : number of bytes to transfer.
 * Returns 0 on success, -1 on device error. */
static int virtio_blk_xfer(virtio_blk_dev_t *d, u32 type, u64 sector,
                           void *data_buf, u32 data_len) {
    /* Build the request header. */
    d->hdr.type     = type;
    d->hdr.reserved = 0;
    d->hdr.sector   = sector;
    d->status       = 0xFFu;

    u64 hdr_phys    = (u64)(uintptr_t)(void *)&d->hdr;
    u64 data_phys   = (u64)(uintptr_t)data_buf;
    u64 status_phys = (u64)(uintptr_t)(void *)&d->status;

    /* Descriptor 0: header (device reads from memory). */
    d->desc[0].addr  = hdr_phys;
    d->desc[0].len   = (u32)sizeof(virtio_blk_outhdr_t);
    d->desc[0].flags = VRING_DESC_F_NEXT;
    d->desc[0].next  = 1;

    /* Descriptor 1: data buffer.
     *  - read  (T_IN):  device writes to memory -> set WRITE flag.
     *  - write (T_OUT): device reads from memory -> no WRITE flag. */
    d->desc[1].addr  = data_phys;
    d->desc[1].len   = data_len;
    d->desc[1].flags = (u16)(VRING_DESC_F_NEXT |
                             (type == VIRTIO_BLK_T_IN ? VRING_DESC_F_WRITE : 0));
    d->desc[1].next  = 2;

    /* Descriptor 2: 1-byte status (device always writes to memory). */
    d->desc[2].addr  = status_phys;
    d->desc[2].len   = 1;
    d->desc[2].flags = VRING_DESC_F_WRITE;
    d->desc[2].next  = 0;

    /* Publish the chain head (descriptor 0) to the avail ring. */
    u16 idx = d->avail_idx;
    /* ring[idx % qsz] = 0 (head of our chain). */
    u16 *ring_base = (u16 *)((u8 *)d->avail + 4);
    ring_base[idx % d->qsz] = 0;

    /* Compiler/CPU ordering: store ring entry before publishing idx. */
    __asm__ volatile("" ::: "memory");
    d->avail->idx = (u16)(idx + 1);
    d->avail_idx  = (u16)(idx + 1);

    /* Notify the queue. outl is serialising on x86 -> full barrier. */
    outl((u16)(d->io_base + VIRTIO_REG_QUEUE_NOTIFY), 0);

    /* Poll the used ring until the device advances its idx. */
    while (d->last_used_idx == d->used->idx) {
        /* spin */
    }
    d->last_used_idx = d->used->idx;

    /* Acknowledge the interrupt (read ISR to clear). */
    (void)inb((u16)(d->io_base + VIRTIO_REG_ISR));

    /* status byte == 0 means success per virtio-blk spec. */
    return (d->status == 0) ? 0 : -1;
}

static int virtio_blk_read(blk_device_t *dev, u64 lba, u32 count, void *buf) {
    virtio_blk_dev_t *d = (virtio_blk_dev_t *)dev->priv;
    if (!d || !d->io_base) return -1;
    if (count == 0) return 0;
    return virtio_blk_xfer(d, VIRTIO_BLK_T_IN, lba, buf,
                           count * VIRTIO_BLK_SECTOR_SIZE);
}

static int virtio_blk_write(blk_device_t *dev, u64 lba, u32 count, const void *buf) {
    virtio_blk_dev_t *d = (virtio_blk_dev_t *)dev->priv;
    if (!d || !d->io_base) return -1;
    if (count == 0) return 0;
    /* Cast away const: the device reads from this memory during a write
     * request, so we never mutate it. */
    return virtio_blk_xfer(d, VIRTIO_BLK_T_OUT, lba, (void *)buf,
                           count * VIRTIO_BLK_SECTOR_SIZE);
}
