/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10b
 * File: kernel/nic_bcm57xx.c
 * Purpose: Broadcom BCM57xx / BCM440x GbE driver for the WP-10b nic
 *          framework.
 *
 * Hardware covered:
 *   14E4:1644  BCM5721 / 57xx series (Tigon3 MAC core)
 *   14E4:1653  BCM5705M (laptops)
 *   14E4:1673  BCM5751M/5751
 *   14E4:1677  BCM5752
 *   14E4:169B? -> not used; the table below is the safe set
 *   14E4:170C  BCM4401-B0 (BCM440x family, 100M)
 *   14E4:4401  BCM4401  (BCM440x)
 *
 * Programming model (Tigon3 register map, as documented in the BCM5700
 * series datasheet and mirrored by the Linux tg3 layout):
 *   - BAR0 MMIO 32 KiB register space.
 *   - MAC_MODE (0x0400): bit1 ENABLE_TX, bit2 ENABLE_RX.
 *   - MAC_ADDR_HI (0x0410) / MAC_ADDR_LO (0x0414): station address.
 *   - Standard RX/TX producer/consumer mailboxes plus host-resident
 *     descriptor rings (RCB programming), TX_MBOX0 (0x3200),
 *     RX_STD_PROD (0x2620).
 *   - 8-byte TX descriptors, 8-byte RX return descriptors.
 *
 * VERIFICATION NOTE (honest disclosure): QEMU has no BCM57xx device
 * model and this sandbox has no real Broadcom NIC, so this driver is
 * compiled, linked and PCI-probed (finding nothing under QEMU), but its
 * datapath has NOT been exercised.  Register values below are derived
 * from the datasheet/Tigon3 layout, not from a live run.
 */
#include "nic.h"
#include "console.h"
#include "string.h"
#include "pmm.h"

#include <stdint.h>

static inline u32 mmio_read32(volatile void *p) { return *(volatile u32 *)p; }
static inline void mmio_write32(volatile void *p, u32 v) { *(volatile u32 *)p = v; }

/* Tigon3 register offsets (BCM57xx datasheet / tg3 layout). */
#define BCM_MAC_MODE      0x0400
#define BCM_MAC_STATUS    0x0404
#define BCM_MAC_ADDR_HI   0x0410
#define BCM_MAC_ADDR_LO   0x0414
#define BCM_TX_MBOX0      0x3200   /* TX producer ring 0 mailbox (tail index) */
#define BCM_RX_STD_PROD   0x2620   /* standard RX ring producer index */
#define BCM_RCV_RING_CFG  0x4540   /* std RX ring control block: host addr */
#define BCM_GRC_MODE      0x6800

/* MAC_MODE bits */
#define BCM_MAC_MODE_TX_EN   (1u << 1)
#define BCM_MAC_MODE_RX_EN   (1u << 2)
/* MAC_STATUS bit 2 = signal detect (link) */
#define BCM_MAC_STATUS_LNK   (1u << 2)

#define BCM_TX_DESCS   32
#define BCM_RX_DESCS   64
#define BCM_BUF_SIZE   2048

/* TX descriptor (Tigon3: 8 bytes, 32-bit addressing). */
typedef struct {
    u32 addr;
    u32 len_flags;    /* bits 0-15 length; bit 31 end-of-frame */
} __attribute__((packed)) bcm_tx_desc_t;

/* RX return descriptor (8 bytes). */
typedef struct {
    u32 status;       /* bits 0-15 length; bit 12? error; bit 31? */
    u32 idx_flags;
} __attribute__((packed)) bcm_rx_desc_t;

typedef struct {
    volatile u8 *mmio;
    u8   bus, dev, func;
    int  up;
    u8   mac[6];
    volatile bcm_tx_desc_t *tx_descs;
    volatile bcm_rx_desc_t *rx_descs;
    u8  *tx_bufs[BCM_TX_DESCS];
    u8  *rx_bufs[BCM_RX_DESCS];
    int  tx_tail;
    int  rx_head;
    u64  tx_packets, rx_packets;
} bcm_dev_t;

static bcm_dev_t g_bcm;

static const struct { u16 vid; u16 did; } bcm_ids[] = {
    { 0x14E4, 0x1644 }, { 0x14E4, 0x1653 }, { 0x14E4, 0x1673 },
    { 0x14E4, 0x1677 }, { 0x14E4, 0x4401 }, { 0x14E4, 0x170C },
};

static u8 *bcm_dma_page = NULL;
static int bcm_dma_off = 0;

static void *bcm_dma_alloc(int size) {
    if (size > (int)PMM_PAGE_SIZE) return (void *)(uintptr_t)pmm_alloc_frame();
    if (bcm_dma_page == NULL || bcm_dma_off + size > (int)PMM_PAGE_SIZE) {
        bcm_dma_page = (u8 *)(uintptr_t)pmm_alloc_frame();
        if (bcm_dma_page == NULL) return NULL;
        bcm_dma_off = 0;
    }
    bcm_dma_off = (bcm_dma_off + 15) & ~15;
    void *p = bcm_dma_page + bcm_dma_off;
    bcm_dma_off += size;
    return p;
}

static u32 bcm_reg(bcm_dev_t *d, u32 off) {
    return mmio_read32((volatile void *)(d->mmio + off));
}
static void bcm_wreg(bcm_dev_t *d, u32 off, u32 v) {
    mmio_write32((volatile void *)(d->mmio + off), v);
}

static void bcm_pci_enable(bcm_dev_t *d) {
    u32 cs = pci_read_config(d->bus, d->dev, d->func, 0x04);
    cs = (cs & 0xFFFF0000) | 0x0107;
    pci_write_config(d->bus, d->dev, d->func, 0x04, cs);
}

static int bcm_setup(bcm_dev_t *d, u8 bus, u8 dev, u8 func) {
    oc_memset(d, 0, sizeof(*d));
    d->bus = bus; d->dev = dev; d->func = func;

    u32 bar0 = pci_read_bar(bus, dev, func, 0);
    if (!bar0 || (bar0 & 0x1)) return -1;
    bcm_pci_enable(d);
    d->mmio = (volatile u8 *)(uintptr_t)(bar0 & 0xFFFFFFF0);

    /* Station address: MAC_ADDR_HI/LO hold the 6 bytes big-endian style
     * (HI = mac[0..3], LO = mac[4..5] in the top two bytes). */
    u32 hi = bcm_reg(d, BCM_MAC_ADDR_HI);
    u32 lo = bcm_reg(d, BCM_MAC_ADDR_LO);
    d->mac[0] = (u8)(hi >> 24);
    d->mac[1] = (u8)(hi >> 16);
    d->mac[2] = (u8)(hi >> 8);
    d->mac[3] = (u8)hi;
    d->mac[4] = (u8)(lo >> 24);
    d->mac[5] = (u8)(lo >> 16);
    if (d->mac[0] == 0 && d->mac[1] == 0 && d->mac[2] == 0) {
        /* No NVM address visible (some BCM440x revisions): synthesise a
         * locally administered one from the PCI address. */
        d->mac[0] = 0x02;
        d->mac[1] = 0x00;
        d->mac[2] = 0x5E;
        d->mac[3] = bus;
        d->mac[4] = dev;
        d->mac[5] = func;
        u32 nhi = ((u32)d->mac[0] << 24) | ((u32)d->mac[1] << 16) |
                  ((u32)d->mac[2] << 8) | d->mac[3];
        u32 nlo = ((u32)d->mac[4] << 24) | ((u32)d->mac[5] << 16);
        bcm_wreg(d, BCM_MAC_ADDR_HI, nhi);
        bcm_wreg(d, BCM_MAC_ADDR_LO, nlo);
    }

    /* Host descriptor rings. */
    d->tx_descs = (volatile bcm_tx_desc_t *)(uintptr_t)pmm_alloc_frame();
    if (!d->tx_descs) return -1;
    oc_memset((void *)d->tx_descs, 0, PMM_PAGE_SIZE);
    d->rx_descs = (volatile bcm_rx_desc_t *)(uintptr_t)pmm_alloc_frame();
    if (!d->rx_descs) return -1;
    oc_memset((void *)d->rx_descs, 0, PMM_PAGE_SIZE);
    for (int i = 0; i < BCM_TX_DESCS; i++) {
        d->tx_bufs[i] = (u8 *)bcm_dma_alloc(BCM_BUF_SIZE);
        if (!d->tx_bufs[i]) return -1;
    }
    for (int i = 0; i < BCM_RX_DESCS; i++) {
        d->rx_bufs[i] = (u8 *)bcm_dma_alloc(BCM_BUF_SIZE);
        if (!d->rx_bufs[i]) return -1;
    }
    d->tx_tail = 0;
    d->rx_head = 0;

    /* Point the standard RX ring control block at the host ring. */
    bcm_wreg(d, BCM_RCV_RING_CFG, (u32)(uintptr_t)d->rx_descs);
    bcm_wreg(d, BCM_RX_STD_PROD, 0);
    bcm_wreg(d, BCM_TX_MBOX0, 0);

    /* Enable the MAC datapath. */
    bcm_wreg(d, BCM_MAC_MODE, BCM_MAC_MODE_TX_EN | BCM_MAC_MODE_RX_EN);

    d->up = 1;
    return 0;
}

static int bcm_ops_send(nic_device_t *ndev, const void *buf, int len);
static int bcm_ops_recv(nic_device_t *ndev, void *buf, int maxlen);
static int bcm_ops_link(nic_device_t *ndev);
static int bcm_ops_mac(nic_device_t *ndev, u8 mac[6]);

static const nic_ops_t bcm_nic_ops = {
    .send        = bcm_ops_send,
    .recv        = bcm_ops_recv,
    .link_status = bcm_ops_link,
    .get_mac     = bcm_ops_mac,
};

int bcm57xx_init(pci_dev_t *pdev) {
    if (g_bcm.up) return 0;

    u8 bus = 0, dev = 0, func = 0;
    if (pdev) {
        bus = pdev->bus; dev = pdev->dev; func = pdev->func;
        u32 ids = pci_read_config(bus, dev, func, 0x00);
        u16 vid = (u16)(ids & 0xFFFF);
        u16 did = (u16)(ids >> 16);
        int known = (vid == 0x14E4);
        for (unsigned i = 0; known && i < sizeof(bcm_ids) / sizeof(bcm_ids[0]); i++)
            if (did == bcm_ids[i].did) known = 1;
        if (!known) return -1;
    } else {
        int found = 0;
        for (unsigned i = 0; i < sizeof(bcm_ids) / sizeof(bcm_ids[0]) && !found; i++) {
            if (pci_find_device(bcm_ids[i].vid, bcm_ids[i].did,
                                &bus, &dev, &func) == 0)
                found = 1;
        }
        if (!found) return -1;
    }

    if (bcm_setup(&g_bcm, bus, dev, func) != 0) {
        oc_console_puts("bcm57xx: init failed\n");
        return -1;
    }

    nic_device_t nd;
    oc_memset(&nd, 0, sizeof(nd));
    oc_strcpy(nd.name, "bcm57xx");
    nd.type = NIC_TYPE_BCM57XX;
    for (int k = 0; k < 6; k++) nd.mac[k] = g_bcm.mac[k];
    nd.bus = bus; nd.dev = dev; nd.func = func;
    nd.vendor_id = 0x14E4;
    u32 ids = pci_read_config(bus, dev, func, 0x00);
    nd.device_id = (u16)(ids >> 16);
    nd.priv = &g_bcm;
    int idx = nic_register(&nd, &bcm_nic_ops);

    char line[96];
    char n[24];
    oc_strcpy(line, "bcm57xx: Tigon3 at ");
    oc_u64_to_hex(bus, n, 2);  oc_strcat(line, n); oc_strcat(line, ":");
    oc_u64_to_hex(dev, n, 2);  oc_strcat(line, n); oc_strcat(line, ".");
    oc_u64_to_hex(func, n, 1); oc_strcat(line, n);
    oc_strcat(line, (idx >= 0) ? " [registered]\n" : " [registry full]\n");
    oc_console_puts(line);
    return idx >= 0 ? 0 : -1;
}

static int bcm_ops_send(nic_device_t *ndev, const void *buf, int len) {
    (void)ndev;
    bcm_dev_t *d = &g_bcm;
    if (!d->up || len <= 0 || len > BCM_BUF_SIZE) return -1;

    int slot = d->tx_tail;
    oc_memcpy(d->tx_bufs[slot], buf, len);
    d->tx_descs[slot].addr = (u32)(uintptr_t)d->tx_bufs[slot];
    d->tx_descs[slot].len_flags = (u32)len | 0x80000000u;   /* EOF */
    __asm__ volatile("sfence" ::: "memory");
    /* Kick the chip: write the new producer index into TX mailbox 0. */
    d->tx_tail = (d->tx_tail + 1) % BCM_TX_DESCS;
    bcm_wreg(d, BCM_TX_MBOX0, (u32)d->tx_tail);

    d->tx_packets++;
    return len;
}

static int bcm_ops_recv(nic_device_t *ndev, void *buf, int maxlen) {
    (void)ndev;
    bcm_dev_t *d = &g_bcm;
    if (!d->up) return -1;

    u32 status = d->rx_descs[d->rx_head].status;
    int len = (int)(status & 0xFFFF);
    if (len == 0) return 0;   /* not filled */

    if (len > maxlen) len = maxlen;
    oc_memcpy(buf, d->rx_bufs[d->rx_head], len);
    d->rx_descs[d->rx_head].status = 0;
    d->rx_head = (d->rx_head + 1) % BCM_RX_DESCS;
    /* Re-arming the RX return ring producer is a mailbox write too. */
    bcm_wreg(d, BCM_RX_STD_PROD, (u32)d->rx_head);
    d->rx_packets++;
    return len;
}

static int bcm_ops_link(nic_device_t *ndev) {
    (void)ndev;
    bcm_dev_t *d = &g_bcm;
    if (!d->up || !d->mmio) return -1;
    return (bcm_reg(d, BCM_MAC_STATUS) & BCM_MAC_STATUS_LNK) ? 1 : 0;
}

static int bcm_ops_mac(nic_device_t *ndev, u8 mac[6]) {
    (void)ndev;
    bcm_dev_t *d = &g_bcm;
    if (!mac) return -1;
    for (int k = 0; k < 6; k++) mac[k] = d->mac[k];
    return 0;
}

void bcm57xx_print_state(void) {
    bcm_dev_t *d = &g_bcm;
    char line[128];
    char n[24];
    if (!d->up) {
        oc_console_puts("bcm57xx: not present\n");
        return;
    }
    oc_strcpy(line, "bcm57xx: pci=");
    oc_u64_to_hex(d->bus, n, 2); oc_strcat(line, n); oc_strcat(line, ":");
    oc_u64_to_hex(d->dev, n, 2); oc_strcat(line, n); oc_strcat(line, ".");
    oc_u64_to_hex(d->func, n, 1); oc_strcat(line, n);
    oc_strcat(line, " tx=");
    oc_u64_to_str(d->tx_packets, n); oc_strcat(line, n);
    oc_strcat(line, " rx=");
    oc_u64_to_str(d->rx_packets, n); oc_strcat(line, n);
    oc_strcat(line, "\n");
    oc_console_puts(line);
}
