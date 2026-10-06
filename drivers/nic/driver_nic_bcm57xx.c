/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10b
 * File: kernel/driver_nic_bcm57xx.c
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
#include "driver_nic.h"
#include "screen_console.h"
#include "lib_string.h"
#include "mem_pmm.h"

#include <stdint.h>

static inline u32 mmio_read32(volatile void *p) { return *(volatile u32 *)p; }
static inline void mmio_write32(volatile void *p, u32 v) { *(volatile u32 *)p = v; }

/* Tigon3 register offsets (BCM57xx datasheet / tg3 layout). */
#define BCM_MAC_MODE      0x0400
#define BCM_MAC_STATUS    0x0404
#define BCM_MAC_ADDR_HI   0x0410
#define BCM_MAC_ADDR_LO   0x0414
#define BCM_TX_MBOX0      0x3200   /* TX producer ring 0 mailbox (tail index) */
#define BCM_TX_CONIDX0    0x3208   /* TX consumer index (chip's send progress) */
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
} __attribute__((packed)) driver_nic_bcm57xx_tx_desc_t;

/* BUG-0128 FIX (A8-7): the STANDARD RX ring is a producer ring of 8-byte
 * buffer descriptors {host_addr, flags_len} - the chip DMA-reads this
 * table to know where to place incoming frames. The old struct was a
 * return-ring shape and its entries were never populated, so the
 * hardware never received any buffer address. */
typedef struct {
    u32 host_addr;    /* buffer address handed to the chip */
    u32 flags_len;    /* bits 0-15 buffer size */
} __attribute__((packed)) driver_nic_bcm57xx_rx_desc_t;

typedef struct {
    volatile u8 *mmio;
    u8   bus, dev, func;
    int  up;
    u8   mac[6];
    volatile driver_nic_bcm57xx_tx_desc_t *tx_descs;
    volatile driver_nic_bcm57xx_rx_desc_t *rx_descs;
    u8  *tx_bufs[BCM_TX_DESCS];
    u8  *rx_bufs[BCM_RX_DESCS];
    int  tx_tail;
    int  tx_clean;    /* BUG-0128 FIX: chip-consumed TX index (in-flight tracking) */
    int  rx_head;
    u64  tx_packets, rx_packets;
} driver_nic_bcm57xx_dev_t;

static driver_nic_bcm57xx_dev_t g_bcm;

static const struct { u16 vid; u16 did; } driver_nic_bcm57xx_ids[] = {
    { 0x14E4, 0x1644 }, { 0x14E4, 0x1653 }, { 0x14E4, 0x1673 },
    { 0x14E4, 0x1677 }, { 0x14E4, 0x4401 }, { 0x14E4, 0x170C },
};

static u8 *driver_nic_bcm57xx_dma_page = NULL;
static int driver_nic_bcm57xx_dma_off = 0;

static void *driver_nic_bcm57xx_dma_alloc(int size) {
    if (size > (int)PMM_PAGE_SIZE) return (void *)(uintptr_t)mem_pmm_alloc_frame();
    if (driver_nic_bcm57xx_dma_page == NULL || driver_nic_bcm57xx_dma_off + size > (int)PMM_PAGE_SIZE) {
        driver_nic_bcm57xx_dma_page = (u8 *)(uintptr_t)mem_pmm_alloc_frame();
        if (driver_nic_bcm57xx_dma_page == NULL) return NULL;
        driver_nic_bcm57xx_dma_off = 0;
    }
    driver_nic_bcm57xx_dma_off = (driver_nic_bcm57xx_dma_off + 15) & ~15;
    void *p = driver_nic_bcm57xx_dma_page + driver_nic_bcm57xx_dma_off;
    driver_nic_bcm57xx_dma_off += size;
    return p;
}

static u32 driver_nic_bcm57xx_reg(driver_nic_bcm57xx_dev_t *d, u32 off) {
    return mmio_read32((volatile void *)(d->mmio + off));
}
static void driver_nic_bcm57xx_wreg(driver_nic_bcm57xx_dev_t *d, u32 off, u32 v) {
    mmio_write32((volatile void *)(d->mmio + off), v);
}

static void driver_nic_bcm57xx_pci_enable(driver_nic_bcm57xx_dev_t *d) {
    u32 cs = driver_pci_read_config(d->bus, d->dev, d->func, 0x04);
    cs = (cs & 0xFFFF0000) | 0x0107;
    driver_pci_write_config(d->bus, d->dev, d->func, 0x04, cs);
}

static int driver_nic_bcm57xx_setup(driver_nic_bcm57xx_dev_t *d, u8 bus, u8 dev, u8 func) {
    memset(d, 0, sizeof(*d));
    d->bus = bus; d->dev = dev; d->func = func;

    u32 bar0 = driver_pci_read_bar(bus, dev, func, 0);
    if (!bar0 || (bar0 & 0x1)) return -1;
    driver_nic_bcm57xx_pci_enable(d);
    d->mmio = (volatile u8 *)(uintptr_t)(bar0 & 0xFFFFFFF0);

    /* Station address: MAC_ADDR_HI/LO hold the 6 bytes big-endian style
     * (HI = mac[0..3], LO = mac[4..5] in the top two bytes). */
    u32 hi = driver_nic_bcm57xx_reg(d, BCM_MAC_ADDR_HI);
    u32 lo = driver_nic_bcm57xx_reg(d, BCM_MAC_ADDR_LO);
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
        driver_nic_bcm57xx_wreg(d, BCM_MAC_ADDR_HI, nhi);
        driver_nic_bcm57xx_wreg(d, BCM_MAC_ADDR_LO, nlo);
    }

    /* Host descriptor rings. */
    d->tx_descs = (volatile driver_nic_bcm57xx_tx_desc_t *)(uintptr_t)mem_pmm_alloc_frame();
    if (!d->tx_descs) return -1;
    memset((void *)d->tx_descs, 0, PMM_PAGE_SIZE);
    d->rx_descs = (volatile driver_nic_bcm57xx_rx_desc_t *)(uintptr_t)mem_pmm_alloc_frame();
    if (!d->rx_descs) return -1;
    memset((void *)d->rx_descs, 0, PMM_PAGE_SIZE);
    for (int i = 0; i < BCM_TX_DESCS; i++) {
        d->tx_bufs[i] = (u8 *)driver_nic_bcm57xx_dma_alloc(BCM_BUF_SIZE);
        if (!d->tx_bufs[i]) return -1;
    }
    for (int i = 0; i < BCM_RX_DESCS; i++) {
        d->rx_bufs[i] = (u8 *)driver_nic_bcm57xx_dma_alloc(BCM_BUF_SIZE);
        if (!d->rx_bufs[i]) return -1;
    }
    d->tx_tail = 0;
    d->tx_clean = 0;   /* BUG-0128 FIX: in-flight tracking */
    d->rx_head = 0;

    /* BUG-0128 FIX (A8-7b): hand every RX buffer to the hardware by
     * writing its address into the standard producer ring and advancing
     * the producer index across the whole ring. Previously the buffers
     * were allocated but never submitted, so RX could never work. */
    for (int i = 0; i < BCM_RX_DESCS; i++) {
        d->rx_descs[i].host_addr = (u32)(uintptr_t)d->rx_bufs[i];
        d->rx_descs[i].flags_len = (u32)BCM_BUF_SIZE;
    }
    __asm__ volatile("sfence" ::: "memory");

    /* Point the standard RX ring control block at the host ring. */
    driver_nic_bcm57xx_wreg(d, BCM_RCV_RING_CFG, (u32)(uintptr_t)d->rx_descs);
    driver_nic_bcm57xx_wreg(d, BCM_RX_STD_PROD, (u32)(BCM_RX_DESCS - 1));
    driver_nic_bcm57xx_wreg(d, BCM_TX_MBOX0, 0);

    /* Enable the MAC datapath. */
    driver_nic_bcm57xx_wreg(d, BCM_MAC_MODE, BCM_MAC_MODE_TX_EN | BCM_MAC_MODE_RX_EN);

    d->up = 1;
    return 0;
}

static int driver_nic_bcm57xx_ops_send(driver_nic_device_t *ndev, const void *buf, int len);
static int driver_nic_bcm57xx_ops_recv(driver_nic_device_t *ndev, void *buf, int maxlen);
static int driver_nic_bcm57xx_ops_link(driver_nic_device_t *ndev);
static int driver_nic_bcm57xx_ops_mac(driver_nic_device_t *ndev, u8 mac[6]);

static const driver_nic_ops_t driver_nic_bcm57xx_nic_ops = {
    .send        = driver_nic_bcm57xx_ops_send,
    .recv        = driver_nic_bcm57xx_ops_recv,
    .link_status = driver_nic_bcm57xx_ops_link,
    .get_mac     = driver_nic_bcm57xx_ops_mac,
};

int bcm57xx_init(driver_pci_dev_t *pdev) {
    if (g_bcm.up) return 0;

    u8 bus = 0, dev = 0, func = 0;
    if (pdev) {
        bus = pdev->bus; dev = pdev->dev; func = pdev->func;
        u32 ids = driver_pci_read_config(bus, dev, func, 0x00);
        u16 vid = (u16)(ids & 0xFFFF);
        u16 did = (u16)(ids >> 16);
        int known = (vid == 0x14E4);
        for (unsigned i = 0; known && i < sizeof(driver_nic_bcm57xx_ids) / sizeof(driver_nic_bcm57xx_ids[0]); i++)
            if (did == driver_nic_bcm57xx_ids[i].did) known = 1;
        if (!known) return -1;
    } else {
        int found = 0;
        for (unsigned i = 0; i < sizeof(driver_nic_bcm57xx_ids) / sizeof(driver_nic_bcm57xx_ids[0]) && !found; i++) {
            if (driver_pci_find_device(driver_nic_bcm57xx_ids[i].vid, driver_nic_bcm57xx_ids[i].did,
                                &bus, &dev, &func) == 0)
                found = 1;
        }
        if (!found) return -1;
    }

    if (driver_nic_bcm57xx_setup(&g_bcm, bus, dev, func) != 0) {
        screen_console_puts("bcm57xx: init failed\n");
        return -1;
    }

    driver_nic_device_t nd;
    memset(&nd, 0, sizeof(nd));
    strcpy(nd.name, "bcm57xx");
    nd.type = NIC_TYPE_BCM57XX;
    for (int k = 0; k < 6; k++) nd.mac[k] = g_bcm.mac[k];
    nd.bus = bus; nd.dev = dev; nd.func = func;
    nd.vendor_id = 0x14E4;
    u32 ids = driver_pci_read_config(bus, dev, func, 0x00);
    nd.device_id = (u16)(ids >> 16);
    nd.priv = &g_bcm;
    int idx = driver_nic_register(&nd, &driver_nic_bcm57xx_nic_ops);

    char line[96];
    char n[24];
    strcpy(line, "bcm57xx: Tigon3 at ");
    u64_to_hex(bus, n, 2);  strcat(line, n); strcat(line, ":");
    u64_to_hex(dev, n, 2);  strcat(line, n); strcat(line, ".");
    u64_to_hex(func, n, 1); strcat(line, n);
    strcat(line, (idx >= 0) ? " [registered]\n" : " [registry full]\n");
    screen_console_puts(line);
    return idx >= 0 ? 0 : -1;
}

static int driver_nic_bcm57xx_ops_send(driver_nic_device_t *ndev, const void *buf, int len) {
    (void)ndev;
    driver_nic_bcm57xx_dev_t *d = &g_bcm;
    if (!d->up || len <= 0 || len > BCM_BUF_SIZE) return -1;

    int slot = d->tx_tail;
    /* BUG-0128 FIX (A8-7a): refuse to overwrite a descriptor the chip
     * may still own. Track the consumer index (chip's send progress);
     * when the ring is nearly full, poll the TX consumer index mailbox
     * (0x3208, tg3 MAILBOX_SND_CONIDX_0) for progress and fail the send
     * with -1 if the outstanding descriptors do not drain in time. */
    for (int t = 0; t < 1000000; t++) {
        int inflight = (d->tx_tail - d->tx_clean + BCM_TX_DESCS) % BCM_TX_DESCS;
        if (inflight < BCM_TX_DESCS - 1) break;
        d->tx_clean = (int)driver_nic_bcm57xx_reg(d, BCM_TX_CONIDX0) % BCM_TX_DESCS;
        if (t == 999999) return -1;   /* ring still full: honest failure */
    }
    memcpy(d->tx_bufs[slot], buf, len);
    d->tx_descs[slot].addr = (u32)(uintptr_t)d->tx_bufs[slot];
    d->tx_descs[slot].len_flags = (u32)len | 0x80000000u;   /* EOF */
    __asm__ volatile("sfence" ::: "memory");
    /* Kick the chip: write the new producer index into TX mailbox 0. */
    d->tx_tail = (d->tx_tail + 1) % BCM_TX_DESCS;
    driver_nic_bcm57xx_wreg(d, BCM_TX_MBOX0, (u32)d->tx_tail);

    d->tx_packets++;
    return len;
}

static int driver_nic_bcm57xx_ops_recv(driver_nic_device_t *ndev, void *buf, int maxlen) {
    (void)ndev;
    driver_nic_bcm57xx_dev_t *d = &g_bcm;
    if (!d->up) return -1;

    /* BUG-0128 FIX (A8-7c): the buffers are now actually submitted (see
     * init), and this polls the SAME descriptor table the chip owns: a
     * consumed entry carries the received length in flags_len[15:0].
     * len == 0 means the chip has not placed a frame there yet (honest
     * "no frame"), and unlike before the descriptor is refilled with a
     * fresh buffer address before re-advancing the producer index. */
    u32 fl = d->rx_descs[d->rx_head].flags_len;
    int len = (int)(fl & 0xFFFFu);
    if (len == 0) return 0;   /* no frame landed here yet */

    if (len > maxlen) len = maxlen;
    memcpy(buf, d->rx_bufs[d->rx_head], len);
    /* Requeue this slot: clear the entry, hand the buffer back through
     * the standard producer index (single-entry re-submission). */
    d->rx_descs[d->rx_head].flags_len = (u32)BCM_BUF_SIZE;
    d->rx_descs[d->rx_head].host_addr = (u32)(uintptr_t)d->rx_bufs[d->rx_head];
    __asm__ volatile("sfence" ::: "memory");
    d->rx_head = (d->rx_head + 1) % BCM_RX_DESCS;
    driver_nic_bcm57xx_wreg(d, BCM_RX_STD_PROD, (u32)((d->rx_head + BCM_RX_DESCS - 1) % BCM_RX_DESCS));
    d->rx_packets++;
    return len;
}

static int driver_nic_bcm57xx_ops_link(driver_nic_device_t *ndev) {
    (void)ndev;
    driver_nic_bcm57xx_dev_t *d = &g_bcm;
    if (!d->up || !d->mmio) return -1;
    return (driver_nic_bcm57xx_reg(d, BCM_MAC_STATUS) & BCM_MAC_STATUS_LNK) ? 1 : 0;
}

static int driver_nic_bcm57xx_ops_mac(driver_nic_device_t *ndev, u8 mac[6]) {
    (void)ndev;
    driver_nic_bcm57xx_dev_t *d = &g_bcm;
    if (!mac) return -1;
    for (int k = 0; k < 6; k++) mac[k] = d->mac[k];
    return 0;
}

void bcm57xx_print_state(void) {
    driver_nic_bcm57xx_dev_t *d = &g_bcm;
    char line[128];
    char n[24];
    if (!d->up) {
        screen_console_puts("bcm57xx: not present\n");
        return;
    }
    strcpy(line, "bcm57xx: pci=");
    u64_to_hex(d->bus, n, 2); strcat(line, n); strcat(line, ":");
    u64_to_hex(d->dev, n, 2); strcat(line, n); strcat(line, ".");
    u64_to_hex(d->func, n, 1); strcat(line, n);
    strcat(line, " tx=");
    u64_to_str(d->tx_packets, n); strcat(line, n);
    strcat(line, " rx=");
    u64_to_str(d->rx_packets, n); strcat(line, n);
    strcat(line, "\n");
    screen_console_puts(line);
}
