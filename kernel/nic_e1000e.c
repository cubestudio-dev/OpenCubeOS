/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10b
 * File: kernel/nic_e1000e.c
 * Purpose: Intel 8254x/82574 family NIC driver (e1000e) for the WP-10b
 *          nic framework.
 *
 * Hardware covered:
 *   8086:100E  82540EM  (QEMU "-device e1000")
 *   8086:100F  82545GM
 *   8086:10D3  82574L   (QEMU "-device e1000e")
 *   8086:10EA  82577LM  (laptops)
 *   8086:15B8  82583V   (desktops)
 *
 * Programming model (per the Intel 8254x / 82574 datasheets):
 *   - BAR0 MMIO register block; the core registers used here (CTRL,
 *     STATUS, RCTL, TCTL, the descriptor ring registers and RA[0]) share
 *     the same offsets across the whole family.
 *   - Legacy 16-byte TX/RX descriptors (the 82574 keeps full legacy
 *     descriptor compatibility; RCTL.DTYP stays 00 for legacy RX).
 *   - Polling mode: interrupts are masked, net.c polls nic_recv().
 *   - DMA buffers come from the identity-mapped PMM region.
 */
#include "nic.h"
#include "console.h"
#include "string.h"
#include "pmm.h"

#include <stdint.h>

/* ---- MMIO accessors (file-local, same pattern as nvme.c) ---- */
static inline u32 mmio_read32(volatile void *p) {
    return *(volatile u32 *)p;
}
static inline void mmio_write32(volatile void *p, u32 v) {
    *(volatile u32 *)p = v;
}

/* ---- Register offsets (8254x/82574 common layout) ---- */
#define E1000E_CTRL    0x0000
#define E1000E_STATUS  0x0008
#define E1000E_EERD    0x0014
#define E1000E_IMC     0x00D8
#define E1000E_RCTL    0x0100
#define E1000E_TIPG    0x0410
#define E1000E_TCTL    0x0400
#define E1000E_RDBAL   0x2800
#define E1000E_RDBAH   0x2804
#define E1000E_RDLEN   0x2808
#define E1000E_RDH     0x2810
#define E1000E_RDT     0x2818
#define E1000E_TDBAL   0x3800
#define E1000E_TDBAH   0x3804
#define E1000E_TDLEN   0x3808
#define E1000E_TDH     0x3810
#define E1000E_TDT     0x3818
#define E1000E_RA      0x5400

/* CTRL bits */
#define E1000E_CTRL_FD      (1u << 0)
#define E1000E_CTRL_ASDE    (1u << 5)
#define E1000E_CTRL_SLU     (1u << 6)
/* STATUS bits: bit1 LU (link up), bits 8-9 speed (10=100M, 11=1G) */
#define E1000E_STATUS_LU    (1u << 1)

/* RCTL bits */
#define E1000E_RCTL_EN      0x00000002u
#define E1000E_RCTL_UPE     0x00000008u
#define E1000E_RCTL_MPE     0x00000010u
#define E1000E_RCTL_BAM     0x00008000u
#define E1000E_RCTL_SECRC   0x04000000u

/* TCTL bits */
#define E1000E_TCTL_EN      (1u << 1)
#define E1000E_TCTL_PSP     (1u << 3)

/* Legacy descriptor (identical 16-byte layout across the family). */
typedef struct {
    u64 addr;
    u16 length;
    union {
        u16 csum;
        struct {
            u8 cso;
            u8 cmd;
        };
    };
    u8  status;
    u8  errors;
    u16 special;
} __attribute__((packed)) e1000e_desc_t;

#define E1000E_NUM_DESC  32
#define E1000E_BUF_SIZE  2048

/* TX cmd bits: EOP=0x08 | IFCS=0x02 | RS=0x01 */
#define E1000E_TX_CMD    0x0B
/* RX status DD bit */
#define E1000E_RX_DD     0x01
/* TX status DD bit */
#define E1000E_TX_DD     0x01

typedef struct {
    volatile u32 *mmio;
    volatile e1000e_desc_t *rx_descs;
    volatile e1000e_desc_t *tx_descs;
    u8 *rx_bufs[E1000E_NUM_DESC];
    u8 *tx_bufs[E1000E_NUM_DESC];
    int rx_tail;
    int tx_tail;
    u8  bus, dev, func;
    int up;
    u64 tx_packets, rx_packets;
} e1000e_dev_t;

static e1000e_dev_t g_e1000e;

/* Vendor/device IDs covered by this driver: the PCIe e1000e family
 * (82574L/82577LM/82583V) exactly as the WP-10b spec lists them.  The
 * older 82540EM (8086:100E) / 82545GM (8086:100F) parts stay with the
 * legacy built-in e1000 path in net.c (WP-06), which keeps QEMU's
 * default NIC and the 18/18 regression on the proven code path. */
static const u16 e1000e_ids[] = { 0x10D3, 0x10EA, 0x15B8 };

/* DMA allocation from the identity-mapped region (same pattern as
 * net.c: PMM pages, sub-allocated for small buffers). */
static u8 *e1000e_dma_page = NULL;
static int e1000e_dma_off = 0;

static void *e1000e_dma_alloc(int size) {
    if (size > (int)PMM_PAGE_SIZE) return (void *)pmm_alloc_frame();
    if (e1000e_dma_page == NULL ||
        e1000e_dma_off + size > (int)PMM_PAGE_SIZE) {
        e1000e_dma_page = (u8 *)(uintptr_t)pmm_alloc_frame();
        if (e1000e_dma_page == NULL) return NULL;
        e1000e_dma_off = 0;
    }
    e1000e_dma_off = (e1000e_dma_off + 15) & ~15;
    void *p = e1000e_dma_page + e1000e_dma_off;
    e1000e_dma_off += size;
    return p;
}

/* Read the station address: RA[0] first, EEPROM (EERD) as fallback. */
u8 g_e1000e_mac[6];

static void e1000e_read_mac(e1000e_dev_t *d) {
    u32 mac_low = mmio_read32((volatile void *)((u8 *)d->mmio + E1000E_RA));
    u32 mac_high = mmio_read32((volatile void *)((u8 *)d->mmio + E1000E_RA + 4));
    u8 mac[6];
    mac[0] = mac_low & 0xFF;
    mac[1] = (mac_low >> 8) & 0xFF;
    mac[2] = (mac_low >> 16) & 0xFF;
    mac[3] = (mac_low >> 24) & 0xFF;
    mac[4] = mac_high & 0xFF;
    mac[5] = (mac_high >> 8) & 0xFF;

    if (mac[0] == 0 && mac[1] == 0 && mac[2] == 0) {
        for (int word = 0; word < 3; word++) {
            mmio_write32((volatile void *)((u8 *)d->mmio + E1000E_EERD),
                         1u | ((u32)word << 8) | (1u << 4));
            u32 val = 0;
            for (int t = 0; t < 100000; t++) {
                val = mmio_read32((volatile void *)((u8 *)d->mmio + E1000E_EERD));
                if (val & (1u << 9)) break;   /* DONE */
            }
            u16 data = (u16)((val >> 16) & 0xFFFF);
            if (word == 0)      { mac[0] = data & 0xFF; mac[1] = (data >> 8) & 0xFF; }
            else if (word == 1) { mac[2] = data & 0xFF; mac[3] = (data >> 8) & 0xFF; }
            else                { mac[4] = data & 0xFF; mac[5] = (data >> 8) & 0xFF; }
        }
    }
    for (int k = 0; k < 6; k++) g_e1000e_mac[k] = mac[k];
}

static int e1000e_ops_send(nic_device_t *dev, const void *buf, int len);
static int e1000e_ops_recv(nic_device_t *dev, void *buf, int maxlen);
static int e1000e_ops_link(nic_device_t *dev);
static int e1000e_ops_mac(nic_device_t *dev, u8 mac[6]);

static const nic_ops_t e1000e_nic_ops = {
    .send        = e1000e_ops_send,
    .recv        = e1000e_ops_recv,
    .link_status = e1000e_ops_link,
    .get_mac     = e1000e_ops_mac,
};

static void e1000e_pci_enable(e1000e_dev_t *d) {
    u32 cs = pci_read_config(d->bus, d->dev, d->func, 0x04);
    cs = (cs & 0xFFFF0000) | 0x0107;   /* IO | MEM | BM | SERR# */
    pci_write_config(d->bus, d->dev, d->func, 0x04, cs);
}

static int e1000e_setup(e1000e_dev_t *d, u8 bus, u8 dev, u8 func) {
    oc_memset(d, 0, sizeof(*d));
    d->bus = bus; d->dev = dev; d->func = func;

    u32 bar0 = pci_read_bar(bus, dev, func, 0);
    if (bar0 == 0) return -1;

    e1000e_pci_enable(d);
    d->mmio = (volatile u32 *)(uintptr_t)bar0;

    e1000e_read_mac(d);

    /* CTRL: full duplex + auto speed + set link up (no soft reset: it
     * would clear the PCI command register, same lesson as net.c). */
    mmio_write32((volatile void *)((u8 *)d->mmio + E1000E_CTRL),
                 E1000E_CTRL_FD | E1000E_CTRL_ASDE | E1000E_CTRL_SLU);

    /* Polling driver: mask all interrupts. */
    mmio_write32((volatile void *)((u8 *)d->mmio + E1000E_IMC), 0xFFFFFFFFu);

    /* ---- RX ring ---- */
    d->rx_descs = (volatile e1000e_desc_t *)(uintptr_t)pmm_alloc_frame();
    if (!d->rx_descs) return -1;
    oc_memset((void *)d->rx_descs, 0, PMM_PAGE_SIZE);
    for (int i = 0; i < E1000E_NUM_DESC; i++) {
        d->rx_bufs[i] = (u8 *)e1000e_dma_alloc(E1000E_BUF_SIZE);
        if (!d->rx_bufs[i]) return -1;
        d->rx_descs[i].addr = (u64)(uintptr_t)d->rx_bufs[i];
        d->rx_descs[i].status = 0;
    }
    d->rx_tail = 0;

    mmio_write32((volatile void *)((u8 *)d->mmio + E1000E_RCTL), 0);
    mmio_write32((volatile void *)((u8 *)d->mmio + E1000E_RDBAL),
                 (u32)(uintptr_t)d->rx_descs);
    mmio_write32((volatile void *)((u8 *)d->mmio + E1000E_RDBAH), 0);
    mmio_write32((volatile void *)((u8 *)d->mmio + E1000E_RDLEN),
                 (u32)(sizeof(e1000e_desc_t) * E1000E_NUM_DESC));
    mmio_write32((volatile void *)((u8 *)d->mmio + E1000E_RDH), 0);
    mmio_write32((volatile void *)((u8 *)d->mmio + E1000E_RDT),
                 E1000E_NUM_DESC - 1);
    mmio_write32((volatile void *)((u8 *)d->mmio + E1000E_RCTL),
                 E1000E_RCTL_EN | E1000E_RCTL_UPE | E1000E_RCTL_MPE |
                 E1000E_RCTL_BAM | E1000E_RCTL_SECRC);

    /* ---- TX ring ---- */
    d->tx_descs = (volatile e1000e_desc_t *)(uintptr_t)pmm_alloc_frame();
    if (!d->tx_descs) return -1;
    oc_memset((void *)d->tx_descs, 0, PMM_PAGE_SIZE);
    for (int i = 0; i < E1000E_NUM_DESC; i++) {
        d->tx_bufs[i] = (u8 *)e1000e_dma_alloc(E1000E_BUF_SIZE);
        if (!d->tx_bufs[i]) return -1;
    }
    d->tx_tail = 0;

    mmio_write32((volatile void *)((u8 *)d->mmio + E1000E_TCTL), 0);
    mmio_write32((volatile void *)((u8 *)d->mmio + E1000E_TDBAL),
                 (u32)(uintptr_t)d->tx_descs);
    mmio_write32((volatile void *)((u8 *)d->mmio + E1000E_TDBAH), 0);
    mmio_write32((volatile void *)((u8 *)d->mmio + E1000E_TDLEN),
                 (u32)(sizeof(e1000e_desc_t) * E1000E_NUM_DESC));
    mmio_write32((volatile void *)((u8 *)d->mmio + E1000E_TDH), 0);
    mmio_write32((volatile void *)((u8 *)d->mmio + E1000E_TDT), 0);
    mmio_write32((volatile void *)((u8 *)d->mmio + E1000E_TIPG), 0x0060200Au);
    mmio_write32((volatile void *)((u8 *)d->mmio + E1000E_TCTL),
                 E1000E_TCTL_EN | E1000E_TCTL_PSP |
                 (0x0Fu << 4) | (0x3Fu << 12));

    /* Bus master last (QEMU e1000/e1000e models cache the PCI command
     * register; same lesson as net.c). */
    e1000e_pci_enable(d);

    d->up = 1;
    return 0;
}

int e1000e_init(pci_dev_t *pdev) {
    if (g_e1000e.up) return 0;   /* already initialised */

    u8 bus = 0, dev = 0, func = 0;
    if (pdev) {
        bus = pdev->bus; dev = pdev->dev; func = pdev->func;
        u32 ids = pci_read_config(bus, dev, func, 0x00);
        u16 vid = (u16)(ids & 0xFFFF);
        u16 did = (u16)(ids >> 16);
        int known = (vid == 0x8086);
        if (known) {
            known = 0;
            for (unsigned i = 0; i < sizeof(e1000e_ids) / sizeof(e1000e_ids[0]); i++)
                if (did == e1000e_ids[i]) { known = 1; break; }
        }
        if (!known) return -1;
    } else {
        int found = 0;
        for (unsigned i = 0; i < sizeof(e1000e_ids) / sizeof(e1000e_ids[0]) && !found; i++) {
            if (pci_find_device(0x8086, e1000e_ids[i], &bus, &dev, &func) == 0)
                found = 1;
        }
        if (!found) return -1;
    }

    if (e1000e_setup(&g_e1000e, bus, dev, func) != 0) {
        oc_console_puts("e1000e: init failed\n");
        return -1;
    }

    nic_device_t nd;
    oc_memset(&nd, 0, sizeof(nd));
    oc_strcpy(nd.name, "e1000e");
    nd.type = NIC_TYPE_E1000E;
    for (int k = 0; k < 6; k++) nd.mac[k] = g_e1000e_mac[k];
    nd.bus = bus; nd.dev = dev; nd.func = func;
    nd.vendor_id = 0x8086;
    /* Re-read the device id for the registry record. */
    u32 ids = pci_read_config(bus, dev, func, 0x00);
    nd.device_id = (u16)(ids >> 16);
    nd.priv = &g_e1000e;
    int idx = nic_register(&nd, &e1000e_nic_ops);

    char line[96];
    char n[24];
    oc_strcpy(line, "e1000e: 8254x/82574 at ");
    oc_u64_to_hex(bus, n, 2);  oc_strcat(line, n); oc_strcat(line, ":");
    oc_u64_to_hex(dev, n, 2);  oc_strcat(line, n); oc_strcat(line, ".");
    oc_u64_to_hex(func, n, 1); oc_strcat(line, n);
    oc_strcat(line, " mac=");
    for (int k = 0; k < 6; k++) {
        oc_u64_to_hex(g_e1000e_mac[k], n, 2); oc_strcat(line, n);
        if (k < 5) oc_strcat(line, ":");
    }
    if (idx >= 0) { oc_strcat(line, " [registered]\n"); }
    else          { oc_strcat(line, " [registry full]\n"); }
    oc_console_puts(line);
    return idx >= 0 ? 0 : -1;
}

static int e1000e_ops_send(nic_device_t *ndev, const void *buf, int len) {
    (void)ndev;
    e1000e_dev_t *d = &g_e1000e;
    if (!d->up || len <= 0 || len > E1000E_BUF_SIZE) return -1;

    int slot = d->tx_tail;
    oc_memcpy(d->tx_bufs[slot], buf, len);
    d->tx_descs[slot].addr   = (u64)(uintptr_t)d->tx_bufs[slot];
    d->tx_descs[slot].length = (u16)len;
    d->tx_descs[slot].cso    = 0;
    d->tx_descs[slot].cmd    = E1000E_TX_CMD;
    d->tx_descs[slot].status = 0;
    d->tx_descs[slot].errors = 0;
    d->tx_descs[slot].special = 0;

    d->tx_tail = (d->tx_tail + 1) % E1000E_NUM_DESC;
    __asm__ volatile("sfence" ::: "memory");
    mmio_write32((volatile void *)((u8 *)d->mmio + E1000E_TDT),
                 (u32)d->tx_tail);

    /* Wait for the DD status bit (bounded). */
    for (int t = 0; t < 1000000; t++) {
        if (d->tx_descs[slot].status & E1000E_TX_DD) break;
    }
    d->tx_packets++;
    return len;
}

static int e1000e_ops_recv(nic_device_t *ndev, void *buf, int maxlen) {
    (void)ndev;
    e1000e_dev_t *d = &g_e1000e;
    if (!d->up) return -1;

    if (!(d->rx_descs[d->rx_tail].status & E1000E_RX_DD)) return 0;

    int len = d->rx_descs[d->rx_tail].length;
    if (len > maxlen) len = maxlen;
    if (len > 0) oc_memcpy(buf, d->rx_bufs[d->rx_tail], len);

    d->rx_descs[d->rx_tail].status = 0;
    d->rx_descs[d->rx_tail].addr =
        (u64)(uintptr_t)d->rx_bufs[d->rx_tail];
    d->rx_descs[d->rx_tail].length = 0;
    d->rx_descs[d->rx_tail].errors = 0;
    d->rx_tail = (d->rx_tail + 1) % E1000E_NUM_DESC;
    /* RDT points at the last available descriptor (QEMU ring-full rule,
     * same as the legacy e1000 path in net.c). */
    u32 rdt = (u32)((d->rx_tail + E1000E_NUM_DESC - 1) % E1000E_NUM_DESC);
    mmio_write32((volatile void *)((u8 *)d->mmio + E1000E_RDT), rdt);
    d->rx_packets++;
    return len;
}

static int e1000e_ops_link(nic_device_t *ndev) {
    (void)ndev;
    e1000e_dev_t *d = &g_e1000e;
    if (!d->up || !d->mmio) return -1;
    u32 st = mmio_read32((volatile void *)((u8 *)d->mmio + E1000E_STATUS));
    return (st & E1000E_STATUS_LU) ? 1 : 0;
}

static int e1000e_ops_mac(nic_device_t *ndev, u8 mac[6]) {
    (void)ndev;
    if (!mac) return -1;
    for (int k = 0; k < 6; k++) mac[k] = g_e1000e_mac[k];
    return 0;
}

void e1000e_print_state(void) {
    e1000e_dev_t *d = &g_e1000e;
    char line[128];
    char n[24];
    if (!d->up) {
        oc_console_puts("e1000e: not present\n");
        return;
    }
    u32 st = mmio_read32((volatile void *)((u8 *)d->mmio + E1000E_STATUS));
    oc_strcpy(line, "e1000e: pci=");
    oc_u64_to_hex(d->bus, n, 2); oc_strcat(line, n); oc_strcat(line, ":");
    oc_u64_to_hex(d->dev, n, 2); oc_strcat(line, n); oc_strcat(line, ".");
    oc_u64_to_hex(d->func, n, 1); oc_strcat(line, n);
    oc_strcat(line, " status=0x");
    oc_u64_to_hex(st, n, 8); oc_strcat(line, n);
    oc_strcat(line, " link=");
    oc_strcat(line, (st & E1000E_STATUS_LU) ? "up" : "down");
    oc_strcat(line, " tx=");
    oc_u64_to_str(d->tx_packets, n); oc_strcat(line, n);
    oc_strcat(line, " rx=");
    oc_u64_to_str(d->rx_packets, n); oc_strcat(line, n);
    oc_strcat(line, "\n");
    oc_console_puts(line);
}
