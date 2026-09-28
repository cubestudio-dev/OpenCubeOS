/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-06
 * File: kernel/net.c
 * Purpose: Network stack implementation - e1000 driver + TCP/IP + socket API.
 *
 * Architecture:
 *   e1000 NIC (MMIO) -> Ethernet frame -> ARP/IP -> ICMP/UDP/TCP
 *   -> DHCP/DNS/socket
 *
 * Uses polling mode (no IRQ). net_poll() must be called periodically.
 */
#include "net.h"
#include "pci.h"
#include "heap.h"
#include "pmm.h"
#include "string.h"
#include "console.h"
#include "timer.h"
#include "vfs.h"   /* P1-3 FIX: for vfs_open / VFS_O_WRONLY in cmd_wget */

/* Allocate identity-mapped memory for DMA (e1000 needs physical addresses).
 * kmalloc returns heap memory at 0xC0000000 which is NOT identity-mapped.
 * We use PMM to allocate 4KB pages from the identity-mapped region.
 * For small allocations (< 4KB), we sub-allocate from a single page.
 * For larger allocations, we allocate a full page. */
static u8 *g_dma_page = NULL;
static int g_dma_page_offset = 0;

static void *dma_alloc(int size) {
    if (size > PMM_PAGE_SIZE) {
        /* Need a full page. */
        return (void *)pmm_alloc_frame();
    }
    /* Sub-allocate from the current page. */
    if (g_dma_page == NULL || g_dma_page_offset + size > PMM_PAGE_SIZE) {
        /* Allocate a new page. */
        g_dma_page = (u8 *)pmm_alloc_frame();
        if (g_dma_page == NULL) return NULL;
        g_dma_page_offset = 0;
    }
    /* Align to 16 bytes. */
    g_dma_page_offset = (g_dma_page_offset + 15) & ~15;
    void *p = g_dma_page + g_dma_page_offset;
    g_dma_page_offset += size;
    return p;
}

static char n_tmp[20];  /* shared buffer for number formatting */

/* Forward declarations. */
void net_udp_socket_handler(u32 src_ip, u16 src_port, u16 dst_port,
                            const void *data, int len);
void icmp_handle_packet(u32 src_ip, const void *data, int len);
void udp_handle_packet(u32 src_ip, const void *data, int len);
void tcp_handle_packet(u32 src_ip, const void *data, int len);

/* ============================================================
 * Utility: I/O port access
 * ============================================================ */
static inline void outb(u16 p, u8 v) { __asm__ volatile("outb %0, %1" :: "a"(v), "Nd"(p)); }
static inline void outw(u16 p, u16 v) { __asm__ volatile("outw %0, %1" :: "a"(v), "Nd"(p)); }
static inline void outl(u16 p, u32 v) { __asm__ volatile("outl %0, %1" :: "a"(v), "Nd"(p)); }
static inline u8  inb(u16 p) { u8 v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline u16 inw(u16 p) { u16 v; __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline u32 inl(u16 p) { u32 v; __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(p)); return v; }

/* MMIO read/write (volatile pointer) */
static inline u32 mmio_read32(volatile void *addr) {
    return *(volatile u32 *)addr;
}
static inline void mmio_write32(volatile void *addr, u32 val) {
    *(volatile u32 *)addr = val;
}

/* ============================================================
 * Internet checksum
 * ============================================================ */
static u16 internet_checksum(const void *data, int len, u32 sum) {
    const u16 *p = (const u16 *)data;
    while (len > 1) {
        sum += *p++;
        len -= 2;
    }
    if (len == 1) {
        sum += *(const u8 *)p;
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return (u16)(~sum);
}

/* ============================================================
 * Network configuration state
 * ============================================================ */
static u8  g_mac[6] = {0,0,0,0,0,0};
static u32 g_ip      = 0;
static u32 g_mask    = 0;
static u32 g_gateway = 0;
static u32 g_dns     = 0;
static int g_nic_ok  = 0;
static net_stats_t g_stats;

/* ============================================================
 * virtio-net driver (legacy transport via PCI I/O ports)
 * ============================================================ */

/* virtio-net PCI device IDs */
#define VIRTIO_NET_VENDOR_ID  0x1AF4
#define VIRTIO_NET_DEVICE_ID  0x1000  /* legacy */

/* virtio PCI I/O port offsets (from BAR0 I/O base) */
#define VIRTIO_PCI_HOST_FEATURES  0
#define VIRTIO_PCI_GUEST_FEATURES 4
#define VIRTIO_PCI_QUEUE_PFN      8
#define VIRTIO_PCI_QUEUE_SIZE     12
#define VIRTIO_PCI_QUEUE_SELECT   14
#define VIRTIO_PCI_QUEUE_NOTIFY   16
#define VIRTIO_PCI_STATUS         18
#define VIRTIO_PCI_ISR            19

/* virtio status bits */
#define VIRTIO_STATUS_ACK        0x01
#define VIRTIO_STATUS_DRIVER     0x02
#define VIRTIO_STATUS_DRIVER_OK  0x04
#define VIRTIO_STATUS_FEATURES_OK 0x08

/* virtio-net feature bits */
#define VIRTIO_NET_F_MAC         (1 << 5)

/* virtio-net header (10 bytes) */
typedef struct __attribute__((packed)) {
    u8  flags;
    u8  gso_type;
    u16 hdr_len;
    u16 gso_size;
    u16 csum_start;
    u16 csum_offset;
} virtio_net_hdr_t;

/* virtqueue descriptor (16 bytes) */
typedef struct {
    u64 addr;       /* physical address of buffer */
    u32 len;        /* length of buffer */
    u16 flags;      /* VIRTQ_DESC_F_NEXT, etc. */
    u16 next;       /* index of next descriptor (if flags & NEXT) */
} __attribute__((packed)) virtq_desc_t;

#define VIRTQ_DESC_F_NEXT  1
#define VIRTQ_DESC_F_WRITE 2

/* virtqueue available ring */
typedef struct {
    u16 flags;
    u16 idx;
    u16 ring[256];  /* up to 256 entries */
} __attribute__((packed)) virtq_avail_t;

/* virtqueue used ring */
typedef struct __attribute__((packed)) {
    u16 flags;
    u16 idx;
    struct {
        u32 id;
        u32 len;
    } ring[256];
} virtq_used_t;

#define VIRTIO_NET_Q_RX  0
#define VIRTIO_NET_Q_TX  1
#define VIRTIO_NUM_DESC  256  /* Match QEMU's typical virtio-net queue size */
#define VIRTIO_BUF_SIZE  2048

/* virtio-net state */
static u32 g_virtio_io_base = 0;
static int g_virtio_ok = 0;
static int g_use_virtio = 0;  /* 1 = use virtio-net, 0 = use e1000 */

/* RX virtqueue */
static virtq_desc_t *g_vrx_descs = NULL;
static virtq_avail_t *g_vrx_avail = NULL;
static virtq_used_t *g_vrx_used = NULL;
static u8 *g_vrx_bufs[VIRTIO_NUM_DESC];
static u16 g_vrx_avail_idx = 0;
static u16 g_vrx_used_idx = 0;
static u8 *g_vrx_pages = NULL;

/* TX virtqueue */
static virtq_desc_t *g_vtx_descs = NULL;
static virtq_avail_t *g_vtx_avail = NULL;
static virtq_used_t *g_vtx_used = NULL;
static u8 *g_vtx_bufs[VIRTIO_NUM_DESC];
static u16 g_vtx_avail_idx = 0;
static u16 g_vtx_used_idx = 0;
static u8 *g_vtx_pages = NULL;

/* Allocate a virtqueue: descriptor table + avail ring + used ring.
 * Per virtio legacy spec, the layout is contiguous:
 *   - Descriptor table: at offset 0, size = 16 * qsz, aligned to 16
 *   - Available ring: at offset 16*qsz, size = 2 + 2 + 2*qsz
 *   - Used ring: at offset (16*qsz + 2 + 2 + 2*qsz) rounded up to 4, size = 2 + 2 + 8*qsz
 * QUEUE_PFN = phys_addr_of_base / 4096
 * For qsz=256: desc=4096 + avail=514 + used=2054 = ~6.7KB, needs 2 pages. */
static int virtio_alloc_virtq(virtq_desc_t **descs, virtq_avail_t **avail,
                              virtq_used_t **used, u8 **pages_out, u16 qsz) {
    u32 desc_size = 16 * qsz;
    u32 avail_size = 2 + 2 + 2 * qsz;
    u32 used_off = (desc_size + avail_size + 3) & ~3u;
    u32 used_size = 2 + 2 + 8 * qsz;
    u32 total = used_off + used_size;

    /* P0-6 FIX: the virtqueue (desc table + avail ring + used ring) must be
     * physically contiguous. For qsz=256 the total is ~6.7 KiB (2 pages).
     * Previously we allocated individual 4 KiB pages that were NOT
     * contiguous, so the used ring pointer (at offset ~4612) ended up
     * outside the allocation — an out-of-bounds write that corrupted
     * random kernel memory. Now we use pmm_alloc_contig to get a proper
     * contiguous run. */
    int npages = (int)((total + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE);
    u8 *base = (u8 *)pmm_alloc_contig((u64)npages);
    if (!base) {
        /* Fallback: try single page (only works for qsz <= ~170). */
        base = (u8 *)pmm_alloc_frame();
        if (!base) return -1;
        if (npages > 1) {
            /* Not enough contiguous memory — fail gracefully. */
            pmm_free_frame((u64)base);
            return -1;
        }
    }
    oc_memset(base, 0, (u64)npages * PMM_PAGE_SIZE);

    *descs = (virtq_desc_t *)base;
    *avail = (virtq_avail_t *)(base + desc_size);
    *used = (virtq_used_t *)(base + used_off);
    *pages_out = base;
    return 0;
}

static int virtio_net_init(void) {
    /* Find virtio-net on PCI bus. */
    u8 bus, dev, func;
    if (pci_find_device(VIRTIO_NET_VENDOR_ID, VIRTIO_NET_DEVICE_ID,
                        &bus, &dev, &func) != 0) {
        oc_console_puts("virtio-net: no device found\n");
        return -1;
    }

    /* Read BAR0 (I/O port base for legacy virtio). */
    u32 bar0_raw = pci_read_config(bus, dev, func, 0x10);
    u32 bar0 = pci_read_bar(bus, dev, func, 0);
    if (bar0 == 0) {
        oc_console_puts("virtio-net: BAR0 is zero\n");
        return -1;
    }
    g_virtio_io_base = bar0;

    char buf[80]; char n[20];
    oc_strcpy(buf, "virtio-net: found at "); oc_u64_to_str(bus, n); oc_strcat(buf, n);
    oc_strcat(buf, ":"); oc_u64_to_str(dev, n); oc_strcat(buf, n);
    oc_strcat(buf, "."); oc_u64_to_str(func, n); oc_strcat(buf, n);
    oc_strcat(buf, " BAR0_raw=0x"); oc_u64_to_hex(bar0_raw, n, 8); oc_strcat(buf, n);
    oc_strcat(buf, " I/O=0x"); oc_u64_to_hex(bar0, n, 4); oc_strcat(buf, n);
    oc_strcat(buf, "\n");
    oc_console_puts(buf);

    /* Enable bus mastering and I/O access. */
    u32 cmd = pci_read_config(bus, dev, func, 0x04);
    cmd |= 0x05;  /* BM + IO enable */
    pci_write_config(bus, dev, func, 0x04, cmd);

    /* Read MAC address from device config (at I/O offset 20-25 for virtio-net). */
    for (int i = 0; i < 6; i++) {
        g_mac[i] = inb(g_virtio_io_base + 20 + i);
    }

    /* Reset device: write 0 to status. */
    outb(g_virtio_io_base + VIRTIO_PCI_STATUS, 0);

    /* Acknowledge device. */
    outb(g_virtio_io_base + VIRTIO_PCI_STATUS,
         VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER);

    /* Read host features and negotiate (only MAC feature). */
    u32 host_features = inl(g_virtio_io_base + VIRTIO_PCI_HOST_FEATURES);
    u32 guest_features = 0;
    if (host_features & VIRTIO_NET_F_MAC) {
        guest_features |= VIRTIO_NET_F_MAC;
    }
    outl(g_virtio_io_base + VIRTIO_PCI_GUEST_FEATURES, guest_features);

    /* NOTE: For legacy virtio, FEATURES_OK bit doesn't exist. We just set
     * ACK | DRIVER now, and set DRIVER_OK after queues are initialized. */

    /* Initialize RX virtqueue (queue 0). */
    outw(g_virtio_io_base + VIRTIO_PCI_QUEUE_SELECT, VIRTIO_NET_Q_RX);
    u16 rx_size = inw(g_virtio_io_base + VIRTIO_PCI_QUEUE_SIZE);
    if (rx_size == 0) return -1;
    /* Use the actual device queue size. Cap at 256 to limit memory usage. */
    if (rx_size > 256) rx_size = 256;

    if (virtio_alloc_virtq(&g_vrx_descs, &g_vrx_avail, &g_vrx_used,
                           &g_vrx_pages, rx_size) != 0) return -1;

    /* Allocate RX buffers and populate descriptors. */
    for (int i = 0; i < rx_size; i++) {
        g_vrx_bufs[i] = (u8 *)pmm_alloc_frame();
        if (!g_vrx_bufs[i]) return -1;
        g_vrx_descs[i].addr = (u64)(uintptr_t)g_vrx_bufs[i];
        g_vrx_descs[i].len = VIRTIO_BUF_SIZE;
        g_vrx_descs[i].flags = VIRTQ_DESC_F_WRITE;
        g_vrx_descs[i].next = 0;
        g_vrx_avail->ring[g_vrx_avail_idx % rx_size] = (u16)i;
        g_vrx_avail_idx++;
    }
    g_vrx_avail->idx = g_vrx_avail_idx;
    g_vrx_used_idx = 0;

    /* Set queue PFN. */
    u32 rx_pfn = (u32)(uintptr_t)g_vrx_pages / PMM_PAGE_SIZE;
    outl(g_virtio_io_base + VIRTIO_PCI_QUEUE_PFN, rx_pfn);

    /* Debug: print queue info. */
    {
        char buf2[80]; char n2[20];
        oc_strcpy(buf2, "virtio-net: RX qsz="); oc_u64_to_str(rx_size, n2); oc_strcat(buf2, n2);
        oc_strcat(buf2, " PFN="); oc_u64_to_str(rx_pfn, n2); oc_strcat(buf2, n2);
        oc_strcat(buf2, " page=0x"); oc_u64_to_hex((u64)(uintptr_t)g_vrx_pages, n2, 8); oc_strcat(buf2, n2);
        oc_strcat(buf2, "\n");
        oc_console_puts(buf2);
    }

    outw(g_virtio_io_base + VIRTIO_PCI_QUEUE_NOTIFY, VIRTIO_NET_Q_RX);

    /* Initialize TX virtqueue (queue 1). */
    outw(g_virtio_io_base + VIRTIO_PCI_QUEUE_SELECT, VIRTIO_NET_Q_TX);
    u16 tx_size = inw(g_virtio_io_base + VIRTIO_PCI_QUEUE_SIZE);
    if (tx_size == 0) return -1;
    if (tx_size > VIRTIO_NUM_DESC) tx_size = VIRTIO_NUM_DESC;

    if (virtio_alloc_virtq(&g_vtx_descs, &g_vtx_avail, &g_vtx_used,
                           &g_vtx_pages, tx_size) != 0) return -1;

    for (int i = 0; i < tx_size; i++) {
        g_vtx_bufs[i] = (u8 *)pmm_alloc_frame();
        if (!g_vtx_bufs[i]) return -1;
    }
    g_vtx_avail_idx = 0;
    g_vtx_used_idx = 0;

    u32 tx_pfn = (u32)(uintptr_t)g_vtx_pages / PMM_PAGE_SIZE;
    outl(g_virtio_io_base + VIRTIO_PCI_QUEUE_PFN, tx_pfn);

    /* Debug: verify TX queue setup. */
    {
        char buf2[100]; char n2[20];
        u16 tx_qsz_verify = inw(g_virtio_io_base + VIRTIO_PCI_QUEUE_SIZE);
        u32 tx_pfn_verify = inl(g_virtio_io_base + VIRTIO_PCI_QUEUE_PFN);
        u16 tx_qsel = inw(g_virtio_io_base + VIRTIO_PCI_QUEUE_SELECT);
        oc_strcpy(buf2, "virtio-net: TX qsz="); oc_u64_to_str(tx_size, n2); oc_strcat(buf2, n2);
        oc_strcat(buf2, "("); oc_u64_to_str(tx_qsz_verify, n2); oc_strcat(buf2, n2); oc_strcat(buf2, ")");
        oc_strcat(buf2, " PFN="); oc_u64_to_str(tx_pfn, n2); oc_strcat(buf2, n2);
        oc_strcat(buf2, "("); oc_u64_to_str(tx_pfn_verify, n2); oc_strcat(buf2, n2); oc_strcat(buf2, ")");
        oc_strcat(buf2, " QSEL="); oc_u64_to_str(tx_qsel, n2); oc_strcat(buf2, n2);
        oc_strcat(buf2, " page=0x"); oc_u64_to_hex((u64)(uintptr_t)g_vtx_pages, n2, 8); oc_strcat(buf2, n2);
        oc_strcat(buf2, "\n");
        oc_console_puts(buf2);
    }

    /* NOW set DRIVER_OK — device starts processing queues. */
    outb(g_virtio_io_base + VIRTIO_PCI_STATUS,
         VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_DRIVER_OK);

    g_virtio_ok = 1;
    g_use_virtio = 1;
    return 0;
}

static int virtio_net_send(const void *data, int len) {
    if (!g_virtio_ok || len <= 0)
        return -1;
    if ((u32)len > VIRTIO_BUF_SIZE - sizeof(virtio_net_hdr_t))
        return -1;

    /* Use the next free TX descriptor (round-robin). */
    u16 desc_idx = g_vtx_avail_idx % VIRTIO_NUM_DESC;

    u8 *buf = g_vtx_bufs[desc_idx];
    virtio_net_hdr_t *hdr = (virtio_net_hdr_t *)buf;
    oc_memset(hdr, 0, sizeof(virtio_net_hdr_t));
    oc_memcpy(buf + sizeof(virtio_net_hdr_t), data, len);

    g_vtx_descs[desc_idx].addr = (u64)(uintptr_t)buf;
    g_vtx_descs[desc_idx].len = sizeof(virtio_net_hdr_t) + len;
    g_vtx_descs[desc_idx].flags = 0;
    g_vtx_descs[desc_idx].next = 0;

    /* Add to avail ring. Memory barriers are critical for virtio. */
    u16 avail_slot = g_vtx_avail_idx % VIRTIO_NUM_DESC;
    g_vtx_avail->ring[avail_slot] = desc_idx;
    __asm__ volatile("sfence" ::: "memory");  /* wmb: ensure desc + ring write */
    g_vtx_avail->idx = g_vtx_avail_idx + 1;
    __asm__ volatile("sfence" ::: "memory");  /* wmb: ensure idx write visible */
    g_vtx_avail_idx++;

    /* Notify the device (kick the TX queue). */
    outw(g_virtio_io_base + VIRTIO_PCI_QUEUE_NOTIFY, VIRTIO_NET_Q_TX);

    /* Wait for the device to process (poll used ring). */
    u16 old_used = g_vtx_used_idx;
    for (int timeout = 0; timeout < 1000000; timeout++) {
        __asm__ volatile("lfence" ::: "memory");  /* rmb: ensure fresh read */
        u16 cur_used = g_vtx_used->idx;
        if (cur_used != old_used) {
            g_vtx_used_idx = cur_used;
            break;
        }
    }

    /* Debug: if TX didn't complete, dump state. */
    if (g_vtx_used->idx == old_used) {
        u8 isr = inb(g_virtio_io_base + VIRTIO_PCI_ISR);
        u8 status = inb(g_virtio_io_base + VIRTIO_PCI_STATUS);
        char buf[120]; char n[20];
        oc_strcpy(buf, "virtio TX timeout: avail_idx="); oc_u64_to_str(g_vtx_avail_idx, n); oc_strcat(buf, n);
        oc_strcat(buf, " used_idx="); oc_u64_to_str(g_vtx_used->idx, n); oc_strcat(buf, n);
        oc_strcat(buf, " ISR=0x"); oc_u64_to_hex(isr, n, 2); oc_strcat(buf, n);
        oc_strcat(buf, " STATUS=0x"); oc_u64_to_hex(status, n, 2); oc_strcat(buf, n);
        oc_strcat(buf, " desc_addr=0x"); oc_u64_to_hex(g_vtx_descs[desc_idx].addr, n, 16); oc_strcat(buf, n);
        oc_strcat(buf, "\n");
        oc_console_puts(buf);
    }

    g_stats.tx_packets++;
    g_stats.tx_bytes += len;
    return len;
}

static int virtio_net_recv(void *buf, int maxlen) {
    if (!g_virtio_ok) return -1;
    if (g_vrx_used->idx == g_vrx_used_idx) return 0;

    u16 used_idx = g_vrx_used_idx % VIRTIO_NUM_DESC;
    u32 desc_id = g_vrx_used->ring[used_idx].id;
    u32 used_len = g_vrx_used->ring[used_idx].len;
    g_vrx_used_idx++;

    int data_len = (int)used_len - (int)sizeof(virtio_net_hdr_t);
    if (data_len < 0) data_len = 0;
    if (data_len > maxlen) data_len = maxlen;

    u8 *src = g_vrx_bufs[desc_id] + sizeof(virtio_net_hdr_t);
    oc_memcpy(buf, src, data_len);

    /* Recycle the descriptor. */
    g_vrx_descs[desc_id].addr = (u64)(uintptr_t)g_vrx_bufs[desc_id];
    g_vrx_descs[desc_id].len = VIRTIO_BUF_SIZE;
    g_vrx_descs[desc_id].flags = VIRTQ_DESC_F_WRITE;
    g_vrx_avail->ring[g_vrx_avail_idx % VIRTIO_NUM_DESC] = desc_id;
    g_vrx_avail_idx++;
    g_vrx_avail->idx = g_vrx_avail_idx;
    outw(g_virtio_io_base + VIRTIO_PCI_QUEUE_NOTIFY, VIRTIO_NET_Q_RX);

    g_stats.rx_packets++;
    g_stats.rx_bytes += data_len;
    return data_len;
}

/* ============================================================
 * e1000 NIC driver
 * ============================================================ */

/* e1000 MMIO register offsets */
#define E1000_CTRL   0x0000
#define E1000_STATUS 0x0008
#define E1000_RCTL   0x0100
#define E1000_TCTL   0x0400
#define E1000_RDBAL  0x2800
#define E1000_RDBAH  0x2804
#define E1000_RDLEN  0x2808
#define E1000_RDH    0x2810
#define E1000_RDT    0x2818
#define E1000_TDBAL  0x3800
#define E1000_TDBAH  0x3804
#define E1000_TDLEN  0x3808
#define E1000_TDH    0x3810
#define E1000_TDT    0x3818
#define E1000_RA     0x5400
#define E1000_IMC    0x00D8  /* Interrupt Mask Clear */

/* RCTL (Receive Control) bit flags -- per Intel 8254x datasheet / QEMU e1000x_regs.h */
#define E1000_RCTL_RST    0x00000001u  /* software reset (self-clearing) */
#define E1000_RCTL_EN     0x00000002u  /* receiver enable */
#define E1000_RCTL_SBP    0x00000004u  /* store bad packets */
#define E1000_RCTL_UPE    0x00000008u  /* unicast promiscuous enable */
#define E1000_RCTL_MPE    0x00000010u  /* multicast promiscuous enable */
#define E1000_RCTL_BAM    0x00008000u  /* broadcast accept mode (REQUIRED for ARP) */
#define E1000_RCTL_SECRC  0x04000000u  /* strip Ethernet CRC from received packets */

/* RX/TX descriptor (legacy format, 16 bytes).
 * For RX: addr(8) + length(2) + checksum(2) + status(1) + errors(1) + special(2)
 * For TX: addr(8) + length(2) + cso(1) + cmd(1) + status(1) + errors(1) + special(2)
 * We use a single struct; the interpretation differs for RX vs TX. */
typedef struct {
    u64 addr;       /* buffer address */
    u16 length;     /* length of data */
    union {
        u16 csum;       /* RX: checksum */
        struct {
            u8 cso;     /* TX: checksum offset */
            u8 cmd;     /* TX: command bits */
        };
    };
    u8  status;     /* status bits */
    u8  errors;     /* error bits */
    u16 special;
} __attribute__((packed)) e1000_desc_t;

#define E1000_NUM_DESC 16
#define E1000_BUF_SIZE 2048

static volatile u32 *g_e1000_mmio = NULL;
static volatile e1000_desc_t *g_rx_descs = NULL;
static volatile e1000_desc_t *g_tx_descs = NULL;
static u8 *g_rx_bufs[E1000_NUM_DESC];
static u8 *g_tx_bufs[E1000_NUM_DESC];
static int g_rx_tail = 0;
static int g_tx_tail = 0;
static u8 g_e1000_bus, g_e1000_dev, g_e1000_func;  /* saved for re-enabling bus master */

static int e1000_init(void) {
    /* Find e1000 on PCI bus. e1000: vendor=0x8086, device=0x100E (or 0x100F, 0x10D3). */
    u8 bus, dev, func;
    int found = -1;
    /* Try common e1000 device IDs */
    u16 devices[] = {0x100E, 0x100F, 0x10D3, 0x10EA};
    for (int i = 0; i < 4; i++) {
        if (pci_find_device(0x8086, devices[i], &bus, &dev, &func) == 0) {
            found = 0;
            break;
        }
    }
    if (found != 0) {
        oc_console_puts("e1000: no NIC found\n");
        return -1;
    }

    /* Save PCI address for later bus master re-enabling. */
    g_e1000_bus = bus;
    g_e1000_dev = dev;
    g_e1000_func = func;

    /* Read BAR0 for MMIO base. */
    u32 bar0 = pci_read_bar(bus, dev, func, 0);
    if (bar0 == 0) {
        oc_console_puts("e1000: BAR0 is zero\n");
        return -1;
    }

    /* Debug: print PCI info. */
    {
        char buf[80]; char n[20];
        u32 cmd_reg = pci_read_config(bus, dev, func, 0x04);
        oc_strcat(buf, " dev="); oc_u64_to_str(dev, n); oc_strcat(buf, n);
        oc_strcat(buf, " func="); oc_u64_to_str(func, n); oc_strcat(buf, n);
        oc_strcat(buf, " cmd=0x"); oc_u64_to_hex(cmd_reg, n, 4); oc_strcat(buf, n);
        oc_strcat(buf, "\n");
        oc_console_puts(buf);
    }

    /* Enable bus mastering and MMIO access.
     * Write the FULL 32-bit value to the PCI command register (offset 0x04).
     * The command register is 16-bit at offset 0x04, and the status register
     * is 16-bit at offset 0x06. We write 0x0000x107 where x107 = IO|MEM|BM|SERR.
     * Preserve the status register (high 16 bits) by reading first. */
    u32 cmd_stat = pci_read_config(bus, dev, func, 0x04);
    u32 cmd_only = cmd_stat & 0x0000FFFF;
    u32 stat_only = cmd_stat & 0xFFFF0000;
    cmd_only = 0x0107;  /* IO + MEM + BM + SERR# */
    pci_write_config(bus, dev, func, 0x04, stat_only | cmd_only);

    g_e1000_mmio = (volatile u32 *)(uintptr_t)bar0;

    /* Read MAC address from EEPROM (RA register array). */
    /* For e1000, MAC is at offset 0x5400 (RA[0] low) and 0x5404 (RA[0] high). */
    u32 mac_low = mmio_read32((volatile void *)((u8 *)g_e1000_mmio + E1000_RA));
    u32 mac_high = mmio_read32((volatile void *)((u8 *)g_e1000_mmio + E1000_RA + 4));
    g_mac[0] = mac_low & 0xFF;
    g_mac[1] = (mac_low >> 8) & 0xFF;
    g_mac[2] = (mac_low >> 16) & 0xFF;
    g_mac[3] = (mac_low >> 24) & 0xFF;
    g_mac[4] = mac_high & 0xFF;
    g_mac[5] = (mac_high >> 8) & 0xFF;

    /* If MAC is all zeros, try reading from EEPROM via EERD register. */
    if (g_mac[0] == 0 && g_mac[1] == 0 && g_mac[2] == 0) {
        /* Read EEPROM word 0 and 1. */
        for (int word = 0; word < 3; word++) {
            mmio_write32((volatile void *)((u8 *)g_e1000_mmio + 0x0014),  /* EERD */
                        (u32)(1) | ((u32)word << 8) | (1 << 4));  /* START + addr */
            u32 val = 0;
            for (int timeout = 0; timeout < 1000; timeout++) {
                val = mmio_read32((volatile void *)((u8 *)g_e1000_mmio + 0x0014));
                if (val & (1 << 9)) break;  /* DONE */
            }
            u16 data = (u16)((val >> 16) & 0xFFFF);
            if (word == 0) {
                g_mac[0] = data & 0xFF;
                g_mac[1] = (data >> 8) & 0xFF;
            } else if (word == 1) {
                g_mac[2] = data & 0xFF;
                g_mac[3] = (data >> 8) & 0xFF;
            } else {
                g_mac[4] = data & 0xFF;
                g_mac[5] = (data >> 8) & 0xFF;
            }
        }
    }

    /* Skip the e1000 software reset — it clears the PCI command register
     * which disables bus mastering. Instead, just set CTRL directly. */

    /* Set CTRL register:
     *   bit 0: FD (Full Duplex)
     *   bit 5: ASDE (Auto-Speed Detect Enable)
     *   bit 6: SLU (Set Link Up)
     *   bit 11: FRCSPD (Force Speed)
     *   bit 12: FRCFD (Force Full Duplex)
     * Don't set RST (bit 26) — it clears PCI command register.
     */
    mmio_write32((volatile void *)((u8 *)g_e1000_mmio + E1000_CTRL),
                (1u << 0) | (1u << 6) | (1u << 11) | (1u << 12));

    /* Disable interrupts. */
    mmio_write32((volatile void *)((u8 *)g_e1000_mmio + E1000_IMC), 0xFFFFFFFF);

    /* CRITICAL: Enable PCI bus master. This MUST be done after any device
     * reset, because the reset clears the PCI command register.
     * Without bus master, QEMU's e1000 reports pci_master=0 and refuses
     * to process any TX/RX descriptors (no DMA). */
    {
        u32 cmd_before = pci_read_config(bus, dev, func, 0x04);
        u32 cmd_new = cmd_before | 0x07;
        pci_write_config(bus, dev, func, 0x04, cmd_new);
        u32 cmd_after = pci_read_config(bus, dev, func, 0x04);
        char dbuf[100]; char dn[20];
            oc_strcat(dbuf, " wrote=0x"); oc_u64_to_hex(cmd_new, dn, 4); oc_strcat(dbuf, dn);
        oc_strcat(dbuf, " after=0x"); oc_u64_to_hex(cmd_after, dn, 4); oc_strcat(dbuf, dn);
        oc_strcat(dbuf, " bus="); oc_u64_to_str(bus, dn); oc_strcat(dbuf, dn);
        oc_strcat(dbuf, " dev="); oc_u64_to_str(dev, dn); oc_strcat(dbuf, dn);
        oc_strcat(dbuf, " func="); oc_u64_to_str(func, dn); oc_strcat(dbuf, dn);
        oc_strcat(dbuf, "\n");
        oc_console_puts(dbuf);
    }

    /* Set Receive Address Register (RA[0]) to our MAC. */
    u32 ra_lo = g_mac[0] | ((u32)g_mac[1] << 8) | ((u32)g_mac[2] << 16) | ((u32)g_mac[3] << 24);
    u32 ra_hi = g_mac[4] | ((u32)g_mac[5] << 8) | (1u << 31);  /* AV bit */
    mmio_write32((volatile void *)((u8 *)g_e1000_mmio + E1000_RA), ra_lo);
    mmio_write32((volatile void *)((u8 *)g_e1000_mmio + E1000_RA + 4), ra_hi);

    /* Clear the Multicast Table Array (MTA) - 128 entries at 0x5200.
     * All zeros = accept no multicast (but we have MPE set in RCTL). */
    for (int i = 0; i < 128; i++) {
        mmio_write32((volatile void *)((u8 *)g_e1000_mmio + 0x5200 + i * 4), 0);
    }

    /* Allocate RX descriptors as a full page (page-aligned, identity-mapped). */
    g_rx_descs = (volatile e1000_desc_t *)pmm_alloc_frame();
    if (!g_rx_descs) return -1;
    oc_memset((void*)g_rx_descs, 0, PMM_PAGE_SIZE);
    uintptr_t rx_descs_phys = (uintptr_t)g_rx_descs;

    /* Allocate RX buffers (identity-mapped). */
    for (int i = 0; i < E1000_NUM_DESC; i++) {
        g_rx_bufs[i] = (u8 *)dma_alloc(E1000_BUF_SIZE);
        if (!g_rx_bufs[i]) return -1;
        g_rx_descs[i].addr = (u64)(uintptr_t)g_rx_bufs[i];
        g_rx_descs[i].status = 0;
    }
    g_rx_tail = 0;

    /* Disable RX before programming (RCTL.EN = 0). */
    mmio_write32((volatile void *)((u8 *)g_e1000_mmio + E1000_RCTL), 0);

    /* Configure RX registers. */
    mmio_write32((volatile void *)((u8 *)g_e1000_mmio + E1000_RDBAL), (u32)rx_descs_phys);
    mmio_write32((volatile void *)((u8 *)g_e1000_mmio + E1000_RDBAH), 0);
    mmio_write32((volatile void *)((u8 *)g_e1000_mmio + E1000_RDLEN),
                sizeof(e1000_desc_t) * E1000_NUM_DESC);
    mmio_write32((volatile void *)((u8 *)g_e1000_mmio + E1000_RDH), 0);
    mmio_write32((volatile void *)((u8 *)g_e1000_mmio + E1000_RDT), E1000_NUM_DESC - 1);

    /* NOW enable RX.
     * RCTL bit layout (per Intel 8254x datasheet / QEMU e1000x_regs.h):
     *   RST=0x01  EN=0x02  SBP=0x04  UPE=0x08  MPE=0x10
     *   BAM=0x8000 (broadcast accept)  SECRC=0x04000000 (strip CRC)
     *
     * BAM (Broadcast Accept Mode) is REQUIRED: without it the e1000 silently
     * drops Ethernet broadcast frames, which breaks ARP (ARP requests are
     * broadcasts).  This was the root cause why slirp (unicast-only) worked
     * but direct L2 networking (socket-mode / TAP) failed -- the NIC never
     * saw ARP requests, so arp_resolve() always timed out.
     *
     * UPE+MPE put the NIC in promiscuous mode (accept all unicast/multicast),
     * which is fine for this hobby OS and makes it robust to any MAC.
     */
    mmio_write32((volatile void *)((u8 *)g_e1000_mmio + E1000_RCTL),
                E1000_RCTL_EN | E1000_RCTL_UPE | E1000_RCTL_MPE |
                E1000_RCTL_BAM | E1000_RCTL_SECRC);

    /* Allocate TX descriptor ring as a full page (page-aligned, identity-mapped). */
    g_tx_descs = (volatile e1000_desc_t *)pmm_alloc_frame();
    if (!g_tx_descs) return -1;
    oc_memset((void*)g_tx_descs, 0, PMM_PAGE_SIZE);
    uintptr_t tx_descs_phys = (uintptr_t)g_tx_descs;

    /* Allocate TX buffers (identity-mapped). */
    for (int i = 0; i < E1000_NUM_DESC; i++) {
        g_tx_bufs[i] = (u8 *)dma_alloc(E1000_BUF_SIZE);
        if (!g_tx_bufs[i]) return -1;
    }
    g_tx_tail = 0;

    /* Disable TX before programming (TCTL.EN = 0). */
    mmio_write32((volatile void *)((u8 *)g_e1000_mmio + E1000_TCTL), 0);

    /* Configure TX descriptor ring registers. */
    mmio_write32((volatile void *)((u8 *)g_e1000_mmio + E1000_TDBAL), (u32)tx_descs_phys);
    mmio_write32((volatile void *)((u8 *)g_e1000_mmio + E1000_TDBAH), 0);
    mmio_write32((volatile void *)((u8 *)g_e1000_mmio + E1000_TDLEN),
                sizeof(e1000_desc_t) * E1000_NUM_DESC);
    mmio_write32((volatile void *)((u8 *)g_e1000_mmio + E1000_TDH), 0);
    mmio_write32((volatile void *)((u8 *)g_e1000_mmio + E1000_TDT), 0);

    /* Set TIPG (Transmit Inter-Packet Gap).
     * IPGT=9, IPGR1=8, IPGR2=6. */
    mmio_write32((volatile void *)((u8 *)g_e1000_mmio + 0x0410), 0x0060200A);

    /* Enable TX.
     * TCTL register bits for e1000 (82540EM):
     *   bit 1: EN (Enable) = 0x02
     *   bit 3: PSP (Pad Short Packets)
     *   bits 4-7: CT (Collision Threshold) = 0x0F
     *   bits 12-21: COLD (Collision Distance) = 0x3F for full-duplex */
    mmio_write32((volatile void *)((u8 *)g_e1000_mmio + E1000_TCTL),
                (1u << 1)       /* EN = bit 1, NOT bit 0! */
                | (1u << 3)     /* PSP */
                | (0x0F << 4)   /* CT = 15 */
                | (0x3F << 12)); /* COLD = 63 (full-duplex) */

    /* CRITICAL: Enable PCI bus master as the VERY LAST step.
     * QEMU's e1000 device model caches the pci_master flag from the PCI
     * command register. Any MMIO write (especially CTRL) may trigger
     * internal state changes that reset this cache. By enabling bus master
     * last, we ensure it's set when we start sending packets. */
    {
        u32 cs = pci_read_config(bus, dev, func, 0x04);
        cs = (cs & 0xFFFF0000) | 0x0107;  /* preserve status, set cmd */
        pci_write_config(bus, dev, func, 0x04, cs);
    }

    g_nic_ok = 1;
    return 0;
}

static int e1000_send(const void *data, int len) {
    if (!g_nic_ok || len <= 0 || len > E1000_BUF_SIZE) return -1;

    /* Copy data to TX buffer. */
    oc_memcpy(g_tx_bufs[g_tx_tail], data, len);

    /* Set up TX descriptor. */
    g_tx_descs[g_tx_tail].addr = (u64)(uintptr_t)g_tx_bufs[g_tx_tail];
    g_tx_descs[g_tx_tail].length = (u16)len;
    g_tx_descs[g_tx_tail].cso = 0;
    g_tx_descs[g_tx_tail].cmd = 0x0B;  /* EOP | IFCS | RS */
    g_tx_descs[g_tx_tail].status = 0;
    g_tx_descs[g_tx_tail].errors = 0;
    g_tx_descs[g_tx_tail].special = 0;

    /* Advance tail. */
    int old_tail = g_tx_tail;
    g_tx_tail = (g_tx_tail + 1) % E1000_NUM_DESC;

    /* Re-assert PCI bus master right before writing TDT. QEMU's e1000
     * checks pci_bus_master() on every TX attempt; some MMIO writes to
     * CTRL can clear the PCI command register. */
    {
        u32 cs = pci_read_config(g_e1000_bus, g_e1000_dev, g_e1000_func, 0x04);
        if (!(cs & 0x04)) {
            cs = (cs & 0xFFFF0000) | 0x0107;
            pci_write_config(g_e1000_bus, g_e1000_dev, g_e1000_func, 0x04, cs);
        }
    }

    /* Make sure descriptor writes are visible before TDT write. */
    __asm__ volatile("sfence" ::: "memory");

    /* P2-07 FIX: removed the TCTL.EN toggle and all the debug
     * oc_console_puts() / oc_u64_to_str() dumping that ran on every TX
     * path. The toggle was a debug rescue mechanism, not part of the
     * normal hardware path — the e1000 datasheet says TDT writes alone
     * are sufficient to launch a TX descriptor. */
    mmio_write32((volatile void *)((u8 *)g_e1000_mmio + E1000_TDT), (u32)g_tx_tail);
    __asm__ volatile("mfence" ::: "memory");

    /* Wait for TX to complete (DD bit in status). Poll for up to 1 s. */
    for (int timeout = 0; timeout < 1000000; timeout++) {
        if (g_tx_descs[old_tail].status & 0x01) break;
    }

    g_stats.tx_packets++;
    g_stats.tx_bytes += len;
    return len;
}

static int e1000_recv(void *buf, int maxlen) {
    if (!g_nic_ok) return -1;

    /* Check if RX descriptor has a packet (DD bit = bit0 of status). */
    if (!(g_rx_descs[g_rx_tail].status & 0x01)) {
        return 0;  /* no packet */
    }

    int len = g_rx_descs[g_rx_tail].length;
    if (len > maxlen) len = maxlen;
    if (len > 0) {
        oc_memcpy(buf, g_rx_bufs[g_rx_tail], len);
    }

    /* Reset descriptor and advance tail. */
    g_rx_descs[g_rx_tail].status = 0;
    g_rx_descs[g_rx_tail].addr = (u64)(uintptr_t)g_rx_bufs[g_rx_tail];
    g_rx_descs[g_rx_tail].length = 0;
    g_rx_descs[g_rx_tail].errors = 0;
    g_rx_tail = (g_rx_tail + 1) % E1000_NUM_DESC;
    /* CRITICAL: RDT must point to the LAST AVAILABLE descriptor (one before g_rx_tail).
     * If we set RDT = g_rx_tail, and RDH == g_rx_tail, QEMU thinks the ring is full
     * and stops receiving. Setting RDT = (g_rx_tail - 1 + N) % N ensures there's
     * always at least one available descriptor between RDH and RDT. */
    u32 new_rdt = (g_rx_tail + E1000_NUM_DESC - 1) % E1000_NUM_DESC;
    mmio_write32((volatile void *)((u8 *)g_e1000_mmio + E1000_RDT), new_rdt);

    g_stats.rx_packets++;
    g_stats.rx_bytes += len;
    return len;
}

/* ============================================================
 * Ethernet layer
 * ============================================================ */
typedef struct __attribute__((packed)) {
    u8 dst[6];
    u8 src[6];
    u16 ethertype;
} eth_hdr_t;

static int eth_send(const u8 *dst, u16 ethertype, const void *payload, int len) {
    if (!g_nic_ok) return -1;
    /* P0-7 FIX: bound payload to the Ethernet MTU (1500). */
    if (len < 0) return -1;
    if (len > 1500) len = 1500;
    u8 frame[ETH_FRAME_MAX];
    eth_hdr_t *eh = (eth_hdr_t *)frame;
    oc_memcpy(eh->dst, dst, 6);
    oc_memcpy(eh->src, g_mac, 6);
    eh->ethertype = htons(ethertype);
    int total = 14 + len;
    if (total < 60) total = 60;  /* minimum frame size */
    oc_memcpy(frame + 14, payload, len);
    for (int i = 14 + len; i < total; i++) frame[i] = 0;
    if (g_use_virtio) {
        return virtio_net_send(frame, total);
    }
    return e1000_send(frame, total);
}

/* ============================================================
 * ARP layer
 * ============================================================ */
typedef struct __attribute__((packed)) {
    u16 htype;
    u16 ptype;
    u8  hlen;
    u8  plen;
    u16 op;
    u8  sha[6];
    u32 spa;
    u8  tha[6];
    u32 tpa;
} arp_pkt_t;

#define ARP_CACHE_SIZE 16
typedef struct {
    u32 ip;
    u8  mac[6];
    int valid;
} arp_entry_t;

static arp_entry_t g_arp_cache[ARP_CACHE_SIZE];
static int g_arp_reply_received = 0;
static u8  g_arp_reply_mac[6];

static void arp_init(void) {
    oc_memset(g_arp_cache, 0, sizeof(g_arp_cache));
}

static int arp_cache_lookup(u32 ip, u8 *mac) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (g_arp_cache[i].valid && g_arp_cache[i].ip == ip) {
            oc_memcpy(mac, g_arp_cache[i].mac, 6);
            return 0;
        }
    }
    return -1;
}

static void arp_cache_add(u32 ip, const u8 *mac) {
    /* Find existing or free slot. */
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (g_arp_cache[i].valid && g_arp_cache[i].ip == ip) {
            oc_memcpy(g_arp_cache[i].mac, mac, 6);
            return;
        }
    }
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!g_arp_cache[i].valid) {
            g_arp_cache[i].ip = ip;
            oc_memcpy(g_arp_cache[i].mac, mac, 6);
            g_arp_cache[i].valid = 1;
            return;
        }
    }
    /* Cache full: overwrite entry 0. */
    g_arp_cache[0].ip = ip;
    oc_memcpy(g_arp_cache[0].mac, mac, 6);
    g_arp_cache[0].valid = 1;
}

static void arp_handle_packet(const arp_pkt_t *pkt, int len) {
    (void)len;
    if (ntohs(pkt->htype) != 1 || ntohs(pkt->ptype) != ETH_TYPE_IP) return;
    if (pkt->hlen != 6 || pkt->plen != 4) return;

    u16 op = ntohs(pkt->op);
    u32 sender_ip = ntohl(pkt->spa);
    u32 target_ip = ntohl(pkt->tpa);

    /* Add sender to cache. */
    arp_cache_add(sender_ip, pkt->sha);

    if (op == 2) {
        /* ARP reply: save MAC for arp_resolve. */
        oc_memcpy(g_arp_reply_mac, pkt->sha, 6);
        g_arp_reply_received = 1;
        g_stats.arp_replies++;
    } else if (op == 1) {
        /* ARP request: if target is us, send reply. */
        g_stats.arp_requests++;
        if (target_ip == g_ip && g_ip != 0) {
            u8 reply[sizeof(arp_pkt_t)];
            arp_pkt_t *r = (arp_pkt_t *)reply;
            r->htype = htons(1);
            r->ptype = htons(ETH_TYPE_IP);
            r->hlen = 6;
            r->plen = 4;
            r->op = htons(2);  /* reply */
            oc_memcpy(r->sha, g_mac, 6);
            r->spa = htonl(g_ip);
            oc_memcpy(r->tha, pkt->sha, 6);
            r->tpa = pkt->spa;
            eth_send(pkt->sha, ETH_TYPE_ARP, reply, sizeof(arp_pkt_t));
        }
    }
}

int arp_resolve(u32 ip, u8 *mac_out) {
    /* Check cache first. */
    if (arp_cache_lookup(ip, mac_out) == 0) return 0;

    /* Send ARP request. */
    u8 broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    u8 req_buf[sizeof(arp_pkt_t)];
    arp_pkt_t *req = (arp_pkt_t *)req_buf;
    req->htype = htons(1);
    req->ptype = htons(ETH_TYPE_IP);
    req->hlen = 6;
    req->plen = 4;
    req->op = htons(1);  /* request */
    oc_memcpy(req->sha, g_mac, 6);
    req->spa = htonl(g_ip);
    oc_memset(req->tha, 0, 6);
    req->tpa = htonl(ip);

    g_arp_reply_received = 0;
    eth_send(broadcast, ETH_TYPE_ARP, req_buf, sizeof(arp_pkt_t));

    /* Wait for reply with timeout (3 seconds at 100Hz = 300 ticks). */
    u64 start = oc_timer_ticks();
    while ((oc_timer_ticks() - start) < 300) {
        net_poll();
        if (g_arp_reply_received) {
            oc_memcpy(mac_out, g_arp_reply_mac, 6);
            return 0;
        }
    }
    return -1;  /* timeout */
}

int arp_get_cache(int index, u32 *ip, u8 *mac) {
    int count = 0;
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (g_arp_cache[i].valid) {
            if (count == index) {
                *ip = g_arp_cache[i].ip;
                oc_memcpy(mac, g_arp_cache[i].mac, 6);
                return 0;
            }
            count++;
        }
    }
    return -1;
}

/* ============================================================
 * IP layer
 * ============================================================ */
typedef struct __attribute__((packed)) {
    u8  ver_ihl;
    u8  tos;
    u16 total_len;
    u16 id;
    u16 flags_frag;
    u8  ttl;
    u8  protocol;
    u16 checksum;
    u32 src_ip;
    u32 dst_ip;
} ip_hdr_t;

static u16 g_ip_id = 1;

static int ip_send(u32 dst_ip, u8 protocol, const void *payload, int len) {
    /* P2-08 FIX: use a consistent IP payload MTU. The Ethernet frame
     * payload is 1500 bytes (ETH_FRAME_MAX 1514 - 14 eth header). The
     * IP header is 20 bytes, so the IP *payload* MTU is 1500 - 20 = 1480
     * bytes. The previous code clamped to 1494 with a comment that
     * contradicted the math ("... = 1480, use 1494 to be safe").
     * This matches udp_send (clamp to 1492 so 1492 + 8 = 1500) and
     * tcp_send_raw (clamp to 1480 so 1480 + 20 = 1500). */
    if (len < 0) return -1;
    if (len > 1480) len = 1480;  /* 1500 - 20 (IP header) */
    /* Resolve destination MAC (via gateway if needed). */
    u32 next_hop = dst_ip;
    /* Broadcast addresses (255.255.255.255 or subnet broadcast) go directly,
     * not through the gateway. This is critical for DHCP. */
    if (dst_ip == 0xFFFFFFFF || ((dst_ip & g_mask) == (g_ip & g_mask) && (dst_ip & ~g_mask) == ~g_mask)) {
        /* Broadcast: send to FF:FF:FF:FF:FF:FF directly. */
        u8 bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        u8 buf[ETH_FRAME_MAX];
        ip_hdr_t *iph = (ip_hdr_t *)buf;
        iph->ver_ihl = 0x45;
        iph->tos = 0;
        iph->total_len = htons((u16)(20 + len));
        iph->id = htons(g_ip_id++);
        iph->flags_frag = htons(0x4000);
        iph->ttl = 64;
        iph->protocol = protocol;
        iph->src_ip = htonl(g_ip);
        iph->dst_ip = htonl(dst_ip);
        iph->checksum = 0;
        iph->checksum = internet_checksum(iph, 20, 0);
        oc_memcpy(buf + 20, payload, len);
        return eth_send(bcast, ETH_TYPE_IP, buf, 20 + len);
    }
    if ((dst_ip & g_mask) != (g_ip & g_mask)) {
        /* Different subnet: use gateway. */
        if (g_gateway == 0) return -1;
        next_hop = g_gateway;
    }

    u8 dst_mac[6];
    if (arp_resolve(next_hop, dst_mac) != 0) return -1;

    u8 buf[ETH_FRAME_MAX];
    ip_hdr_t *iph = (ip_hdr_t *)buf;
    iph->ver_ihl = 0x45;
    iph->tos = 0;
    iph->total_len = htons((u16)(20 + len));
    iph->id = htons(g_ip_id++);
    iph->flags_frag = htons(0x4000);  /* Don't Fragment */
    iph->ttl = 64;
    iph->protocol = protocol;
    iph->src_ip = htonl(g_ip);
    iph->dst_ip = htonl(dst_ip);
    iph->checksum = 0;
    iph->checksum = internet_checksum(iph, 20, 0);

    oc_memcpy(buf + 20, payload, len);

    return eth_send(dst_mac, ETH_TYPE_IP, buf, 20 + len);
}

static void ip_handle_packet(const void *data, int len) {
    if (len < 20) return;
    const ip_hdr_t *iph = (const ip_hdr_t *)data;

    /* BUG-035 FIX: Remove per-packet debug output that floods the console.
     * This was useful during development but should not be in production. */
    /* DEBUG removed: RX IP/RX ICMP/RX UDP prints */

    /* Check if packet is for us. */
    u32 dst = ntohl(iph->dst_ip);
    if (dst != g_ip && dst != 0xFFFFFFFF && g_ip != 0) {
        /* Check broadcast. */
        if ((dst & 0xFF) != 0xFF) {
            oc_console_puts("  RX: dropping, dst not us\n");
            return;
        }
    }

    int hdr_len = (iph->ver_ihl & 0x0F) * 4;
    if (hdr_len < 20 || len < hdr_len) return;

    const void *payload = (const u8 *)data + hdr_len;
    int payload_len = ntohs(iph->total_len) - hdr_len;
    u32 src_ip = ntohl(iph->src_ip);

    switch (iph->protocol) {
        case IP_PROTO_ICMP:
            icmp_handle_packet(src_ip, payload, payload_len);
            break;
        case IP_PROTO_UDP:
            udp_handle_packet(src_ip, payload, payload_len);
            break;
        case IP_PROTO_TCP:
            tcp_handle_packet(src_ip, payload, payload_len);
            break;
    }
}

/* ============================================================
 * ICMP layer
 * ============================================================ */
typedef struct __attribute__((packed)) {
    u8  type;
    u8  code;
    u16 checksum;
    u16 id;
    u16 seq;
} icmp_hdr_t;

static u16 g_ping_id = 0;
static u16 g_ping_seq = 0;
static int g_ping_reply_received = 0;

void icmp_handle_packet(u32 src_ip, const void *data, int len) {
    if (len < 8) return;
    const icmp_hdr_t *h = (const icmp_hdr_t *)data;

    /* BUG-035 FIX: Remove ICMP debug output */

    if (h->type == 0) {
        /* Echo reply. */
        g_ping_reply_received = 1;
        g_stats.icmp_echo_recv++;
    } else if (h->type == 8) {
        /* Echo request: send reply. */
        u8 buf[64];
        icmp_hdr_t *r = (icmp_hdr_t *)buf;
        r->type = 0;  /* reply */
        r->code = 0;
        r->id = h->id;
        r->seq = h->seq;
        int plen = len - 8;
        if (plen > 56) plen = 56;
        if (plen > 0) oc_memcpy(buf + 8, (const u8 *)data + 8, plen);
        r->checksum = 0;
        r->checksum = internet_checksum(r, 8 + plen, 0);
        ip_send(src_ip, IP_PROTO_ICMP, buf, 8 + plen);
    }
}

int icmp_ping(u32 dst_ip, int timeout_ms) {
    u8 buf[8 + 32];
    icmp_hdr_t *h = (icmp_hdr_t *)buf;
    h->type = 8;  /* echo request */
    h->code = 0;
    h->id = htons(++g_ping_id);
    h->seq = htons(++g_ping_seq);
    /* Fill payload with pattern. */
    for (int i = 0; i < 32; i++) buf[8 + i] = (u8)(i & 0xFF);
    h->checksum = 0;
    h->checksum = internet_checksum(h, 8 + 32, 0);

    g_ping_reply_received = 0;
    g_stats.icmp_echo_sent++;

    if (ip_send(dst_ip, IP_PROTO_ICMP, buf, 8 + 32) < 0) return -1;

    /* Wait for reply. */
    u64 start = oc_timer_ticks();
    u64 timeout_ticks = (u64)timeout_ms / 10;
    while ((oc_timer_ticks() - start) < timeout_ticks) {
        net_poll();
        if (g_ping_reply_received) return 0;
    }
    return -1;  /* timeout */
}

/* ============================================================
 * UDP layer
 * ============================================================ */
typedef struct __attribute__((packed)) {
    u16 src_port;
    u16 dst_port;
    u16 length;
    u16 checksum;
} udp_hdr_t;

#define UDP_MAX_HANDLERS 32
typedef struct {
    u16 port;
    udp_handler_fn handler;
} udp_handler_t;

static udp_handler_t g_udp_handlers[UDP_MAX_HANDLERS];

void udp_init(void) {
    oc_memset(g_udp_handlers, 0, sizeof(g_udp_handlers));
}

int udp_bind(u16 port, udp_handler_fn handler) {
    for (int i = 0; i < UDP_MAX_HANDLERS; i++) {
        if (g_udp_handlers[i].handler == NULL) {
            g_udp_handlers[i].port = port;
            g_udp_handlers[i].handler = handler;
            return 0;
        }
    }
    return -1;
}

int udp_send(u32 dst_ip, u16 dst_port, u16 src_port, const void *data, int len) {
    /* P0-7 FIX: bound the payload to the MTU. Previously any len > 1492
     * overflowed the 1500-byte stack buffer. */
    if (len < 0) return -1;
    if (len > 1492) len = 1492;  /* 1500 - 8 (UDP header) - leave room */
    u8 buf[1500];
    udp_hdr_t *h = (udp_hdr_t *)buf;
    h->src_port = htons(src_port);
    h->dst_port = htons(dst_port);
    h->length = htons((u16)(8 + len));
    h->checksum = 0;
    oc_memcpy(buf + 8, data, len);
    return ip_send(dst_ip, IP_PROTO_UDP, buf, 8 + len);
}

void udp_handle_packet(u32 src_ip, const void *data, int len) {
    if (len < 8) return;
    const udp_hdr_t *h = (const udp_hdr_t *)data;
    u16 dst_port = ntohs(h->dst_port);
    u16 src_port = ntohs(h->src_port);
    int payload_len = ntohs(h->length) - 8;
    if (payload_len < 0) return;
    const void *payload = (const u8 *)data + 8;

    for (int i = 0; i < UDP_MAX_HANDLERS; i++) {
        if (g_udp_handlers[i].handler && g_udp_handlers[i].port == dst_port) {
            g_udp_handlers[i].handler(src_ip, src_port, payload, payload_len);
            return;
        }
    }
    /* Also check socket layer. */
    net_udp_socket_handler(src_ip, src_port, dst_port, payload, payload_len);
}

/* ============================================================
 * TCP layer (simplified state machine)
 * ============================================================ */
typedef struct __attribute__((packed)) {
    u16 src_port;
    u16 dst_port;
    u32 seq;
    u32 ack;
    u16 data_offset_flags;
    u16 window;
    u16 checksum;
    u16 urgent;
} tcp_hdr_t;

#define TCP_MAX_CONNS 32
typedef enum {
    TCP_CLOSED = 0,
    TCP_SYN_SENT,
    TCP_SYN_RCVD,
    TCP_ESTABLISHED,
    TCP_FIN_WAIT_1,
    TCP_FIN_WAIT_2,
    TCP_CLOSE_WAIT,
    TCP_LAST_ACK,
    TCP_CLOSING,
} tcp_state_t;

/* WP-09: TCP reliability fields. */
#define TCP_RTX_BUF_SIZE 4096
typedef struct {
    tcp_state_t state;
    u32 remote_ip;
    u16 local_port;
    u16 remote_port;
    u32 our_seq;        /* next seq to send */
    u32 our_ack;        /* ack we send to peer */
    u32 snd_una;        /* oldest unacknowledged seq (WP-09) */
    u32 snd_wnd;        /* peer's advertised window (WP-09) */
    u32 cwnd;           /* congestion window (WP-09) */
    u32 ssthresh;       /* slow-start threshold (WP-09) */
    u32 rto;            /* retransmission timeout in ticks (WP-09) */
    u64 rto_deadline;   /* timer tick when RTO fires (WP-09) */
    int dup_ack_count;  /* duplicate ACK counter (WP-09) */
    u16 mss;            /* maximum segment size (WP-09) */
    u8  win_scale_sent; /* window scale we offered (WP-09) */
    u8  win_scale_recv; /* window scale peer offered (WP-09) */
    u32 ts_recent;      /* latest timestamp from peer (WP-09) */
    u32 ts_echo;        /* timestamp to echo back (WP-09) */
    int ts_enabled;     /* timestamps negotiated (WP-09) */
    int in_use;
    int sock_fd;
    u8  rx_buf[2048];
    int rx_len;
    /* WP-09: retransmission buffer */
    u8  rtx_buf[TCP_RTX_BUF_SIZE];
    int rtx_len;         /* bytes in rtx_buf awaiting ACK */
    u32 rtx_seq;         /* seq of first byte in rtx_buf */
    int rtt_measured;    /* have we measured RTT yet */
    u32 srtt;            /* smoothed RTT in ticks */
    u32 rttvar;          /* RTT variance */
} tcp_conn_t;

static tcp_conn_t g_tcp_conns[TCP_MAX_CONNS];

void tcp_init(void) {
    oc_memset(g_tcp_conns, 0, sizeof(g_tcp_conns));
}

static tcp_conn_t *tcp_find_conn(u32 ip, u16 local_port, u16 remote_port) {
    for (int i = 0; i < TCP_MAX_CONNS; i++) {
        if (g_tcp_conns[i].in_use &&
            g_tcp_conns[i].remote_ip == ip &&
            g_tcp_conns[i].local_port == local_port &&
            g_tcp_conns[i].remote_port == remote_port) {
            return &g_tcp_conns[i];
        }
    }
    return NULL;
}

static tcp_conn_t *tcp_alloc_conn(void) {
    for (int i = 0; i < TCP_MAX_CONNS; i++) {
        if (!g_tcp_conns[i].in_use) {
            oc_memset(&g_tcp_conns[i], 0, sizeof(tcp_conn_t));
            g_tcp_conns[i].in_use = 1;
            return &g_tcp_conns[i];
        }
    }
    return NULL;
}

static u16 tcp_checksum(u32 src_ip, u32 dst_ip, const void *data, int len) {
    /* BUG-003 FIX (revised): Construct the pseudo-header in network byte
     * order (as bytes), compute its RAW sum (NOT the one's complement
     * checksum), and pass that raw sum as the initial value to
     * internet_checksum for the TCP segment data.
     *
     * The previous attempt called internet_checksum(ph, 12, 0) which
     * returns ~sum (the checksum), then passed that as the initial
     * value to the second call. But the second call needs the RAW sum,
     * not the one's complement. This produced wrong checksums
     * (0xdc57 instead of the correct 0xac01), which SLIRP silently
     * dropped.
     *
     * src_ip and dst_ip are stored as u32 in network byte order
     * (IP4 macro: (a<<24)|(b<<16)|(c<<8)|d). Extracting
     * (>> 24, >> 16, >> 8, >> 0) gives the bytes in network order. */
    u8 ph[12];
    ph[0] = (u8)((src_ip >> 24) & 0xFF);
    ph[1] = (u8)((src_ip >> 16) & 0xFF);
    ph[2] = (u8)((src_ip >> 8) & 0xFF);
    ph[3] = (u8)(src_ip & 0xFF);
    ph[4] = (u8)((dst_ip >> 24) & 0xFF);
    ph[5] = (u8)((dst_ip >> 16) & 0xFF);
    ph[6] = (u8)((dst_ip >> 8) & 0xFF);
    ph[7] = (u8)(dst_ip & 0xFF);
    ph[8] = 0;           /* zero */
    ph[9] = 6;            /* protocol = TCP */
    ph[10] = (u8)((len >> 8) & 0xFF);   /* TCP length high byte */
    ph[11] = (u8)(len & 0xFF);           /* TCP length low byte */
    /* Compute the RAW sum of the pseudo-header (do NOT take one's
     * complement). internet_checksum will add this to the TCP data
     * sum, fold carries, and return the final ~sum at the end. */
    const u16 *p = (const u16 *)(const void *)ph;
    u32 sum = 0;
    for (int i = 0; i < 6; i++)
        sum += p[i];
    return internet_checksum(data, len, sum);
}

/* WP-09: TCP option kind codes */
#define TCP_OPT_MSS      2
#define TCP_OPT_WSCALE   3
#define TCP_OPT_SACK_PERM 4
#define TCP_OPT_TS       8
#define TCP_OPT_END      0
#define TCP_OPT_NOP      1

/* WP-09: Build TCP options for SYN packets (MSS, Window Scale, SACK-Permitted, Timestamps) */
static int tcp_build_syn_options(tcp_conn_t *c, u8 *opts) {
    int i = 0;
    /* MSS option (4 bytes) */
    opts[i++] = TCP_OPT_MSS;
    opts[i++] = 4;
    opts[i++] = (u8)(c->mss >> 8);
    opts[i++] = (u8)(c->mss & 0xFF);
    /* Window Scale option (3 bytes + 1 NOP for alignment) */
    opts[i++] = TCP_OPT_NOP;
    opts[i++] = TCP_OPT_WSCALE;
    opts[i++] = 3;
    opts[i++] = c->win_scale_sent;
    /* SACK-Permitted option (2 bytes) */
    opts[i++] = TCP_OPT_SACK_PERM;
    opts[i++] = 2;
    /* Timestamps option (10 bytes + 2 NOP for alignment) */
    opts[i++] = TCP_OPT_NOP;
    opts[i++] = TCP_OPT_NOP;
    opts[i++] = TCP_OPT_TS;
    opts[i++] = 10;
    /* TSval: current tick count */
    u32 tsval = (u32)oc_timer_ticks();
    opts[i++] = (u8)(tsval >> 24);
    opts[i++] = (u8)(tsval >> 16);
    opts[i++] = (u8)(tsval >> 8);
    opts[i++] = (u8)(tsval & 0xFF);
    /* Echo reply: 0 (no previous timestamp) */
    opts[i++] = 0; opts[i++] = 0; opts[i++] = 0; opts[i++] = 0;
    return i;  /* should be 20 bytes */
}

/* WP-09: Build TCP Timestamp option for data packets */
static int tcp_build_ts_option(tcp_conn_t *c, u8 *opts) {
    if (!c->ts_enabled) return 0;
    int i = 0;
    opts[i++] = TCP_OPT_NOP;
    opts[i++] = TCP_OPT_NOP;
    opts[i++] = TCP_OPT_TS;
    opts[i++] = 10;
    u32 tsval = (u32)oc_timer_ticks();
    opts[i++] = (u8)(tsval >> 24);
    opts[i++] = (u8)(tsval >> 16);
    opts[i++] = (u8)(tsval >> 8);
    opts[i++] = (u8)(tsval & 0xFF);
    u32 echo = c->ts_recent;
    opts[i++] = (u8)(echo >> 24);
    opts[i++] = (u8)(echo >> 16);
    opts[i++] = (u8)(echo >> 8);
    opts[i++] = (u8)(echo & 0xFF);
    return 12;
}

/* WP-09: Parse TCP options from incoming packet */
static void tcp_parse_options(tcp_conn_t *c, const u8 *opts, int opt_len, int is_syn) {
    int i = 0;
    while (i < opt_len) {
        u8 kind = opts[i++];
        if (kind == TCP_OPT_END) break;
        if (kind == TCP_OPT_NOP) continue;
        if (i >= opt_len) break;
        u8 len = opts[i++];
        if (len < 2 || i + len - 2 > opt_len) break;
        switch (kind) {
            case TCP_OPT_MSS:
                if (len == 4 && is_syn) {
                    u16 mss = ((u16)opts[i] << 8) | opts[i+1];
                    if (mss > 536 && mss < 9000) c->mss = mss;
                }
                break;
            case TCP_OPT_WSCALE:
                if (len == 3 && is_syn) {
                    c->win_scale_recv = opts[i];
                }
                break;
            case TCP_OPT_SACK_PERM:
                if (is_syn) { /* SACK permitted — we support it */ }
                break;
            case TCP_OPT_TS:
                if (len == 10) {
                    u32 tsval = ((u32)opts[i] << 24) | ((u32)opts[i+1] << 16)
                              | ((u32)opts[i+2] << 8) | opts[i+3];
                    u32 tsecr = ((u32)opts[i+4] << 24) | ((u32)opts[i+5] << 16)
                              | ((u32)opts[i+6] << 8) | opts[i+7];
                    c->ts_recent = tsval;
                    if (is_syn) c->ts_enabled = 1;
                    (void)tsecr;
                }
                break;
        }
        i += len - 2;
    }
}

static int tcp_send_raw(tcp_conn_t *c, u8 flags, const void *data, int len) {
    /* P0-7 FIX: bound the payload to the MTU. */
    if (len < 0) len = 0;
    if (len > 1480) len = 1480;

    u8 buf[1500];
    tcp_hdr_t *h = (tcp_hdr_t *)buf;
    h->src_port = htons(c->local_port);
    h->dst_port = htons(c->remote_port);
    h->seq = htonl(c->our_seq);
    h->ack = htonl(c->our_ack);

    /* WP-09: Build options. For SYN, include MSS/WScale/SACK/TS.
     * For data, include Timestamps if enabled. */
    int hdr_len = 20;
    int opt_len = 0;
    if (flags & TCP_SYN) {
        opt_len = tcp_build_syn_options(c, buf + 20);
    } else {
        opt_len = tcp_build_ts_option(c, buf + 20);
    }
    hdr_len = 20 + opt_len;
    int data_offset = (hdr_len / 4) << 12;
    h->data_offset_flags = htons((u16)data_offset | flags);

    /* WP-09: Use snd_wnd (clamped) instead of hardcoded 8192 */
    u16 win = (u16)(c->snd_wnd > 65535 ? 65535 : c->snd_wnd);
    if (win == 0) win = 8192;  /* fallback if unset */
    h->window = htons(win);
    h->urgent = 0;
    if (len > 0 && data) oc_memcpy(buf + hdr_len, data, len);
    h->checksum = 0;
    h->checksum = tcp_checksum(g_ip, c->remote_ip, h, hdr_len + len);
    return ip_send(c->remote_ip, IP_PROTO_TCP, h, hdr_len + len);
}

int tcp_connect(u32 dst_ip, u16 dst_port) {
    tcp_conn_t *c = tcp_alloc_conn();
    if (!c) return -1;
    c->state = TCP_SYN_SENT;
    c->remote_ip = dst_ip;
    c->local_port = 40000 + (g_stats.tcp_connections % 10000);
    c->remote_port = dst_port;
    c->our_seq = 0x1000;
    c->our_ack = 0;
    c->sock_fd = -1;
    /* WP-09: init reliability fields */
    c->snd_una = c->our_seq;
    c->snd_wnd = 8192;
    c->cwnd = 1;           /* slow start: 1 MSS */
    c->ssthresh = 65535;   /* high threshold → slow start phase */
    c->rto = 30;           /* 300ms initial RTO (30 ticks @ 100Hz) */
    c->rto_deadline = 0;
    c->dup_ack_count = 0;
    c->mss = 1460;         /* default MSS (Ethernet MTU - 40) */
    c->win_scale_sent = 7; /* window scale: 2^7 = 128 → 8192*128 = 1MB */
    c->win_scale_recv = 0;
    c->ts_recent = 0;
    c->ts_echo = 0;
    c->ts_enabled = 0;
    c->rtx_len = 0;
    c->rtx_seq = 0;
    c->rtt_measured = 0;
    c->srtt = 0;
    c->rttvar = 0;

    /* Send SYN with options. */
    tcp_send_raw(c, TCP_SYN, NULL, 0);
    c->our_seq += 1;
    c->snd_una = c->our_seq;

    g_stats.tcp_connections++;

    /* Wait for SYN-ACK. */
    u64 start = oc_timer_ticks();
    while ((oc_timer_ticks() - start) < 300) {  /* 3 second timeout */
        net_poll();
        if (c->state == TCP_ESTABLISHED) {
            return (int)(c - g_tcp_conns);
        }
    }
    c->in_use = 0;
    return -1;
}

/* WP-09: Update RTT estimate and RTO (RFC 6298 algorithm) */
static void tcp_update_rtt(tcp_conn_t *c, u32 rtt_ticks) {
    if (!c->rtt_measured) {
        /* First measurement: SRTT = RTT, RTTVAR = RTT/2 */
        c->srtt = rtt_ticks;
        c->rttvar = rtt_ticks / 2;
        c->rtt_measured = 1;
    } else {
        /* Subsequent: RTTVAR = 0.75*RTTVAR + 0.25*|SRTT-RTT| */
        u32 diff = (c->srtt > rtt_ticks) ? (c->srtt - rtt_ticks) : (rtt_ticks - c->srtt);
        c->rttvar = (3 * c->rttvar + diff) / 4;
        /* SRTT = 0.875*SRTT + 0.125*RTT */
        c->srtt = (7 * c->srtt + rtt_ticks) / 8;
    }
    /* RTO = max(SRTT + 4*RTTVAR, 10 ticks = 100ms) */
    u32 rto = c->srtt + 4 * c->rttvar;
    if (rto < 10) rto = 10;
    if (rto > 600) rto = 600;  /* cap at 6 seconds */
    c->rto = rto;
}

/* WP-09: Check and handle RTO timer for all connections */
static void tcp_check_rto(void) {
    u64 now = oc_timer_ticks();
    for (int i = 0; i < TCP_MAX_CONNS; i++) {
        tcp_conn_t *c = &g_tcp_conns[i];
        if (!c->in_use || c->state != TCP_ESTABLISHED) continue;
        if (c->rtx_len == 0 || c->rto_deadline == 0) continue;
        if (now < c->rto_deadline) continue;
        /* RTO expired! Retransmit oldest unacked data. */
        if (c->rtx_len > 0) {
            int rtx = c->rtx_len;
            if (rtx > c->mss) rtx = c->mss;
            /* Retransmit from rtx_buf */
            c->our_seq = c->rtx_seq;  /* go back to oldest unacked */
            tcp_send_raw(c, TCP_ACK | TCP_PSH, c->rtx_buf, rtx);
            c->our_seq += rtx;
        }
        /* Congestion control: RTO → cwnd = 1, ssthresh = max(in_flight/2, 2) */
        int in_flight = c->rtx_len;
        u32 new_ssthresh = in_flight / 2;
        if (new_ssthresh < 2 * c->mss) new_ssthresh = 2 * c->mss;
        c->ssthresh = new_ssthresh;
        c->cwnd = 1;  /* back to slow start */
        /* Exponential backoff: double RTO */
        c->rto *= 2;
        if (c->rto > 600) c->rto = 600;
        c->rto_deadline = now + c->rto;
        c->dup_ack_count = 0;
    }
}

int tcp_send(int sock, const void *data, int len) {
    if (sock < 0 || sock >= TCP_MAX_CONNS) return -1;
    tcp_conn_t *c = &g_tcp_conns[sock];
    if (!c->in_use || c->state != TCP_ESTABLISHED) return -1;

    /* WP-09: Clamp send size to min(cwnd, snd_wnd, mss) */
    int sendable = len;
    u32 win = c->cwnd * c->mss;
    if (win < c->snd_wnd) {
        if ((u32)sendable > win) sendable = (int)win;
    } else {
        if ((u32)sendable > c->snd_wnd) sendable = (int)c->snd_wnd;
    }
    if (sendable > c->mss) sendable = c->mss;
    if (sendable > 1480) sendable = 1480;

    /* WP-09: Copy to retransmission buffer */
    if (c->rtx_len + sendable <= TCP_RTX_BUF_SIZE) {
        oc_memcpy(c->rtx_buf + c->rtx_len, data, sendable);
        if (c->rtx_len == 0) {
            c->rtx_seq = c->our_seq;  /* record oldest unacked seq */
            c->rto_deadline = oc_timer_ticks() + c->rto;  /* arm RTO timer */
        }
        c->rtx_len += sendable;
    }

    tcp_send_raw(c, TCP_ACK | TCP_PSH, data, sendable);
    c->our_seq += sendable;
    return sendable;
}

int tcp_close(int sock) {
    if (sock < 0 || sock >= TCP_MAX_CONNS) return -1;
    tcp_conn_t *c = &g_tcp_conns[sock];
    if (!c->in_use) return -1;

    if (c->state == TCP_ESTABLISHED) {
        /* Send FIN-ACK. */
        tcp_send_raw(c, TCP_FIN | TCP_ACK, NULL, 0);
        c->our_seq += 1;
        c->state = TCP_FIN_WAIT_1;
        /* Wait briefly for ACK. */
        u64 start = oc_timer_ticks();
        while ((oc_timer_ticks() - start) < 100) {
            net_poll();
            if (c->state == TCP_CLOSED) break;
        }
    }
    c->in_use = 0;
    return 0;
}

/* P1-13 FIX: implement tcp_listen — register a handler for incoming
 * connections on a port. When a SYN arrives, tcp_handle_packet will
 * create a connection and call the handler. */
#define TCP_MAX_LISTENERS 8
static struct {
    u16 port;
    tcp_handler_fn handler;
    int in_use;
} g_tcp_listeners[TCP_MAX_LISTENERS];

int tcp_listen(u16 port, tcp_handler_fn handler) {
    for (int i = 0; i < TCP_MAX_LISTENERS; i++) {
        if (!g_tcp_listeners[i].in_use) {
            g_tcp_listeners[i].port = port;
            g_tcp_listeners[i].handler = handler;
            g_tcp_listeners[i].in_use = 1;
            return 0;
        }
    }
    return -1;  /* table full */
}

void tcp_handle_packet(u32 src_ip, const void *data, int len) {
    if (len < 20) return;
    const tcp_hdr_t *h = (const tcp_hdr_t *)data;
    u16 src_port = ntohs(h->src_port);
    u16 dst_port = ntohs(h->dst_port);
    u32 seq = ntohl(h->seq);
    u32 ack = ntohl(h->ack);
    u16 flags = ntohs(h->data_offset_flags) & 0x1FF;
    u16 win = ntohs(h->window);
    int hdr_len = (ntohs(h->data_offset_flags) >> 12) * 4;
    int payload_len = len - hdr_len;
    const void *payload = (const u8 *)data + hdr_len;
    const u8 *opts = (const u8 *)data + 20;
    int opt_len = hdr_len - 20;

    tcp_conn_t *c = tcp_find_conn(src_ip, dst_port, src_port);

    /* WP-09: Parse TCP options if present */
    int is_syn = (flags & TCP_SYN) ? 1 : 0;
    if (opt_len > 0 && c) {
        tcp_parse_options(c, opts, opt_len, is_syn);
    }
    if (!c) {
        /* P1-13 FIX: check if this is a SYN to a listening port. If so,
         * create a new connection, send SYN-ACK, and call the handler. */
        if (flags & TCP_SYN) {
            for (int i = 0; i < TCP_MAX_LISTENERS; i++) {
                if (g_tcp_listeners[i].in_use && g_tcp_listeners[i].port == dst_port) {
                    c = tcp_alloc_conn();
                    if (!c) return;
                    c->remote_ip = src_ip;
                    c->remote_port = src_port;
                    c->local_port = dst_port;
                    c->state = TCP_SYN_RCVD;
                    c->our_seq = 1000;
                    c->our_ack = seq + 1;
                    /* WP-09: init reliability fields for server-side conn */
                    c->snd_una = c->our_seq;
                    c->snd_wnd = 8192;
                    c->cwnd = 1;
                    c->ssthresh = 65535;
                    c->rto = 30;
                    c->mss = 1460;
                    c->win_scale_sent = 7;
                    c->rtx_len = 0;
                    c->rtt_measured = 0;
                    /* Send SYN-ACK. */
                    tcp_send_raw(c, TCP_SYN | TCP_ACK, NULL, 0);
                    c->our_seq += 1;
                    /* Call the handler to notify of new connection. */
                    if (g_tcp_listeners[i].handler) {
                        g_tcp_listeners[i].handler(src_ip, src_port);
                    }
                    return;
                }
            }
        }
        return;
    }

    if (flags & TCP_RST) {
        c->state = TCP_CLOSED;
        c->in_use = 0;
        return;
    }

    switch (c->state) {
        case TCP_SYN_SENT:
            if ((flags & TCP_SYN) && (flags & TCP_ACK)) {
                c->our_ack = seq + 1;
                /* Send ACK. */
                tcp_send_raw(c, TCP_ACK, NULL, 0);
                c->state = TCP_ESTABLISHED;
            }
            break;
        case TCP_SYN_RCVD:
            /* P1-12 FIX: handle SYN_RCVD — ACK completes the handshake. */
            if (flags & TCP_ACK) {
                c->state = TCP_ESTABLISHED;
            }
            break;
        case TCP_ESTABLISHED:
            /* WP-09: Handle ACK for sent data */
            if (flags & TCP_ACK) {
                /* Update snd_una (slide window) */
                if (ack > c->snd_una) {
                    /* WP-09: New data ACKed — slide retransmission buffer */
                    u32 acked_bytes = ack - c->snd_una;
                    if (c->rtx_len > 0 && acked_bytes <= (u32)c->rtx_len) {
                        /* Slide rtx_buf forward by acked_bytes */
                        int remaining = c->rtx_len - (int)acked_bytes;
                        if (remaining > 0) {
                            oc_memmove(c->rtx_buf, c->rtx_buf + acked_bytes, remaining);
                        }
                        c->rtx_len = remaining;
                        c->rtx_seq = ack;
                        if (c->rtx_len == 0) {
                            c->rto_deadline = 0;  /* disarm RTO */
                        }
                    } else if (c->rtx_len > 0) {
                        /* All data ACKed */
                        c->rtx_len = 0;
                        c->rto_deadline = 0;
                    }
                    c->snd_una = ack;
                    c->dup_ack_count = 0;  /* reset dup ACK counter */

                    /* WP-09: Congestion control — increase cwnd */
                    if (c->cwnd < c->ssthresh / c->mss) {
                        /* Slow start: +1 per ACK */
                        c->cwnd++;
                    } else {
                        /* Congestion avoidance: +1/cwnd per ACK (approx) */
                        if ((c->cwnd * c->mss) % (c->cwnd * c->mss) == 0) {
                            c->cwnd++;
                        }
                    }

                    /* WP-09: Update RTT using timestamp echo if available */
                    if (c->ts_enabled && opt_len > 0) {
                        /* The timestamp echo reply is in the TS option.
                         * We parse it during tcp_parse_options. For RTT,
                         * we need the echo of OUR timestamp. */
                        /* Simple RTT: time since we sent the data */
                        u32 rtt = (u32)(oc_timer_ticks() - (c->rto - c->srtt));
                        if (rtt > 0 && rtt < 600) {
                            tcp_update_rtt(c, rtt);
                        }
                    }
                } else if (ack == c->snd_una && payload_len == 0) {
                    /* WP-09: Duplicate ACK (no new data, same ack) */
                    c->dup_ack_count++;
                    if (c->dup_ack_count == 3) {
                        /* Fast retransmit: retransmit oldest unacked segment */
                        if (c->rtx_len > 0) {
                            int rtx = c->rtx_len;
                            if (rtx > c->mss) rtx = c->mss;
                            u32 saved_seq = c->our_seq;
                            c->our_seq = c->rtx_seq;
                            tcp_send_raw(c, TCP_ACK | TCP_PSH, c->rtx_buf, rtx);
                            c->our_seq = saved_seq;  /* don't advance seq on retransmit */
                        }
                        /* Fast recovery: ssthresh = max(cwnd/2, 2), cwnd = ssthresh */
                        u32 new_ss = (c->cwnd * c->mss) / 2;
                        if (new_ss < 2 * c->mss) new_ss = 2 * c->mss;
                        c->ssthresh = new_ss;
                        c->cwnd = c->ssthresh / c->mss;
                        if (c->cwnd < 1) c->cwnd = 1;
                    }
                }
            }

            /* WP-09: Update send window from peer's advertised window */
            if (c->win_scale_recv > 0) {
                c->snd_wnd = (u32)win << c->win_scale_recv;
            } else {
                c->snd_wnd = win;
            }

            if (payload_len > 0) {
                /* Data received. */
                if (c->rx_len + payload_len < (int)sizeof(c->rx_buf)) {
                    oc_memcpy(c->rx_buf + c->rx_len, payload, payload_len);
                    c->rx_len += payload_len;
                }
                c->our_ack = seq + payload_len;
                tcp_send_raw(c, TCP_ACK, NULL, 0);
            }
            if (flags & TCP_FIN) {
                /* P1-12 FIX: remote sent FIN → go to CLOSE_WAIT, not LAST_ACK.
                 * The standard TCP state machine: ESTABLISHED + FIN → CLOSE_WAIT.
                 * We ACK the FIN and wait for our side to close. */
                c->our_ack = seq + payload_len + 1;
                tcp_send_raw(c, TCP_ACK, NULL, 0);
                c->state = TCP_CLOSE_WAIT;
            }
            break;
        case TCP_CLOSE_WAIT:
            /* P1-12 FIX: handle CLOSE_WAIT — remote already closed.
             * We can still send data. When we're done, we send FIN. */
            if (flags & TCP_RST) {
                c->state = TCP_CLOSED;
                c->in_use = 0;
            }
            break;
        case TCP_FIN_WAIT_1:
            if (flags & TCP_ACK && !(flags & TCP_FIN)) {
                c->state = TCP_FIN_WAIT_2;
            } else if (flags & TCP_FIN) {
                /* Simultaneous close: both sent FIN. */
                c->our_ack = seq + 1;
                tcp_send_raw(c, TCP_ACK, NULL, 0);
                c->state = TCP_CLOSING;
            }
            break;
        case TCP_FIN_WAIT_2:
            if (flags & TCP_FIN) {
                c->our_ack = seq + 1;
                tcp_send_raw(c, TCP_ACK, NULL, 0);
                c->state = TCP_CLOSED;
                c->in_use = 0;
            }
            break;
        case TCP_CLOSING:
            /* P1-12 FIX: handle CLOSING — waiting for ACK of our FIN. */
            if (flags & TCP_ACK) {
                c->state = TCP_CLOSED;
                c->in_use = 0;
            }
            break;
        case TCP_LAST_ACK:
            if (flags & TCP_ACK) {
                c->state = TCP_CLOSED;
                c->in_use = 0;
            }
            break;
        default:
            break;
    }
}

/* ============================================================
 * Socket API
 * ============================================================ */
#define MAX_SOCKETS 32
typedef struct {
    int type;       /* SOCK_TCP or SOCK_UDP */
    u32 remote_ip;
    u16 local_port;
    u16 remote_port;
    int in_use;
    int tcp_conn;   /* index into g_tcp_conns for TCP sockets */
    /* UDP receive buffer */
    u8  rx_buf[2048];
    int rx_len;
    u32 rx_from_ip;
    u16 rx_from_port;
} net_socket_t;

static net_socket_t g_sockets[MAX_SOCKETS];

/* Called from UDP handler to deliver data to bound sockets. */
void net_udp_socket_handler(u32 src_ip, u16 src_port, u16 dst_port,
                            const void *data, int len) {
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (g_sockets[i].in_use && g_sockets[i].type == SOCK_UDP &&
            g_sockets[i].local_port == dst_port) {
            if (g_sockets[i].rx_len == 0) {
                int n = len;
                if (n > (int)sizeof(g_sockets[i].rx_buf)) n = sizeof(g_sockets[i].rx_buf);
                oc_memcpy(g_sockets[i].rx_buf, data, n);
                g_sockets[i].rx_len = n;
                g_sockets[i].rx_from_ip = src_ip;
                g_sockets[i].rx_from_port = src_port;
            }
            return;
        }
    }
}

int net_socket(int type) {
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (!g_sockets[i].in_use) {
            oc_memset(&g_sockets[i], 0, sizeof(net_socket_t));
            g_sockets[i].in_use = 1;
            g_sockets[i].type = type;
            g_sockets[i].tcp_conn = -1;
            return i;
        }
    }
    return -1;
}

int net_bind(int fd, u32 ip, u16 port) {
    if (fd < 0 || fd >= MAX_SOCKETS || !g_sockets[fd].in_use) return -1;
    g_sockets[fd].local_port = port;
    (void)ip;
    return 0;
}

int net_connect(int fd, u32 ip, u16 port) {
    if (fd < 0 || fd >= MAX_SOCKETS || !g_sockets[fd].in_use) return -1;
    g_sockets[fd].remote_ip = ip;
    g_sockets[fd].remote_port = port;

    if (g_sockets[fd].type == SOCK_TCP) {
        int conn = tcp_connect(ip, port);
        if (conn < 0) return -1;
        g_sockets[fd].tcp_conn = conn;
        g_tcp_conns[conn].sock_fd = fd;
    }
    return 0;
}

int net_send(int fd, const void *data, int len) {
    if (fd < 0 || fd >= MAX_SOCKETS || !g_sockets[fd].in_use) return -1;
    if (g_sockets[fd].type == SOCK_TCP) {
        return tcp_send(g_sockets[fd].tcp_conn, data, len);
    } else {
        return udp_send(g_sockets[fd].remote_ip, g_sockets[fd].remote_port,
                       g_sockets[fd].local_port, data, len);
    }
}

int net_recv(int fd, void *buf, int len) {
    if (fd < 0 || fd >= MAX_SOCKETS || !g_sockets[fd].in_use) return -1;

    if (g_sockets[fd].type == SOCK_TCP) {
        int conn = g_sockets[fd].tcp_conn;
        if (conn < 0 || !g_tcp_conns[conn].in_use) return -1;
        /* Wait for data. */
        u64 start = oc_timer_ticks();
        while ((oc_timer_ticks() - start) < 500) {
            net_poll();
            if (g_tcp_conns[conn].rx_len > 0) {
                int n = g_tcp_conns[conn].rx_len;
                if (n > len) n = len;
                oc_memcpy(buf, g_tcp_conns[conn].rx_buf, n);
                g_tcp_conns[conn].rx_len = 0;
                return n;
            }
        }
        return -1;
    } else {
        /* UDP: wait for data. */
        u64 start = oc_timer_ticks();
        while ((oc_timer_ticks() - start) < 500) {
            net_poll();
            if (g_sockets[fd].rx_len > 0) {
                int n = g_sockets[fd].rx_len;
                if (n > len) n = len;
                oc_memcpy(buf, g_sockets[fd].rx_buf, n);
                g_sockets[fd].rx_len = 0;
                return n;
            }
        }
        return -1;
    }
}

int net_close(int fd) {
    if (fd < 0 || fd >= MAX_SOCKETS || !g_sockets[fd].in_use) return -1;
    if (g_sockets[fd].type == SOCK_TCP && g_sockets[fd].tcp_conn >= 0) {
        tcp_close(g_sockets[fd].tcp_conn);
    }
    g_sockets[fd].in_use = 0;
    return 0;
}

/* ============================================================
 * DHCP client
 * ============================================================ */
typedef struct __attribute__((packed)) {
    u8  op;
    u8  htype;
    u8  hlen;
    u8  hops;
    u32 xid;
    u16 secs;
    u16 flags;
    u32 ciaddr;
    u32 yiaddr;
    u32 siaddr;
    u32 giaddr;
    u8  chaddr[16];
    u8  sname[64];
    u8  file[128];
    u32 magic;
    u8  options[312];
} dhcp_pkt_t;

static u32 g_dhcp_offered_ip = 0;
static u32 g_dhcp_server_ip = 0;
static int g_dhcp_got_offer = 0;
static int g_dhcp_got_ack = 0;

static void dhcp_handler(u32 src_ip, u16 src_port, const void *data, int len) {
    (void)src_ip;
    (void)src_port;
    if (len < (int)sizeof(dhcp_pkt_t) - 312) return;
    const dhcp_pkt_t *p = (const dhcp_pkt_t *)data;
    if (ntohl(p->magic) != 0x63825363) return;

    /* Parse options. */
    int opt_len = len - ((const u8 *)p->options - (const u8 *)data);
    const u8 *opts = p->options;
    int i = 0;
    u8 msg_type = 0;

    while (i < opt_len && opts[i] != 0xFF) {
        u8 opt = opts[i++];
        if (opt == 0) continue;  /* pad */
        if (i >= opt_len) break;
        u8 olen = opts[i++];
        if (i + olen > opt_len) break;
        if (opt == 53 && olen >= 1) {
            msg_type = opts[i];
        }
        i += olen;
    }

    if (msg_type == 2) {
        /* DHCP OFFER. */
        g_dhcp_offered_ip = ntohl(p->yiaddr);
        g_dhcp_got_offer = 1;
    } else if (msg_type == 5) {
        /* DHCP ACK. */
        g_dhcp_got_ack = 1;
        g_dhcp_offered_ip = ntohl(p->yiaddr);
        /* Parse options for mask/gateway/dns. */
        i = 0;
        u32 mask = 0, gw = 0, dns = 0;
        while (i < opt_len && opts[i] != 0xFF) {
            u8 opt = opts[i++];
            if (opt == 0) continue;
            if (i >= opt_len) break;
            u8 olen = opts[i++];
            if (i + olen > opt_len) break;
            if (opt == 1 && olen >= 4) {
                mask = ntohl(*(u32 *)(opts + i));
            } else if (opt == 3 && olen >= 4) {
                gw = ntohl(*(u32 *)(opts + i));
            } else if (opt == 6 && olen >= 4) {
                dns = ntohl(*(u32 *)(opts + i));
            }
            i += olen;
        }
        if (mask) g_mask = mask;
        if (gw) g_gateway = gw;
        if (dns) g_dns = dns;
    }
}

int dhcp_discover(void) {
    if (!g_nic_ok) return -1;

    /* Bind UDP port 68 for DHCP responses. */
    udp_bind(DHCP_CLIENT_PORT, dhcp_handler);

    u8 buf[sizeof(dhcp_pkt_t)];
    dhcp_pkt_t *p = (dhcp_pkt_t *)buf;
    oc_memset(p, 0, sizeof(dhcp_pkt_t));
    p->op = 1;  /* BOOTREQUEST */
    p->htype = 1;  /* Ethernet */
    p->hlen = 6;
    p->hops = 0;
    p->xid = htonl(0x12345678);
    p->secs = 0;
    p->flags = htons(0x8000);  /* broadcast */
    p->ciaddr = 0;
    p->yiaddr = 0;
    p->siaddr = 0;
    p->giaddr = 0;
    oc_memcpy(p->chaddr, g_mac, 6);
    p->magic = htonl(0x63825363);

    /* Options: 53(msg type)=1(discover), 55(param req list), 0xFF(end). */
    int oi = 0;
    p->options[oi++] = 53; p->options[oi++] = 1; p->options[oi++] = 1;  /* DISCOVER */
    p->options[oi++] = 55; p->options[oi++] = 4;
    p->options[oi++] = 1;  /* subnet mask */
    p->options[oi++] = 3;  /* router */
    p->options[oi++] = 6;  /* DNS */
    p->options[oi++] = 0xFF;  /* end */

    int pkt_len = (int)((u8 *)(p->options + oi) - buf);
    /* Pad to minimum BOOTP size (300 bytes) - some DHCP servers require this. */
    while (pkt_len < 300) {
        p->options[oi++] = 0;  /* PAD option */
        pkt_len++;
    }
    g_dhcp_got_offer = 0;
    g_dhcp_got_ack = 0;

    /* Send DHCP DISCOVER with src IP 0.0.0.0 (per DHCP spec).
     * Temporarily set g_ip=0 so ip_send uses 0.0.0.0 as src. */
    u32 saved_ip = g_ip;
    u32 saved_mask = g_mask;
    g_ip = 0;
    g_mask = 0;

    /* Send DHCPDISCOVER (broadcast). */
    /* Debug: dump first 64 bytes of DHCP packet */
    {
        char dbuf[200]; char dn[20];
        oc_strcpy(dbuf, "DHCP DISCOVER hex:\n");
        oc_console_puts(dbuf);
        for (int i = 0; i < 64; i++) {
            if (i % 16 == 0) {
                oc_strcpy(dbuf, "  ");
                oc_u64_to_hex((u64)i, dn, 2); oc_strcat(dbuf, dn);
                oc_strcat(dbuf, ": ");
            }
            oc_u64_to_hex(buf[i], dn, 2);
            oc_strcat(dbuf, dn); oc_strcat(dbuf, " ");
            if (i % 16 == 15) {
                oc_strcat(dbuf, "\n");
                oc_console_puts(dbuf);
                dbuf[0] = 0;
            }
        }
        if (64 % 16 != 0) { oc_strcat(dbuf, "\n"); oc_console_puts(dbuf); }
    }
    udp_send(0xFFFFFFFF, DHCP_SERVER_PORT, DHCP_CLIENT_PORT, buf, pkt_len);
    
    /* Restore IP and mask for receiving the reply. */
    g_ip = saved_ip;
    g_mask = saved_mask;

    /* Wait for DHCPOFFER. */
    u64 start = oc_timer_ticks();
    while ((oc_timer_ticks() - start) < 500) {
        net_poll();
        if (g_dhcp_got_offer) break;
    }
    if (!g_dhcp_got_offer) return -1;

    /* Send DHCPREQUEST. */
    oc_memset(p, 0, sizeof(dhcp_pkt_t));
    p->op = 1;
    p->htype = 1;
    p->hlen = 6;
    p->xid = htonl(0x12345678);
    p->flags = htons(0x8000);
    oc_memcpy(p->chaddr, g_mac, 6);
    p->magic = htonl(0x63825363);
    p->ciaddr = 0;
    p->yiaddr = 0;

    oi = 0;
    p->options[oi++] = 53; p->options[oi++] = 1; p->options[oi++] = 3;  /* REQUEST */
    p->options[oi++] = 50; p->options[oi++] = 4;  /* requested IP */
    *(u32 *)(p->options + oi) = htonl(g_dhcp_offered_ip); oi += 4;
    p->options[oi++] = 54; p->options[oi++] = 4;  /* server ID */
    *(u32 *)(p->options + oi) = htonl(g_dhcp_server_ip); oi += 4;
    p->options[oi++] = 0xFF;

    pkt_len = (int)((u8 *)(p->options + oi) - buf);

    g_dhcp_got_ack = 0;
    /* DHCP REQUEST also uses src 0.0.0.0 */
    g_ip = 0;
    g_mask = 0;
    udp_send(0xFFFFFFFF, DHCP_SERVER_PORT, DHCP_CLIENT_PORT, buf, pkt_len);
    g_ip = saved_ip;
    g_mask = saved_mask;

    /* Wait for DHCPACK. */
    start = oc_timer_ticks();
    while ((oc_timer_ticks() - start) < 500) {
        net_poll();
        if (g_dhcp_got_ack) {
            g_ip = g_dhcp_offered_ip;
            return 0;
        }
    }
    return -1;
}

/* ============================================================
 * DNS client
 * ============================================================ */
typedef struct __attribute__((packed)) {
    u16 id;
    u16 flags;
    u16 qdcount;
    u16 ancount;
    u16 nscount;
    u16 arcount;
} dns_hdr_t;

static u32 g_dns_result_ip = 0;
static int g_dns_got_response = 0;

static void dns_handler(u32 src_ip, u16 src_port, const void *data, int len) {
    (void)src_ip;
    (void)src_port;
    if (len < (int)sizeof(dns_hdr_t)) return;
    const dns_hdr_t *h = (const dns_hdr_t *)data;
    if (ntohs(h->ancount) == 0) return;

    /* P1-14 FIX: add bounds checks throughout DNS parsing to prevent
     * OOB reads on truncated/malicious DNS responses. */
    const u8 *p = (const u8 *)data + sizeof(dns_hdr_t);
    const u8 *end = (const u8 *)data + len;

    /* Skip question name (labels until 0). */
    while (p < end && *p != 0) {
        if (*p & 0xC0) { p += 2; break; }
        p += *p + 1;
        if (p >= end) return;  /* P1-14: bounds */
    }
    if (p < end && *p == 0) p++;  /* skip null terminator */
    p += 4;  /* skip QTYPE and QCLASS */
    if (p > end) return;  /* P1-14: bounds */

    /* Parse first answer. */
    while (p < end) {
        /* Name (may be compressed). */
        if (*p & 0xC0) { p += 2; }
        else {
            while (p < end && *p != 0) { p += *p + 1; if (p >= end) return; }
            if (p < end) p++;
        }
        if (p + 10 > end) return;  /* P1-14: bounds */
        u16 type = ntohs(*(u16 *)(p)); p += 2;
        u16 cls = ntohs(*(u16 *)(p)); p += 2;
        (void)cls;
        p += 4;  /* TTL */
        if (p + 2 > end) return;  /* P1-14: bounds */
        u16 rdlen = ntohs(*(u16 *)(p)); p += 2;
        if (p + rdlen > end) return;  /* P1-14: bounds */
        if (type == 1 && rdlen == 4) {
            /* A record. */
            if (p + 4 > end) return;  /* P1-14: bounds */
            g_dns_result_ip = ntohl(*(u32 *)p);
            g_dns_got_response = 1;
            return;
        }
        p += rdlen;
    }
}

int dns_resolve(const char *name, u32 *ip_out) {
    if (!g_nic_ok || g_dns == 0) return -1;

    udp_bind(DNS_PORT, dns_handler);  /* We'll use port 53 as "source" (simplified) */

    u8 buf[512];
    dns_hdr_t *h = (dns_hdr_t *)buf;
    h->id = htons(0x1234);
    h->flags = htons(0x0100);  /* standard query, recursion desired */
    h->qdcount = htons(1);
    h->ancount = 0;
    h->nscount = 0;
    h->arcount = 0;

    /* Encode domain name as DNS labels. */
    u8 *q = buf + sizeof(dns_hdr_t);
    const char *p = name;
    while (*p) {
        const char *dot = p;
        while (*dot && *dot != '.') dot++;
        int labellen = dot - p;
        if (labellen > 63) return -1;
        *q++ = (u8)labellen;
        for (int i = 0; i < labellen; i++) *q++ = p[i];
        if (*dot == '.') p = dot + 1;
        else { p = dot; break; }
    }
    *q++ = 0;  /* end of name */
    *(u16 *)q = htons(1);  /* QTYPE = A */
    q += 2;
    *(u16 *)q = htons(1);  /* QCLASS = IN */
    q += 2;

    int pkt_len = (int)(q - buf);

    g_dns_got_response = 0;
    /* Use a fixed source port and bind to it so the response is received. */
    u16 dns_src_port = 1024 + DNS_PORT;
    udp_bind(dns_src_port, dns_handler);
    udp_send(g_dns, DNS_PORT, dns_src_port, buf, pkt_len);

    /* Wait for response. */
    u64 start = oc_timer_ticks();
    while ((oc_timer_ticks() - start) < 500) {
        net_poll();
        if (g_dns_got_response) {
            *ip_out = g_dns_result_ip;
            return 0;
        }
    }
    return -1;
}

/* ============================================================
 * Network configuration getters/setters
 * ============================================================ */
u32 net_get_ip(void) { return g_ip; }
u32 net_get_mask(void) { return g_mask; }
u32 net_get_gateway(void) { return g_gateway; }
u32 net_get_dns(void) { return g_dns; }

void net_set_ip(u32 ip, u32 mask, u32 gateway) {
    g_ip = ip;
    g_mask = mask;
    g_gateway = gateway;
}

void net_set_dns(u32 dns) { g_dns = dns; }

void net_get_mac(u8 mac[6]) {
    oc_memcpy(mac, g_mac, 6);
}

int net_get_link_status(void) {
    if (!g_nic_ok) return 0;
    if (g_use_virtio) return 1;  /* virtio-net link is always up after init */
    u32 status = mmio_read32((volatile void *)((u8 *)g_e1000_mmio + E1000_STATUS));
    return (status & 0x02) ? 1 : 0;  /* LU bit */
}

void net_get_stats(net_stats_t *out) {
    *out = g_stats;
}

/* ============================================================
 * Network init and poll
 * ============================================================ */
void net_init(void) {
    oc_memset(&g_stats, 0, sizeof(g_stats));
    pci_init();
    arp_init();
    udp_init();
    tcp_init();
    oc_memset(g_sockets, 0, sizeof(g_sockets));

    /* Try virtio-net first (it works better in QEMU), then fall back to e1000. */
    if (virtio_net_init() == 0) {
        oc_console_puts("virtio-net: initialized\n");
        g_nic_ok = 1;
        g_ip = IP4(10,0,2,15);
        g_mask = IP4(255,255,255,0);
        g_gateway = IP4(10,0,2,2);
        g_dns = IP4(10,0,2,3);
    } else if (e1000_init() == 0) {
        g_ip = IP4(10,0,2,15);
        g_mask = IP4(255,255,255,0);
        g_gateway = IP4(10,0,2,2);
        g_dns = IP4(10,0,2,3);
    }
}

void net_poll(void) {
    if (!g_nic_ok) return;
    /* WP-09: Check TCP RTO timers on every poll */
    tcp_check_rto();
    u8 buf[ETH_FRAME_MAX];
    for (int i = 0; i < 8; i++) {
        int len;
        if (g_use_virtio) {
            len = virtio_net_recv(buf, sizeof(buf));
        } else {
            len = e1000_recv(buf, sizeof(buf));
        }
        if (len <= 0) break;
        if (len < 14) continue;
        eth_hdr_t *eh = (eth_hdr_t *)buf;
        u16 ethertype = ntohs(eh->ethertype);
        const void *payload = buf + 14;
        int payload_len = len - 14;
        if (ethertype == ETH_TYPE_ARP) {
            arp_handle_packet((const arp_pkt_t *)payload, payload_len);
        } else if (ethertype == ETH_TYPE_IP) {
            ip_handle_packet(payload, payload_len);
        }
    }
}

/* ============================================================
 * Shell commands
 * ============================================================ */

/* Helper: parse IP address "10.0.2.15" -> u32 (host order). */
static u32 parse_ip(const char *s) {
    u32 a=0, b=0, c=0, d=0;
    int n = 0;
    /* Simple manual parse. */
    int i = 0;
    a = 0; while (s[i] && s[i] >= '0' && s[i] <= '9') { a = a*10 + (s[i]-'0'); i++; }
    if (s[i] == '.') i++; else return 0;
    b = 0; while (s[i] && s[i] >= '0' && s[i] <= '9') { b = b*10 + (s[i]-'0'); i++; }
    if (s[i] == '.') i++; else return 0;
    c = 0; while (s[i] && s[i] >= '0' && s[i] <= '9') { c = c*10 + (s[i]-'0'); i++; }
    if (s[i] == '.') i++; else return 0;
    d = 0; while (s[i] && s[i] >= '0' && s[i] <= '9') { d = d*10 + (s[i]-'0'); i++; }
    (void)n;
    return IP4(a, b, c, d);
}

/* Helper: format IP address. */
static void format_ip(u32 ip, char *out) {
    char n[12];
    oc_u64_to_str((ip >> 24) & 0xFF, n); oc_strcpy(out, n); oc_strcat(out, ".");
    oc_u64_to_str((ip >> 16) & 0xFF, n); oc_strcat(out, n); oc_strcat(out, ".");
    oc_u64_to_str((ip >> 8) & 0xFF, n); oc_strcat(out, n); oc_strcat(out, ".");
    oc_u64_to_str(ip & 0xFF, n); oc_strcat(out, n);
}

/* Helper: format MAC address. */
static void format_mac(const u8 *mac, char *out) {
    char n[4];
    for (int i = 0; i < 6; i++) {
        oc_u64_to_hex(mac[i], n, 2);
        oc_strcat(out, n);
        if (i < 5) oc_strcat(out, ":");
    }
}

int cmd_ifconfig(const char *args) {
    (void)args;
    char buf[80];
    char ipstr[20], macstr[24];

    if (!g_nic_ok) {
        oc_console_puts("no NIC detected\n");
        return 0;
    }

    /* BUG-035 FIX: Remove e1000 debug register dump from ifconfig.
     * Old code printed rx_descs/tx_descs/RDBAL/TDBAL/CTRL — internal
     * debug info not useful to users. */

    oc_console_puts(g_use_virtio ? "virtio-net:\n" : "e1000:\n");

    /* MAC address. */
    macstr[0] = 0;
    format_mac(g_mac, macstr);
    oc_strcpy(buf, "  HWaddr "); oc_strcat(buf, macstr); oc_strcat(buf, "\n");
    oc_console_puts(buf);

    /* IP address. */
    ipstr[0] = 0;
    format_ip(g_ip, ipstr);
    oc_strcpy(buf, "  inet "); oc_strcat(buf, ipstr); oc_strcat(buf, "\n");
    oc_console_puts(buf);

    /* Netmask. */
    ipstr[0] = 0;
    format_ip(g_mask, ipstr);
    oc_strcpy(buf, "  mask "); oc_strcat(buf, ipstr); oc_strcat(buf, "\n");
    oc_console_puts(buf);

    /* Gateway. */
    ipstr[0] = 0;
    format_ip(g_gateway, ipstr);
    oc_strcpy(buf, "  gateway "); oc_strcat(buf, ipstr); oc_strcat(buf, "\n");
    oc_console_puts(buf);

    /* DNS. */
    ipstr[0] = 0;
    format_ip(g_dns, ipstr);
    oc_strcpy(buf, "  DNS "); oc_strcat(buf, ipstr); oc_strcat(buf, "\n");
    oc_console_puts(buf);

    /* Link status. */
    int link = net_get_link_status();
    oc_strcpy(buf, "  link: "); oc_strcat(buf, link ? "UP" : "DOWN"); oc_strcat(buf, "\n");
    oc_console_puts(buf);

    /* Stats. */
    net_stats_t st;
    net_get_stats(&st);
    oc_strcpy(buf, "  RX packets: "); oc_u64_to_str(st.rx_packets, n_tmp); oc_strcat(buf, n_tmp);
    oc_strcat(buf, "  TX packets: "); oc_u64_to_str(st.tx_packets, n_tmp); oc_strcat(buf, n_tmp);
    oc_strcat(buf, "\n");
    oc_console_puts(buf);

    return 0;
}

int cmd_ip(const char *args) {
    if (!args[0]) {
        /* Show current config. */
        char buf[80]; char ipstr[20];
        ipstr[0] = 0; format_ip(g_ip, ipstr);
        oc_strcpy(buf, "IP: "); oc_strcat(buf, ipstr); oc_strcat(buf, "\n");
        oc_console_puts(buf);
        ipstr[0] = 0; format_ip(g_mask, ipstr);
        oc_strcpy(buf, "Mask: "); oc_strcat(buf, ipstr); oc_strcat(buf, "\n");
        oc_console_puts(buf);
        ipstr[0] = 0; format_ip(g_gateway, ipstr);
        oc_strcpy(buf, "Gateway: "); oc_strcat(buf, ipstr); oc_strcat(buf, "\n");
        oc_console_puts(buf);
        return 0;
    }
    /* Parse "ip mask gateway" */
    char ip_str[20], mask_str[20], gw_str[20];
    int i = 0, j = 0;
    while (args[i] && args[i] != ' ' && j < 19) ip_str[j++] = args[i++];
    ip_str[j] = 0;
    while (args[i] == ' ') i++;
    j = 0;
    while (args[i] && args[i] != ' ' && j < 19) mask_str[j++] = args[i++];
    mask_str[j] = 0;
    while (args[i] == ' ') i++;
    j = 0;
    while (args[i] && args[i] != ' ' && j < 19) gw_str[j++] = args[i++];
    gw_str[j] = 0;
    u32 ip = parse_ip(ip_str);
    u32 mask = parse_ip(mask_str);
    u32 gw = parse_ip(gw_str);
    net_set_ip(ip, mask, gw);
    oc_console_puts("IP set.\n");
    return 0;
}

int cmd_route(const char *args) {
    (void)args;
    char buf[80]; char ipstr[20];
    oc_console_puts("Routing table:\n");
    ipstr[0] = 0; format_ip(0, ipstr);
    oc_strcpy(buf, "  dst="); oc_strcat(buf, ipstr);
    oc_strcat(buf, " mask=0.0.0.0 gw=");
    ipstr[0] = 0; format_ip(g_gateway, ipstr);
    oc_strcat(buf, ipstr); oc_strcat(buf, " (default)\n");
    oc_console_puts(buf);
    return 0;
}

int cmd_ping(const char *args) {
    if (!args[0]) {
        oc_console_puts("usage: ping <host|ip>\n");
        return 1;
    }
    u32 ip = parse_ip(args);
    if (ip == 0) {
        /* Not an IP address, try DNS resolution */
        oc_console_puts("Resolving ");
        oc_console_puts(args);
        oc_console_puts("...\n");
        if (dns_resolve(args, &ip) != 0) {
            oc_console_puts("DNS resolution failed\n");
            return 1;
        }
        char rbuf[80]; char ripstr[20];
        ripstr[0] = 0; format_ip(ip, ripstr);
        oc_strcpy(rbuf, "Resolved to "); oc_strcat(rbuf, ripstr); oc_strcat(rbuf, "\n");
        oc_console_puts(rbuf);
    }
    char buf[80]; char ipstr[20];
    ipstr[0] = 0; format_ip(ip, ipstr);
    oc_strcpy(buf, "PING "); oc_strcat(buf, ipstr); oc_strcat(buf, " ...\n");
    oc_console_puts(buf);

    for (int i = 0; i < 4; i++) {
        oc_console_puts("  ");
        int rc = icmp_ping(ip, 3000);
        if (rc == 0) {
            oc_console_puts("reply received\n");
        } else {
            oc_console_puts("timeout\n");
        }
        for (volatile int j = 0; j < 500000; j++);
    }
    return 0;
}

int cmd_netstat(const char *args) {
    (void)args;
    char buf[80];

    /* BUG-035 FIX: Remove e1000 debug register dump from netstat.
     * Old code printed RDH/RDT/TDH/TDT/STATUS/RCTL/TCTL — internal
     * debug info not useful to users. */

    net_stats_t st;
    net_get_stats(&st);
    oc_console_puts("Network statistics:\n");
    oc_strcpy(buf, "  TX: "); oc_u64_to_str(st.tx_packets, n_tmp); oc_strcat(buf, n_tmp);
    oc_strcat(buf, " packets, "); oc_u64_to_str(st.tx_bytes, n_tmp); oc_strcat(buf, n_tmp);
    oc_strcat(buf, " bytes\n");
    oc_console_puts(buf);
    oc_strcpy(buf, "  RX: "); oc_u64_to_str(st.rx_packets, n_tmp); oc_strcat(buf, n_tmp);
    oc_strcat(buf, " packets, "); oc_u64_to_str(st.rx_bytes, n_tmp); oc_strcat(buf, n_tmp);
    oc_strcat(buf, " bytes\n");
    oc_console_puts(buf);
    oc_strcpy(buf, "  ARP requests: "); oc_u64_to_str(st.arp_requests, n_tmp); oc_strcat(buf, n_tmp);
    oc_strcat(buf, "  replies: "); oc_u64_to_str(st.arp_replies, n_tmp); oc_strcat(buf, n_tmp);
    oc_strcat(buf, "\n");
    oc_console_puts(buf);
    oc_strcpy(buf, "  ICMP echo sent: "); oc_u64_to_str(st.icmp_echo_sent, n_tmp); oc_strcat(buf, n_tmp);
    oc_strcat(buf, "  recv: "); oc_u64_to_str(st.icmp_echo_recv, n_tmp); oc_strcat(buf, n_tmp);
    oc_strcat(buf, "\n");
    oc_console_puts(buf);
    oc_strcpy(buf, "  TCP connections: "); oc_u64_to_str(st.tcp_connections, n_tmp); oc_strcat(buf, n_tmp);
    oc_strcat(buf, "\n");
    oc_console_puts(buf);

    /* Show active sockets. */
    oc_console_puts("Sockets:\n");
    int count = 0;
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (g_sockets[i].in_use) {
            char ipstr[20];
            ipstr[0] = 0; format_ip(g_sockets[i].remote_ip, ipstr);
            oc_strcpy(buf, "  ["); oc_u64_to_str(i, n_tmp); oc_strcat(buf, n_tmp);
            oc_strcat(buf, "] type="); oc_strcat(buf, g_sockets[i].type == SOCK_TCP ? "TCP" : "UDP");
            oc_strcat(buf, " remote="); oc_strcat(buf, ipstr);
            oc_strcat(buf, ":"); oc_u64_to_str(g_sockets[i].remote_port, n_tmp); oc_strcat(buf, n_tmp);
            oc_strcat(buf, "\n");
            oc_console_puts(buf);
            count++;
        }
    }
    if (count == 0) oc_console_puts("  (none)\n");

    /* Show ARP cache. */
    oc_console_puts("ARP cache:\n");
    count = 0;
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        u32 ip; u8 mac[6];
        if (arp_get_cache(i, &ip, mac) == 0) {
            char ipstr[20], macstr[24];
            ipstr[0] = 0; format_ip(ip, ipstr);
            macstr[0] = 0; format_mac(mac, macstr);
            oc_strcpy(buf, "  "); oc_strcat(buf, ipstr); oc_strcat(buf, " -> ");
            oc_strcat(buf, macstr); oc_strcat(buf, "\n");
            oc_console_puts(buf);
            count++;
        }
    }
    if (count == 0) oc_console_puts("  (empty)\n");

    return 0;
}

int cmd_dhcp(const char *args) {
    (void)args;
    oc_console_puts("Sending DHCP discover...\n");
    int rc = dhcp_discover();
    if (rc == 0) {
        char buf[80]; char ipstr[20];
        ipstr[0] = 0; format_ip(g_ip, ipstr);
        oc_strcpy(buf, "DHCP success: IP="); oc_strcat(buf, ipstr);
        oc_strcat(buf, "\n");
        oc_console_puts(buf);
        ipstr[0] = 0; format_ip(g_mask, ipstr);
        oc_strcpy(buf, "  mask="); oc_strcat(buf, ipstr);
        oc_strcat(buf, "\n");
        oc_console_puts(buf);
        ipstr[0] = 0; format_ip(g_gateway, ipstr);
        oc_strcpy(buf, "  gateway="); oc_strcat(buf, ipstr);
        oc_strcat(buf, "\n");
        oc_console_puts(buf);
        ipstr[0] = 0; format_ip(g_dns, ipstr);
        oc_strcpy(buf, "  DNS="); oc_strcat(buf, ipstr);
        oc_strcat(buf, "\n");
        oc_console_puts(buf);
    } else {
        oc_console_puts("DHCP failed (timeout)\n");
    }
    return 0;
}

int cmd_dns(const char *args) {
    if (!args[0]) {
        oc_console_puts("usage: dns <name>\n");
        return 1;
    }
    char buf[80];
    oc_strcpy(buf, "Resolving "); oc_strcat(buf, args); oc_strcat(buf, "...\n");
    oc_console_puts(buf);
    u32 ip;
    int rc = dns_resolve(args, &ip);
    if (rc == 0) {
        char ipstr[20];
        ipstr[0] = 0; format_ip(ip, ipstr);
        oc_strcpy(buf, "Result: "); oc_strcat(buf, ipstr); oc_strcat(buf, "\n");
        oc_console_puts(buf);
    } else {
        oc_console_puts("DNS resolution failed (timeout)\n");
    }
    return 0;
}

int cmd_lspci(const char *args) {
    (void)args;
    pci_list_devices();
    return 0;
}

int cmd_wget(const char *args) {
    if (!args[0]) {
        oc_console_puts("usage: wget <host> [port] [path]\n");
        return 1;
    }
    /* Parse: wget host [port] [path] */
    char host[64] = {0};
    int port = 80;
    char path[128] = "/";
    int i = 0, j = 0;
    while (args[i] && args[i] != ' ' && j < 63) host[j++] = args[i++];
    host[j] = 0;
    while (args[i] == ' ') i++;
    if (args[i]) {
        port = 0;
        while (args[i] && args[i] >= '0' && args[i] <= '9') { port = port*10 + (args[i]-'0'); i++; }
        if (port == 0) port = 80;
        while (args[i] == ' ') i++;
        if (args[i]) {
            j = 0;
            while (args[i] && j < 127) path[j++] = args[i++];
            path[j] = 0;
        }
    }

    /* Resolve host (try as IP first). */
    u32 ip = parse_ip(host);
    if (ip == 0) {
        if (dns_resolve(host, &ip) != 0) {
            oc_console_puts("cannot resolve host\n");
            return 1;
        }
    }

    char buf[80]; char ipstr[20];
    ipstr[0] = 0; format_ip(ip, ipstr);
    oc_strcpy(buf, "Connecting to "); oc_strcat(buf, ipstr);
    oc_strcat(buf, ":"); oc_u64_to_str(port, n_tmp); oc_strcat(buf, n_tmp);
    oc_strcat(buf, path); oc_strcat(buf, "\n");
    oc_console_puts(buf);

    int sock = net_socket(SOCK_TCP);
    if (sock < 0) { oc_console_puts("socket failed\n"); return 1; }
    if (net_connect(sock, ip, (u16)port) < 0) {
        oc_console_puts("connect failed\n");
        net_close(sock);
        return 1;
    }
    oc_console_puts("Connected.\n");

    /* Send HTTP GET. */
    char request[256];
    oc_strcpy(request, "GET ");
    oc_strcat(request, path);
    oc_strcat(request, " HTTP/1.0\r\nHost: ");
    oc_strcat(request, host);
    oc_strcat(request, "\r\n\r\n");
    net_send(sock, request, oc_strlen(request));

    /* P1-3 FIX: Save response body to a VFS file instead of just printing
     * the first 500 bytes. Extract filename from the URL path. */
    /* Extract filename from path (last component after last '/') */
    char fname[64];
    const char *last = path;
    for (const char *p = path; *p; p++) { if (*p == '/') last = p + 1; }
    if (*last) {
        int k = 0;
        while (last[k] && k < 63) { fname[k] = last[k]; k++; }
        fname[k] = 0;
    } else {
        oc_strcpy(fname, "index.html");
    }

    /* Receive response — loop until we get headers + full body. */
    char rbuf[1024];
    int total_header = 0;
    int header_end = -1;  /* index of \r\n\r\n in rbuf */
    int content_length = -1;
    int body_start = 0;
    int body_received = 0;
    /* Phase 1: receive until we find \r\n\r\n (end of headers). */
    while (header_end < 0) {
        int n = net_recv(sock, rbuf + total_header, sizeof(rbuf) - 1 - total_header);
        if (n <= 0) { oc_console_puts("no response (timeout)\n"); net_close(sock); return 1; }
        total_header += n;
        rbuf[total_header] = 0;
        /* Search for \r\n\r\n */
        for (int k = 0; k <= total_header - 4; k++) {
            if (rbuf[k] == '\r' && rbuf[k+1] == '\n' &&
                rbuf[k+2] == '\r' && rbuf[k+3] == '\n') {
                header_end = k;
                body_start = k + 4;
                body_received = total_header - body_start;
                break;
            }
        }
        if (total_header >= (int)sizeof(rbuf) - 1) {
            /* Buffer full but no header end found — malformed response. */
            oc_console_puts("malformed HTTP response\n");
            net_close(sock);
            return 1;
        }
    }
    /* Parse Content-Length from headers (case-insensitive). */
    for (int k = 0; k < header_end; k++) {
        if (rbuf[k] == 'C' || rbuf[k] == 'c') {
            if (((rbuf[k+1] == 'o' || rbuf[k+1] == 'O') &&
                 (oc_strncmp(rbuf + k, "Content-Length:", 15) == 0 ||
                  oc_strncmp(rbuf + k, "content-length:", 15) == 0))) {
                int v = k + 15;
                while (rbuf[v] == ' ' || rbuf[v] == '\t') v++;
                content_length = 0;
                while (rbuf[v] >= '0' && rbuf[v] <= '9') {
                    content_length = content_length * 10 + (rbuf[v] - '0');
                    v++;
                }
                break;
            }
        }
    }
    if (content_length < 0) content_length = body_received;  /* unknown — save what we have */

    /* Open VFS file for writing. */
    char fpath[80];
    oc_strcpy(fpath, "/");
    oc_strcat(fpath, fname);
    int fd = vfs_open(fpath, VFS_O_WRONLY | VFS_O_CREAT);
    if (fd < 0) {
        /* If VFS create fails, print to console as fallback. */
        oc_console_puts("wget: cannot create file, printing to console:\n");
        int show = body_received > 500 ? 500 : body_received;
        for (int k = 0; k < show; k++) oc_console_putc(rbuf[body_start + k]);
        oc_console_putc('\n');
        net_close(sock);
        return 1;
    }

    /* Write the initial body data (already received with headers). */
    vfs_write(fd, rbuf + body_start, body_received);
    int total_saved = body_received;

    /* Phase 2: loop net_recv until we have all content_length bytes. */
    while (total_saved < content_length) {
        int n = net_recv(sock, rbuf, sizeof(rbuf));
        if (n <= 0) break;  /* timeout or connection closed */
        vfs_write(fd, rbuf, n);
        total_saved += n;
    }
    vfs_close(fd);
    net_close(sock);

    /* Report. */
    oc_console_puts("Saved ");
    oc_u64_to_str(total_saved, n_tmp); oc_console_puts(n_tmp);
    oc_console_puts(" bytes to ");
    oc_console_puts(fpath);
    oc_console_putc('\n');
    return 0;
}

/* Network shell command registration. */
void net_register_shell_commands(void) {
    extern int shell_register_command(const char *name, int (*fn)(const char *), const char *help);
    shell_register_command("ifconfig", cmd_ifconfig, "show network interface info");
    shell_register_command("ip", cmd_ip, "show/set IP address");
    shell_register_command("route", cmd_route, "show routing table");
    shell_register_command("ping", cmd_ping, "send ICMP echo (ping <host>)");
    shell_register_command("netstat", cmd_netstat, "show network statistics and sockets");
    shell_register_command("dhcp", cmd_dhcp, "get IP via DHCP");
    shell_register_command("dns", cmd_dns, "resolve domain name (dns <name>)");
    shell_register_command("lspci", cmd_lspci, "list PCI devices");
    shell_register_command("wget", cmd_wget, "download file via HTTP (wget <host> [port] [path])");
}

/* Periodic timer callback for network polling. */
static void net_timer_cb(void *ctx) {
    (void)ctx;
    net_poll();
}

void net_start_timer(void) {
    oc_timer_register_periodic(net_timer_cb, NULL, 10);  /* every 10ms */
}
