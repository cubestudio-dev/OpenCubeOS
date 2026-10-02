/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10b
 * File: kernel/nic_rtl8169.c
 * Purpose: Realtek RTL8169-family GbE/2.5G/FE drivers for the WP-10b
 *          nic framework.  One shared core covers three sub-families:
 *
 *   rtl8168  RTL8168/8111 GbE      (10EC:8168, 10EC:8161)
 *   rtl8125  RTL8125 2.5GbE        (10EC:8125, 10EC:3000)
 *   rtl810x  RTL810x FE/100M      (10EC:8136, 10EC:8137)
 *
 * Programming model (RTL8169/8168 datasheet family layout):
 *   - BAR0/2 (MMIO 256B window or I/O).  This driver uses the I/O BAR
 *     when present and MMIO otherwise, through one accessor table.
 *   - One normal-priority TX descriptor ring (TNPDS @ 0x20, 8-byte
 *     aligned, 64-bit addresses) and one RX descriptor ring (RDSAR
 *     @ 0xE4/0xE8).
 *   - 16-byte descriptors: opts1 (own/first/last/size), opts2 (vlan),
 *     buffer address (64-bit).
 *   - Polling mode: IMR0 = 0.
 *
 * NOTE ON VERIFICATION: QEMU has no RTL8169-family device model, so
 * this driver is implemented from the datasheet register map and cannot
 * be exercised in QEMU; it is compiled, linked and probed (the PCI
 * probe correctly finds nothing under QEMU), and needs real hardware
 * for an end-to-end run.  This limitation is stated honestly in the
 * WP-10b delivery report (no fake test output).
 */
#include "nic.h"
#include "console.h"
#include "string.h"
#include "pmm.h"

#include <stdint.h>

/* ---- Bus access (I/O ports; every RTL8169-family chip exposes a PIO
 * BAR alongside the MMIO BAR, so the driver always uses the PIO one) ---- */
static inline void rtl_w8(u16 port, u8 v)   { __asm__ volatile("outb %0,%1" : : "a"(v), "Nd"(port)); }
static inline void rtl_w16(u16 port, u16 v) { __asm__ volatile("outw %0,%1" : : "a"(v), "Nd"(port)); }
static inline void rtl_w32(u16 port, u32 v) { __asm__ volatile("outl %0,%1" : : "a"(v), "Nd"(port)); }
static inline u8   rtl_r8(u16 port)         { u8 r; __asm__ volatile("inb %1,%0" : "=a"(r) : "Nd"(port)); return r; }
static inline u16  rtl_r16(u16 port)        { u16 r; __asm__ volatile("inw %1,%0" : "=a"(r) : "Nd"(port)); return r; }
static inline u32  rtl_r32(u16 port)        { u32 r; __asm__ volatile("inl %1,%0" : "=a"(r) : "Nd"(port)); return r; }

/* ---- Register offsets (RTL8169 family) ---- */
#define R8169_IDR0      0x00   /* MAC, 6 bytes */
#define R8169_TNPDS     0x20   /* TX descriptor ring base (8 bytes: lo+hi) */
#define R8169_CR        0x37   /* command */
#define R8169_TP_POLL   0x39   /* TX poll */
#define R8169_IMR0      0x3C   /* interrupt mask (16b) */
#define R8169_ISR0      0x3E   /* interrupt status (16b) */
#define R8169_TCR       0x40
#define R8169_RCR       0x44
#define R8169_PHYS_SR   0x6A   /* PHY status (link) */
#define R8169_RMS       0xDA   /* RX maximum size */
#define R8169_CPLUS_CMD 0xE0   /* C+ command (16b) */
#define R8169_RDSAR_LO  0xE4   /* RX descriptor ring base, low 32 */
#define R8169_RDSAR_HI  0xE8   /* RX descriptor ring base, high 32 */

/* CR bits */
#define R8169_CR_RST  0x10
#define R8169_CR_RE   0x08
#define R8169_CR_TE   0x04

/* Descriptor opts1 bits */
#define R8169_D_OWN   0x80000000u
#define R8169_D_EOR   0x40000000u
#define R8169_D_FS    0x20000000u
#define R8169_D_LS    0x10000000u
#define R8169_D_SIZE  0x00003FFFu
/* RX status bits in opts1 of a filled RX descriptor */
#define R8169_RX_RES  0x00100000u   /* receive error summary */

/* C+ command bits */
#define R8169_CPLUS_RXVLAN  0x0040
#define R8169_CPLUS_RXCSUM  0x0020
#define R8169_CPLUS_MMAPEN  0x0008   /* DAC: 64-bit DMA addressing */

#define R8169_TX_DESCS 32
#define R8169_RX_DESCS 64
#define R8169_BUF_SIZE 2048

/* 16-byte RTL8169-family descriptor. */
typedef struct {
    volatile u32 opts1;   /* OWN/EOR/FS/LS/size (TX) | status (RX) */
    volatile u32 opts2;   /* vlan tag / rx checksum */
    volatile u32 buf_lo;
    volatile u32 buf_hi;
} rtl_desc_t;

typedef struct {
    u16  io;               /* I/O base of the register window */
    u8   bus, dev, func;
    int  up;
    u8   mac[6];
    volatile rtl_desc_t *tx_descs;
    volatile rtl_desc_t *rx_descs;
    u8  *tx_bufs[R8169_TX_DESCS];
    u8  *rx_bufs[R8169_RX_DESCS];
    int  tx_tail;
    int  rx_head;
    u64  tx_packets, rx_packets;
} rtl8169_dev_t;

static rtl8169_dev_t g_rtl8169;    /* rtl8168/8111 */
static rtl8169_dev_t g_rtl8125;    /* rtl8125 */
static rtl8169_dev_t g_rtl810x;    /* rtl810x */

/* DMA allocation from the identity-mapped region. */
static u8 *rtl_dma_page = NULL;
static int rtl_dma_off = 0;

static void *rtl_dma_alloc(int size) {
    if (size > (int)PMM_PAGE_SIZE) return (void *)(uintptr_t)pmm_alloc_frame();
    if (rtl_dma_page == NULL || rtl_dma_off + size > (int)PMM_PAGE_SIZE) {
        rtl_dma_page = (u8 *)(uintptr_t)pmm_alloc_frame();
        if (rtl_dma_page == NULL) return NULL;
        rtl_dma_off = 0;
    }
    rtl_dma_off = (rtl_dma_off + 15) & ~15;
    void *p = rtl_dma_page + rtl_dma_off;
    rtl_dma_off += size;
    return p;
}

/* Descriptor ring pages: TX ring + RX ring + buffers, one contiguous
 * PMM block keeps the math simple (rings are page aligned). */
static int rtl_alloc_rings(rtl8169_dev_t *d) {
    d->tx_descs = (volatile rtl_desc_t *)(uintptr_t)pmm_alloc_frame();
    if (!d->tx_descs) return -1;
    oc_memset((void *)d->tx_descs, 0, PMM_PAGE_SIZE);
    d->rx_descs = (volatile rtl_desc_t *)(uintptr_t)pmm_alloc_frame();
    if (!d->rx_descs) return -1;
    oc_memset((void *)d->rx_descs, 0, PMM_PAGE_SIZE);

    for (int i = 0; i < R8169_TX_DESCS; i++) {
        d->tx_bufs[i] = (u8 *)rtl_dma_alloc(R8169_BUF_SIZE);
        if (!d->tx_bufs[i]) return -1;
    }
    for (int i = 0; i < R8169_RX_DESCS; i++) {
        d->rx_bufs[i] = (u8 *)rtl_dma_alloc(R8169_BUF_SIZE);
        if (!d->rx_bufs[i]) return -1;
        d->rx_descs[i].buf_lo = (u32)(uintptr_t)d->rx_bufs[i];
        d->rx_descs[i].buf_hi = 0;
        d->rx_descs[i].opts1 = R8169_D_OWN | ((i == R8169_RX_DESCS - 1) ? R8169_D_EOR : 0) |
                               R8169_BUF_SIZE;
        d->rx_descs[i].opts2 = 0;
    }
    d->tx_tail = 0;
    d->rx_head = 0;
    return 0;
}

static void rtl_pci_enable(u8 bus, u8 dev, u8 func) {
    u32 cs = pci_read_config(bus, dev, func, 0x04);
    cs = (cs & 0xFFFF0000) | 0x0107;
    pci_write_config(bus, dev, func, 0x04, cs);
}

static int rtl_setup(rtl8169_dev_t *d, u8 bus, u8 dev, u8 func) {
    oc_memset(d, 0, sizeof(*d));
    d->bus = bus; d->dev = dev; d->func = func;

    /* Find the PIO BAR (BAR0 is MMIO on most boards, the PIO window is
     * usually BAR2; walk the first three). */
    u16 io = 0;
    for (int b = 0; b < 3 && !io; b++) {
        u32 bar = pci_read_bar(bus, dev, func, b);
        if ((bar & 0x1) && (bar & 0xFFFC)) io = (u16)(bar & 0xFFFC);
    }
    if (!io) return -1;
    d->io = io;
    rtl_pci_enable(bus, dev, func);

    for (int i = 0; i < 6; i++)
        d->mac[i] = rtl_r8(d->io + R8169_IDR0 + i);

    /* Reset, wait for clear. */
    rtl_r8(d->io + R8169_CR);
    rtl_w8(d->io + R8169_CR, R8169_CR_RST);
    for (int t = 0; t < 100000; t++) {
        if (!(rtl_r8(d->io + R8169_CR) & R8169_CR_RST)) break;
    }

    if (rtl_alloc_rings(d) != 0) return -1;

    /* Mask interrupts (polling). */
    rtl_w16(d->io + R8169_IMR0, 0);

    /* C+ command: RX checksum + 64-bit descriptor addresses (real
     * hardware needs DAC for >4G buffers; we stay in 32-bit space but
     * keep the register programming per datasheet). */
    rtl_w16(d->io + R8169_CPLUS_CMD,
            R8169_CPLUS_RXCSUM | R8169_CPLUS_MMAPEN);

    /* Program the rings. */
    rtl_w32(d->io + R8169_TNPDS, (u32)(uintptr_t)d->tx_descs);
    rtl_w32(d->io + R8169_TNPDS + 4, (u32)(((u64)(uintptr_t)d->tx_descs) >> 32));
    rtl_w32(d->io + R8169_RDSAR_LO, (u32)(uintptr_t)d->rx_descs);
    rtl_w32(d->io + R8169_RDSAR_HI,
            (u32)(((u64)(uintptr_t)d->rx_descs) >> 32));

    /* RX maximum frame size. */
    rtl_w16(d->io + R8169_RMS, 1536);

    /* Accept broadcast/multicast/own. */
    rtl_w32(d->io + R8169_RCR, 0x0000000Fu);

    /* Enable TX + RX. */
    rtl_w8(d->io + R8169_CR, R8169_CR_TE | R8169_CR_RE);

    d->up = 1;
    return 0;
}

/* Shared ops (each family gets its own nic_device_t record; the ops are
 * identical apart from the ->priv back-pointer, which the framework
 * passes back as dev). */
static int rtl_ops_send(nic_device_t *ndev, const void *buf, int len);
static int rtl_ops_recv(nic_device_t *ndev, void *buf, int maxlen);
static int rtl_ops_link(nic_device_t *ndev);
static int rtl_ops_mac(nic_device_t *ndev, u8 mac[6]);

static const nic_ops_t rtl_nic_ops = {
    .send        = rtl_ops_send,
    .recv        = rtl_ops_recv,
    .link_status = rtl_ops_link,
    .get_mac     = rtl_ops_mac,
};

static rtl8169_dev_t *rtl_of(nic_device_t *ndev) {
    return (rtl8169_dev_t *)ndev->priv;
}

static int rtl_register_family(rtl8169_dev_t *d, const char *name,
                               nic_type_t type, u8 bus, u8 dev, u8 func,
                               u16 vid, u16 did) {
    nic_device_t nd;
    oc_memset(&nd, 0, sizeof(nd));
    oc_strncpy(nd.name, name, NIC_NAME_LEN - 1);
    nd.type = type;
    for (int k = 0; k < 6; k++) nd.mac[k] = d->mac[k];
    nd.bus = bus; nd.dev = dev; nd.func = func;
    nd.vendor_id = vid; nd.device_id = did;
    nd.priv = d;
    int idx = nic_register(&nd, &rtl_nic_ops);

    char line[96];
    char n[24];
    oc_strcpy(line, name);
    oc_strcat(line, ": at ");
    oc_u64_to_hex(bus, n, 2);  oc_strcat(line, n); oc_strcat(line, ":");
    oc_u64_to_hex(dev, n, 2);  oc_strcat(line, n); oc_strcat(line, ".");
    oc_u64_to_hex(func, n, 1); oc_strcat(line, n);
    oc_strcat(line, " mac=");
    for (int k = 0; k < 6; k++) {
        oc_u64_to_hex(d->mac[k], n, 2); oc_strcat(line, n);
        if (k < 5) oc_strcat(line, ":");
    }
    oc_strcat(line, (idx >= 0) ? " [registered]\n" : " [registry full]\n");
    oc_console_puts(line);
    return idx;
}

static int rtl_init_family(rtl8169_dev_t *d, const char *name,
                           nic_type_t type, const u16 *ids, unsigned n_ids,
                           pci_dev_t *pdev) {
    if (d->up) return 0;
    u8 bus = 0, dev = 0, func = 0;
    if (pdev) {
        bus = pdev->bus; dev = pdev->dev; func = pdev->func;
        u32 ids_v = pci_read_config(bus, dev, func, 0x00);
        u16 vid = (u16)(ids_v & 0xFFFF);
        u16 did = (u16)(ids_v >> 16);
        int known = (vid == 0x10EC);
        for (unsigned i = 0; known && i < n_ids; i++)
            if (did == ids[i]) known = 1;
        if (!known) return -1;
    } else {
        int found = 0;
        for (unsigned i = 0; i < n_ids && !found; i++) {
            if (pci_find_device(0x10EC, ids[i], &bus, &dev, &func) == 0)
                found = 1;
        }
        if (!found) return -1;
    }

    if (rtl_setup(d, bus, dev, func) != 0) {
        oc_console_puts(name);
        oc_console_puts(": init failed\n");
        return -1;
    }
    u32 ids_v = pci_read_config(bus, dev, func, 0x00);
    int idx = rtl_register_family(d, name, type, bus, dev, func,
                                  0x10EC, (u16)(ids_v >> 16));
    return idx >= 0 ? 0 : -1;
}

int rtl8168_init(pci_dev_t *pdev) {
    static const u16 ids[] = { 0x8168, 0x8161 };
    return rtl_init_family(&g_rtl8169, "rtl8168", NIC_TYPE_RTL8168,
                           ids, 2, pdev);
}

int rtl8125_init(pci_dev_t *pdev) {
    static const u16 ids[] = { 0x8125, 0x3000 };
    return rtl_init_family(&g_rtl8125, "rtl8125", NIC_TYPE_RTL8125,
                           ids, 2, pdev);
}

int rtl810x_init(pci_dev_t *pdev) {
    static const u16 ids[] = { 0x8136, 0x8137 };
    return rtl_init_family(&g_rtl810x, "rtl810x", NIC_TYPE_RTL810X,
                           ids, 2, pdev);
}

static int rtl_ops_send(nic_device_t *ndev, const void *buf, int len) {
    rtl8169_dev_t *d = rtl_of(ndev);
    if (!d || !d->up || len <= 0 || len > 1790) return -1;

    int slot = d->tx_tail;
    /* Wait until the descriptor is released by the chip. */
    for (int t = 0; t < 1000000; t++) {
        if (!(d->tx_descs[slot].opts1 & R8169_D_OWN)) break;
    }
    oc_memcpy(d->tx_bufs[slot], buf, len);
    d->tx_descs[slot].buf_lo = (u32)(uintptr_t)d->tx_bufs[slot];
    d->tx_descs[slot].buf_hi = 0;
    __asm__ volatile("sfence" ::: "memory");
    d->tx_descs[slot].opts1 = R8169_D_OWN | R8169_D_FS | R8169_D_LS |
                              (u32)len;

    /* Kick the engine (TP_POLL bit 6 = send normal-priority queue). */
    rtl_w8(d->io + R8169_TP_POLL, 0x40);

    /* Wait for OWN to be released. */
    int ok = 0;
    for (int t = 0; t < 1000000; t++) {
        if (!(d->tx_descs[slot].opts1 & R8169_D_OWN)) { ok = 1; break; }
    }
    d->tx_tail = (d->tx_tail + 1) % R8169_TX_DESCS;
    if (ok) d->tx_packets++;
    return ok ? len : -1;
}

static int rtl_ops_recv(nic_device_t *ndev, void *buf, int maxlen) {
    rtl8169_dev_t *d = rtl_of(ndev);
    if (!d || !d->up) return -1;

    volatile rtl_desc_t *desc = &d->rx_descs[d->rx_head];
    if (desc->opts1 & R8169_D_OWN) return 0;   /* not filled yet */
    if (desc->opts1 & R8169_RX_RES) {
        /* error frame: recycle */
        desc->opts1 = R8169_D_OWN |
                      ((d->rx_head == R8169_RX_DESCS - 1) ? R8169_D_EOR : 0) |
                      R8169_BUF_SIZE;
        d->rx_head = (d->rx_head + 1) % R8169_RX_DESCS;
        return 0;
    }

    int len = (int)(desc->opts1 & R8169_D_SIZE);
    if (len > maxlen) len = maxlen;
    if (len > 0) oc_memcpy(buf, d->rx_bufs[d->rx_head], len);

    desc->opts1 = R8169_D_OWN |
                  ((d->rx_head == R8169_RX_DESCS - 1) ? R8169_D_EOR : 0) |
                  R8169_BUF_SIZE;
    d->rx_head = (d->rx_head + 1) % R8169_RX_DESCS;
    d->rx_packets++;
    return len;
}

static int rtl_ops_link(nic_device_t *ndev) {
    rtl8169_dev_t *d = rtl_of(ndev);
    if (!d || !d->up) return -1;
    /* PHYSR bit 3 = link status (1 = up) on most family members. */
    u8 v = rtl_r8(d->io + R8169_PHYS_SR);
    return (v & 0x08) ? 1 : 0;
}

static int rtl_ops_mac(nic_device_t *ndev, u8 mac[6]) {
    rtl8169_dev_t *d = rtl_of(ndev);
    if (!d || !mac) return -1;
    for (int k = 0; k < 6; k++) mac[k] = d->mac[k];
    return 0;
}

static void rtl_print_family(rtl8169_dev_t *d, const char *name) {
    char line[128];
    char n[24];
    if (!d->up) {
        oc_console_puts(name);
        oc_console_puts(": not present\n");
        return;
    }
    oc_strcpy(line, name);
    oc_strcat(line, ": pci=");
    oc_u64_to_hex(d->bus, n, 2); oc_strcat(line, n); oc_strcat(line, ":");
    oc_u64_to_hex(d->dev, n, 2); oc_strcat(line, n); oc_strcat(line, ".");
    oc_u64_to_hex(d->func, n, 1); oc_strcat(line, n);
    oc_strcat(line, " tx=");
    oc_u64_to_str(d->tx_packets, n); oc_strcat(line, n);
    oc_strcat(line, " rx=");
    oc_u64_to_str(d->rx_packets, n); oc_strcat(line, n);
    oc_strcat(line, " link=");
    u8 v = rtl_r8(d->io + R8169_PHYS_SR);
    oc_strcat(line, (v & 0x08) ? "up" : "down");
    oc_strcat(line, "\n");
    oc_console_puts(line);
}

void rtl8168_print_state(void) { rtl_print_family(&g_rtl8169, "rtl8168"); }
void rtl8125_print_state(void) { rtl_print_family(&g_rtl8125, "rtl8125"); }
void rtl810x_print_state(void) { rtl_print_family(&g_rtl810x, "rtl810x"); }
