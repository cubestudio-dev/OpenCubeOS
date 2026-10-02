/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10b
 * File: kernel/nic_ixgbe.c
 * Purpose: Intel 82598/82599 10GbE driver (ixgbe) for the WP-10b nic
 *          framework.
 *
 * Hardware covered:
 *   8086:10FB  82599EB/X520
 *   8086:1563  X550
 *
 * Programming model (82598/82599 datasheets):
 *   - BAR0 MMIO 128 KiB; per-queue blocks of 0x1000: RX queue n registers
 *     at 0x1000 + n*0x1000 (RDBAL/RDBAH/RDLEN/RDH/RDT + RXDCTL at
 *     +0x28), TX queue n at 0x6000 + n*0x1000 (TXDCTL at +0x28).
 *   - Global RX enable via RXCTRL (0x03000) bit 0; TX via TXDCTL enable
 *     bit (25) per queue plus HLREG0 (0x05000) TXCRCEN.
 *   - Advanced descriptors (DEXT set): 16-byte TX read
 *     (addr, len | EOP | IFCS | RS | DEXT), 16-byte RX
 *     (addr read; length/status written back at offset 12).
 *
 * VERIFICATION NOTE (honest disclosure): QEMU has no 82598/82599/X550
 * device model and this sandbox has no real 10GbE NIC, so the datapath
 * has NOT been exercised; register programming below is datasheet
 * derived.  The driver compiles, links, and PCI-probes (nothing found
 * under QEMU).
 */
#include "nic.h"
#include "console.h"
#include "string.h"
#include "pmm.h"

#include <stdint.h>

static inline u32 mmio_read32(volatile void *p) { return *(volatile u32 *)p; }
static inline void mmio_write32(volatile void *p, u32 v) { *(volatile u32 *)p = v; }

/* Global registers. */
#define IXGBE_CTRL     0x0000
#define IXGBE_STATUS   0x0008
#define IXGBE_IMC      0x00898  /* interrupt mask clear */
#define IXGBE_RCTL     0x0100   /* legacy-compat RX control */
#define IXGBE_RXCTRL   0x03000  /* bit0 = RXEN */
#define IXGBE_HLREG0   0x05000  /* bit0 TXCRCEN, bit15 RXCRCSTRP */
#define IXGBE_RAL0     0x0A200  /* receive address low 0 */
#define IXGBE_RAH0     0x0A204

/* Per-queue blocks. */
#define IXGBE_RXQ_BASE(q)  (0x01000u + (u32)(q) * 0x1000u)
#define IXGBE_TXQ_BASE(q)  (0x06000u + (u32)(q) * 0x1000u)
#define IXGBE_Q_DBAL  0x00
#define IXGBE_Q_DBAH  0x04
#define IXGBE_Q_DLEN  0x08
#define IXGBE_Q_DH    0x10
#define IXGBE_Q_DT    0x18
#define IXGBE_Q_DCTL  0x28   /* RXDCTL / TXDCTL */

#define IXGBE_STATUS_LNK   (1u << 8)   /* bit 8 = Link Up on 82598/82599 */

#define IXGBE_NUM_DESC  32
#define IXGBE_BUF_SIZE  4096     /* jumbo-friendly 10G buffers */

/* TX advanced descriptor command bits (cmd_len word). */
#define IXGBE_TXD_EOP    (1u << 24)
#define IXGBE_TXD_IFCS   (1u << 25)
#define IXGBE_TXD_RS     (1u << 27)
#define IXGBE_TXD_DEXT   (1u << 29)

/* RX write-back: status byte at offset 15 of the descriptor (DD bit). */
#define IXGBE_RX_DD      0x01

typedef struct {
    u64 addr;            /* read: buffer address */
    u16 len;             /* TX: length; RX WB: packet length */
    u16 vlan;
    u32 cmd_status;      /* TX: EOP/IFCS/RS/DEXT; RX WB: status/errors */
} __attribute__((packed)) ixgbe_desc_t;

typedef struct {
    volatile u32 *mmio;
    volatile ixgbe_desc_t *rx_descs;
    volatile ixgbe_desc_t *tx_descs;
    u8 *rx_bufs[IXGBE_NUM_DESC];
    u8 *tx_bufs[IXGBE_NUM_DESC];
    int rx_tail;
    int tx_tail;
    u8  bus, dev, func;
    int up;
    u8  mac[6];
    u64 tx_packets, rx_packets;
} ixgbe_dev_t;

static ixgbe_dev_t g_ixgbe;

static const u16 ixgbe_ids[] = { 0x10FB, 0x1563 };

static u8 *ixgbe_dma_page = NULL;
static int ixgbe_dma_off = 0;

static void *ixgbe_dma_alloc(int size) {
    if (size > (int)PMM_PAGE_SIZE) return (void *)(uintptr_t)pmm_alloc_frame();
    if (ixgbe_dma_page == NULL || ixgbe_dma_off + size > (int)PMM_PAGE_SIZE) {
        ixgbe_dma_page = (u8 *)(uintptr_t)pmm_alloc_frame();
        if (ixgbe_dma_page == NULL) return NULL;
        ixgbe_dma_off = 0;
    }
    ixgbe_dma_off = (ixgbe_dma_off + 15) & ~15;
    void *p = ixgbe_dma_page + ixgbe_dma_off;
    ixgbe_dma_off += size;
    return p;
}

static u32 ix_reg(ixgbe_dev_t *d, u32 off) {
    return mmio_read32((volatile void *)((u8 *)d->mmio + off));
}
static void ix_wreg(ixgbe_dev_t *d, u32 off, u32 v) {
    mmio_write32((volatile void *)((u8 *)d->mmio + off), v);
}

static void ixgbe_pci_enable(ixgbe_dev_t *d) {
    u32 cs = pci_read_config(d->bus, d->dev, d->func, 0x04);
    cs = (cs & 0xFFFF0000) | 0x0107;
    pci_write_config(d->bus, d->dev, d->func, 0x04, cs);
}

static int ixgbe_setup(ixgbe_dev_t *d, u8 bus, u8 dev, u8 func) {
    oc_memset(d, 0, sizeof(*d));
    d->bus = bus; d->dev = dev; d->func = func;

    u32 bar0 = pci_read_bar(bus, dev, func, 0);
    if (!bar0 || (bar0 & 0x1)) return -1;
    ixgbe_pci_enable(d);
    d->mmio = (volatile u32 *)(uintptr_t)(bar0 & 0xFFFFFFF0);

    /* Polling mode. */
    ix_wreg(d, IXGBE_IMC, 0xFFFFFFFFu);

    /* Station address: RAL0/RAH0 (real boards load it from NVM). */
    u32 mac_low = ix_reg(d, IXGBE_RAL0);
    u32 mac_high = ix_reg(d, IXGBE_RAH0);
    d->mac[0] = mac_low & 0xFF;
    d->mac[1] = (mac_low >> 8) & 0xFF;
    d->mac[2] = (mac_low >> 16) & 0xFF;
    d->mac[3] = (mac_low >> 24) & 0xFF;
    d->mac[4] = mac_high & 0xFF;
    d->mac[5] = (mac_high >> 8) & 0xFF;
    if (d->mac[0] == 0 && d->mac[1] == 0 && d->mac[2] == 0) {
        d->mac[0] = 0x02;   /* locally administered fallback */
        d->mac[1] = 0x00;
        d->mac[2] = 0x5E;
        d->mac[3] = bus;
        d->mac[4] = dev;
        d->mac[5] = func;
        u32 lo = ((u32)d->mac[0]) | ((u32)d->mac[1] << 8) |
                 ((u32)d->mac[2] << 16) | ((u32)d->mac[3] << 24);
        u32 hi = ((u32)d->mac[4]) | ((u32)d->mac[5] << 8) | (1u << 30);
        ix_wreg(d, IXGBE_RAL0, lo);
        ix_wreg(d, IXGBE_RAH0, hi);
    }

    /* Descriptor rings + buffers (queue 0). */
    d->rx_descs = (volatile ixgbe_desc_t *)(uintptr_t)pmm_alloc_frame();
    if (!d->rx_descs) return -1;
    oc_memset((void *)d->rx_descs, 0, PMM_PAGE_SIZE);
    d->tx_descs = (volatile ixgbe_desc_t *)(uintptr_t)pmm_alloc_frame();
    if (!d->tx_descs) return -1;
    oc_memset((void *)d->tx_descs, 0, PMM_PAGE_SIZE);
    for (int i = 0; i < IXGBE_NUM_DESC; i++) {
        d->rx_bufs[i] = (u8 *)ixgbe_dma_alloc(IXGBE_BUF_SIZE);
        d->tx_bufs[i] = (u8 *)ixgbe_dma_alloc(IXGBE_BUF_SIZE);
        if (!d->rx_bufs[i] || !d->tx_bufs[i]) return -1;
        d->rx_descs[i].addr = (u64)(uintptr_t)d->rx_bufs[i];
    }
    d->rx_tail = 0;
    d->tx_tail = 0;

    /* RX queue 0. */
    {
        u32 base = IXGBE_RXQ_BASE(0);
        ix_wreg(d, base + IXGBE_Q_DBAL, (u32)(uintptr_t)d->rx_descs);
        ix_wreg(d, base + IXGBE_Q_DBAH, 0);
        ix_wreg(d, base + IXGBE_Q_DLEN,
                (u32)(sizeof(ixgbe_desc_t) * IXGBE_NUM_DESC));
        ix_wreg(d, base + IXGBE_Q_DH, 0);
        ix_wreg(d, base + IXGBE_Q_DT, 0);
        /* RXDCTL: enable (bit25), WTHRESH 1 (bits 16-24 = 1). */
        ix_wreg(d, base + IXGBE_Q_DCTL, (1u << 25) | (1u << 16));
    }
    /* TX queue 0. */
    {
        u32 base = IXGBE_TXQ_BASE(0);
        ix_wreg(d, base + IXGBE_Q_DBAL, (u32)(uintptr_t)d->tx_descs);
        ix_wreg(d, base + IXGBE_Q_DBAH, 0);
        ix_wreg(d, base + IXGBE_Q_DLEN,
                (u32)(sizeof(ixgbe_desc_t) * IXGBE_NUM_DESC));
        ix_wreg(d, base + IXGBE_Q_DH, 0);
        ix_wreg(d, base + IXGBE_Q_DT, 0);
        /* TXDCTL: enable + HWTHRESH 0. */
        ix_wreg(d, base + IXGBE_Q_DCTL, (1u << 25));
    }

    /* Global datapath enables. */
    ix_wreg(d, IXGBE_HLREG0,
            (1u << 0) |          /* TXCRCEN */
            (1u << 15));         /* RXCRCSTRP (strip FCS) */
    ix_wreg(d, IXGBE_RXCTRL, 1u);   /* RXEN */

    ixgbe_pci_enable(d);
    d->up = 1;
    return 0;
}

static int ixgbe_ops_send(nic_device_t *ndev, const void *buf, int len);
static int ixgbe_ops_recv(nic_device_t *ndev, void *buf, int maxlen);
static int ixgbe_ops_link(nic_device_t *ndev);
static int ixgbe_ops_mac(nic_device_t *ndev, u8 mac[6]);

static const nic_ops_t ixgbe_nic_ops = {
    .send        = ixgbe_ops_send,
    .recv        = ixgbe_ops_recv,
    .link_status = ixgbe_ops_link,
    .get_mac     = ixgbe_ops_mac,
};

int ixgbe_init(pci_dev_t *pdev) {
    if (g_ixgbe.up) return 0;

    u8 bus = 0, dev = 0, func = 0;
    if (pdev) {
        bus = pdev->bus; dev = pdev->dev; func = pdev->func;
        u32 ids = pci_read_config(bus, dev, func, 0x00);
        u16 vid = (u16)(ids & 0xFFFF);
        u16 did = (u16)(ids >> 16);
        int known = (vid == 0x8086);
        for (unsigned i = 0; known && i < sizeof(ixgbe_ids) / sizeof(ixgbe_ids[0]); i++)
            if (did == ixgbe_ids[i]) known = 1;
        if (!known) return -1;
    } else {
        int found = 0;
        for (unsigned i = 0; i < sizeof(ixgbe_ids) / sizeof(ixgbe_ids[0]) && !found; i++) {
            if (pci_find_device(0x8086, ixgbe_ids[i], &bus, &dev, &func) == 0)
                found = 1;
        }
        if (!found) return -1;
    }

    if (ixgbe_setup(&g_ixgbe, bus, dev, func) != 0) {
        oc_console_puts("ixgbe: init failed\n");
        return -1;
    }

    nic_device_t nd;
    oc_memset(&nd, 0, sizeof(nd));
    oc_strcpy(nd.name, "ixgbe");
    nd.type = NIC_TYPE_IXGBE;
    for (int k = 0; k < 6; k++) nd.mac[k] = g_ixgbe.mac[k];
    nd.bus = bus; nd.dev = dev; nd.func = func;
    nd.vendor_id = 0x8086;
    u32 ids = pci_read_config(bus, dev, func, 0x00);
    nd.device_id = (u16)(ids >> 16);
    nd.priv = &g_ixgbe;
    int idx = nic_register(&nd, &ixgbe_nic_ops);

    char line[96];
    char n[24];
    oc_strcpy(line, "ixgbe: 10GbE at ");
    oc_u64_to_hex(bus, n, 2);  oc_strcat(line, n); oc_strcat(line, ":");
    oc_u64_to_hex(dev, n, 2);  oc_strcat(line, n); oc_strcat(line, ".");
    oc_u64_to_hex(func, n, 1); oc_strcat(line, n);
    oc_strcat(line, (idx >= 0) ? " [registered]\n" : " [registry full]\n");
    oc_console_puts(line);
    return idx >= 0 ? 0 : -1;
}

static int ixgbe_ops_send(nic_device_t *ndev, const void *buf, int len) {
    (void)ndev;
    ixgbe_dev_t *d = &g_ixgbe;
    if (!d->up || len <= 0 || len > IXGBE_BUF_SIZE) return -1;

    int slot = d->tx_tail;
    oc_memcpy(d->tx_bufs[slot], buf, len);
    d->tx_descs[slot].addr = (u64)(uintptr_t)d->tx_bufs[slot];
    d->tx_descs[slot].len = (u16)len;
    d->tx_descs[slot].vlan = 0;
    d->tx_descs[slot].cmd_status =
        IXGBE_TXD_EOP | IXGBE_TXD_IFCS | IXGBE_TXD_RS | IXGBE_TXD_DEXT;
    __asm__ volatile("sfence" ::: "memory");
    d->tx_tail = (d->tx_tail + 1) % IXGBE_NUM_DESC;
    ix_wreg(d, IXGBE_TXQ_BASE(0) + IXGBE_Q_DT, (u32)d->tx_tail);

    for (int t = 0; t < 1000000; t++) {
        if (d->tx_descs[slot].cmd_status & IXGBE_TXD_RS) break;
    }
    d->tx_packets++;
    return len;
}

static int ixgbe_ops_recv(nic_device_t *ndev, void *buf, int maxlen) {
    (void)ndev;
    ixgbe_dev_t *d = &g_ixgbe;
    if (!d->up) return -1;

    /* DD bit lives in the status byte of the write-back. */
    volatile u8 *wb_status =
        (volatile u8 *)&d->rx_descs[d->rx_tail].cmd_status;
    wb_status += 3;   /* cmd_status bits 24-31 = status byte */
    if (!(*wb_status & IXGBE_RX_DD)) return 0;

    int len = d->rx_descs[d->rx_tail].len;
    if (len > maxlen) len = maxlen;
    if (len > 0) oc_memcpy(buf, d->rx_bufs[d->rx_tail], len);

    d->rx_descs[d->rx_tail].len = 0;
    d->rx_descs[d->rx_tail].cmd_status = 0;
    d->rx_descs[d->rx_tail].addr = (u64)(uintptr_t)d->rx_bufs[d->rx_tail];
    __asm__ volatile("sfence" ::: "memory");
    d->rx_tail = (d->rx_tail + 1) % IXGBE_NUM_DESC;
    u32 rdt = (u32)((d->rx_tail + IXGBE_NUM_DESC - 1) % IXGBE_NUM_DESC);
    ix_wreg(d, IXGBE_RXQ_BASE(0) + IXGBE_Q_DT, rdt);
    d->rx_packets++;
    return len;
}

static int ixgbe_ops_link(nic_device_t *ndev) {
    (void)ndev;
    ixgbe_dev_t *d = &g_ixgbe;
    if (!d->up || !d->mmio) return -1;
    return (ix_reg(d, IXGBE_STATUS) & IXGBE_STATUS_LNK) ? 1 : 0;
}

static int ixgbe_ops_mac(nic_device_t *ndev, u8 mac[6]) {
    (void)ndev;
    ixgbe_dev_t *d = &g_ixgbe;
    if (!mac) return -1;
    for (int k = 0; k < 6; k++) mac[k] = d->mac[k];
    return 0;
}

void ixgbe_print_state(void) {
    ixgbe_dev_t *d = &g_ixgbe;
    char line[128];
    char n[24];
    if (!d->up) {
        oc_console_puts("ixgbe: not present\n");
        return;
    }
    oc_strcpy(line, "ixgbe: pci=");
    oc_u64_to_hex(d->bus, n, 2); oc_strcat(line, n); oc_strcat(line, ":");
    oc_u64_to_hex(d->dev, n, 2); oc_strcat(line, n); oc_strcat(line, ".");
    oc_u64_to_hex(d->func, n, 1); oc_strcat(line, n);
    oc_strcat(line, " link=");
    oc_strcat(line, (ix_reg(d, IXGBE_STATUS) & IXGBE_STATUS_LNK) ? "up" : "down");
    oc_strcat(line, " tx=");
    oc_u64_to_str(d->tx_packets, n); oc_strcat(line, n);
    oc_strcat(line, " rx=");
    oc_u64_to_str(d->rx_packets, n); oc_strcat(line, n);
    oc_strcat(line, "\n");
    oc_console_puts(line);
}
