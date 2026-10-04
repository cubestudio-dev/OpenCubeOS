/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10b
 * File: kernel/driver_nic_igb.c
 * Purpose: Intel 82575/82576 server GbE driver (igb) for the WP-10b
 *          nic framework.
 *
 * Hardware covered:
 *   8086:10C9  82575EB/82576
 *   8086:1521  82576-4 (server adapters, QEMU "-device igb")
 *
 * Programming model (82575/82576 datasheet; cross-checked against the
 * QEMU igb model so the QEMU bring-up is authoritative):
 *   - BAR0 MMIO 128 KiB.
 *   - Queue register blocks (legacy window): TX queue n at 0xE000 +
 *     n*0x40 (TDBAL/TDBAH/TDLEN/TDH/TDT/TXDCTL), RX queue n at 0xC000 +
 *     n*0x40 (RDBAL/.../RDT/SRRCTL/RXDCTL).  The driver programs TWO RX
 *     and TWO TX queues; queue 0 carries the data path, queue 1 is
 *     fully programmed and parked.
 *   - RXDCTL/TXDCTL.QUEUE_ENABLE (bit 25) is REQUIRED for the queue to
 *     move data; SRRCTL.DESCTYPE = 010b (advanced single-buffer).
 *   - Advanced descriptors on both directions:
 *       TX 16B: buffer_addr(8) | cmd_type_len(4) | olinfo_status(4);
 *               cmd = len | DTYP_DATA | DEXT | EOP | IFCS | RS;
 *               write-back DD (bit0) lands in olinfo_status.
 *       RX 16B: pkt_addr(8) | hdr_addr(8, unused); write-back overlays
 *               hdr area: status_error(2, DD=bit0), length(2), vlan(2).
 *   - Polling mode: interrupts masked via IMC.
 */
#include "driver_nic.h"
#include "screen_console.h"
#include "lib_string.h"
#include "mem_pmm.h"
#include "core_timer.h"

#include <stdint.h>

static inline u32 mmio_read32(volatile void *p) { return *(volatile u32 *)p; }
static inline void mmio_write32(volatile void *p, u32 v) { *(volatile u32 *)p = v; }

/* ---- Global registers ---- */
#define IGB_CTRL      0x0000
#define IGB_STATUS    0x0008
#define IGB_IMC       0x00D8
#define IGB_RCTL      0x0100
#define IGB_TCTL      0x0400
#define IGB_TIPG      0x0410
#define IGB_RAL0      0x5400

#define IGB_CTRL_SLU    (1u << 6)
#define IGB_CTRL_ASDE   (1u << 5)
#define IGB_CTRL_FD     (1u << 0)
#define IGB_STATUS_LU   (1u << 1)
#define IGB_RCTL_EN     0x00000002u
#define IGB_RCTL_UPE    0x00000008u
#define IGB_RCTL_MPE    0x00000010u
#define IGB_RCTL_BAM    0x00008000u
#define IGB_RCTL_SECRC  0x04000000u
#define IGB_TCTL_EN     (1u << 1)
#define IGB_TCTL_PSP    (1u << 3)

/* ---- Queue register blocks ---- */
#define IGB_TXQ_BASE(q)  (0xE000u + (u32)(q) * 0x40u)
#define IGB_RXQ_BASE(q)  (0xC000u + (u32)(q) * 0x40u)
#define IGB_Q_DBAL  0x00
#define IGB_Q_DBAH  0x04
#define IGB_Q_DLEN  0x08
#define IGB_Q_DH    0x10
#define IGB_Q_DT    0x18
#define IGB_Q_DCTL  0x28   /* RXDCTL / TXDCTL (queue enable, bit 25) */

#define IGB_SRRCTL_DESCTYPE_ADV_ONEBUF  0x02000000u
#define IGB_SRRCTL_BSIZEPKT_2KB         (2u << 10)

#define IGB_Q_ENABLE    0x02000000u  /* RXDCTL/TXDCTL queue-enable bit 25 */

/* ---- Advanced descriptor command/status bits ---- */
#define IGB_ADVTXD_DTYP_DATA  0x00300000u
#define IGB_ADVTXD_DEXT       0x20000000u
#define IGB_TXD_CMD_EOP       0x01000000u
#define IGB_TXD_CMD_IFCS      0x02000000u
#define IGB_TXD_CMD_RS        0x08000000u
#define IGB_STAT_DD           0x00000001u

#define IGB_NUM_QUEUES  2     /* programmed queues (0 = data path) */
#define IGB_NUM_DESC    32
#define IGB_BUF_SIZE    2048

/* Advanced TX descriptor. */
typedef struct {
    u64 addr;
    u32 cmd_type_len;
    u32 olinfo_status;    /* WB: status (DD bit0) */
} __attribute__((packed)) driver_nic_igb_tx_desc_t;

/* Advanced RX descriptor -- 16 bytes TOTAL.  Write-back layout per the
 * QEMU igb model (which matches e1000e): status_error is a 32-bit word
 * at offset 8 (DD = bit0), packet length at offset 12, vlan at 14.
 * The write-back overlays the header-address half (offset 8-15). */
typedef struct {
    u64 pkt_addr;         /* read: data buffer */
    union {
        u64 hdr_addr;     /* read: header buffer (0 = none) */
        struct {
            u32 status_error;   /* WB offset 8:  DD = bit0 */
            u16 length;         /* WB offset 12: packet length */
            u16 vlan;           /* WB offset 14 */
        } wb;
    };
} __attribute__((packed)) driver_nic_igb_rx_desc_t;

typedef struct {
    volatile u32 *mmio;
    volatile driver_nic_igb_tx_desc_t *tx_descs[IGB_NUM_QUEUES];
    volatile driver_nic_igb_rx_desc_t *rx_descs[IGB_NUM_QUEUES];
    u8 *tx_bufs[IGB_NUM_QUEUES][IGB_NUM_DESC];
    u8 *rx_bufs[IGB_NUM_QUEUES][IGB_NUM_DESC];
    int rx_tail;
    int tx_tail;
    u8  bus, dev, func;
    int up;
    u8  mac[6];
    u64 tx_packets, rx_packets;
} driver_nic_igb_dev_t;

static driver_nic_igb_dev_t g_igb;

static const u16 driver_nic_igb_ids[] = { 0x10C9, 0x1521 };

static u8 *driver_nic_igb_dma_page = NULL;
static int driver_nic_igb_dma_off = 0;

static void *driver_nic_igb_dma_alloc(int size) {
    if (size > (int)PMM_PAGE_SIZE) return (void *)(uintptr_t)mem_pmm_alloc_frame();
    if (driver_nic_igb_dma_page == NULL || driver_nic_igb_dma_off + size > (int)PMM_PAGE_SIZE) {
        driver_nic_igb_dma_page = (u8 *)(uintptr_t)mem_pmm_alloc_frame();
        if (driver_nic_igb_dma_page == NULL) return NULL;
        driver_nic_igb_dma_off = 0;
    }
    driver_nic_igb_dma_off = (driver_nic_igb_dma_off + 15) & ~15;
    void *p = driver_nic_igb_dma_page + driver_nic_igb_dma_off;
    driver_nic_igb_dma_off += size;
    return p;
}

static void driver_nic_igb_pci_enable(driver_nic_igb_dev_t *d) {
    u32 cs = driver_pci_read_config(d->bus, d->dev, d->func, 0x04);
    cs = (cs & 0xFFFF0000) | 0x0107;
    driver_pci_write_config(d->bus, d->dev, d->func, 0x04, cs);
}

static u32 driver_nic_igb_reg(driver_nic_igb_dev_t *d, u32 off) {
    return mmio_read32((volatile void *)((u8 *)d->mmio + off));
}
static void driver_nic_igb_wreg(driver_nic_igb_dev_t *d, u32 off, u32 v) {
    mmio_write32((volatile void *)((u8 *)d->mmio + off), v);
}

static int driver_nic_igb_setup_txq(driver_nic_igb_dev_t *d, int q) {
    d->tx_descs[q] =
        (volatile driver_nic_igb_tx_desc_t *)(uintptr_t)mem_pmm_alloc_frame();
    if (!d->tx_descs[q]) return -1;
    memset((void *)d->tx_descs[q], 0, PMM_PAGE_SIZE);
    u32 base = IGB_TXQ_BASE(q);
    for (int i = 0; i < IGB_NUM_DESC; i++) {
        d->tx_bufs[q][i] = (u8 *)driver_nic_igb_dma_alloc(IGB_BUF_SIZE);
        if (!d->tx_bufs[q][i]) return -1;
    }
    driver_nic_igb_wreg(d, base + IGB_Q_DBAL, (u32)(uintptr_t)d->tx_descs[q]);
    driver_nic_igb_wreg(d, base + IGB_Q_DBAH, 0);
    driver_nic_igb_wreg(d, base + IGB_Q_DLEN,
             (u32)(sizeof(driver_nic_igb_tx_desc_t) * IGB_NUM_DESC));
    driver_nic_igb_wreg(d, base + IGB_Q_DH, 0);
    driver_nic_igb_wreg(d, base + IGB_Q_DT, 0);
    driver_nic_igb_wreg(d, base + IGB_Q_DCTL, IGB_Q_ENABLE);
    return 0;
}

static int driver_nic_igb_setup_rxq(driver_nic_igb_dev_t *d, int q) {
    d->rx_descs[q] =
        (volatile driver_nic_igb_rx_desc_t *)(uintptr_t)mem_pmm_alloc_frame();
    if (!d->rx_descs[q]) return -1;
    memset((void *)d->rx_descs[q], 0, PMM_PAGE_SIZE);
    u32 base = IGB_RXQ_BASE(q);
    for (int i = 0; i < IGB_NUM_DESC; i++) {
        d->rx_bufs[q][i] = (u8 *)driver_nic_igb_dma_alloc(IGB_BUF_SIZE);
        if (!d->rx_bufs[q][i]) return -1;
        d->rx_descs[q][i].pkt_addr = (u64)(uintptr_t)d->rx_bufs[q][i];
        d->rx_descs[q][i].hdr_addr = 0;
        d->rx_descs[q][i].wb.status_error = 0;
        d->rx_descs[q][i].wb.length = 0;
    }
    driver_nic_igb_wreg(d, base + IGB_Q_DBAL, (u32)(uintptr_t)d->rx_descs[q]);
    driver_nic_igb_wreg(d, base + IGB_Q_DBAH, 0);
    driver_nic_igb_wreg(d, base + IGB_Q_DLEN,
             (u32)(sizeof(driver_nic_igb_rx_desc_t) * IGB_NUM_DESC));
    driver_nic_igb_wreg(d, base + IGB_Q_DH, 0);
    driver_nic_igb_wreg(d, base + IGB_Q_DT, IGB_NUM_DESC - 1);
    /* SRRCTL: advanced single-buffer descriptors, 2 KiB buffers. */
    driver_nic_igb_wreg(d, base + 0x0C,
             IGB_SRRCTL_DESCTYPE_ADV_ONEBUF | IGB_SRRCTL_BSIZEPKT_2KB);
    driver_nic_igb_wreg(d, base + IGB_Q_DCTL, IGB_Q_ENABLE);
    return 0;
}

static int driver_nic_igb_setup(driver_nic_igb_dev_t *d, u8 bus, u8 dev, u8 func) {
    memset(d, 0, sizeof(*d));
    d->bus = bus; d->dev = dev; d->func = func;

    u32 bar0 = driver_pci_read_bar(bus, dev, func, 0);
    if (!bar0 || (bar0 & 0x1)) return -1;   /* needs MMIO */
    driver_nic_igb_pci_enable(d);
    d->mmio = (volatile u32 *)(uintptr_t)(bar0 & 0xFFFFFFF0);

    /* MAC: RAL0/RAH0. */
    u32 mac_low = driver_nic_igb_reg(d, IGB_RAL0);
    u32 mac_high = driver_nic_igb_reg(d, IGB_RAL0 + 4);
    d->mac[0] = mac_low & 0xFF;
    d->mac[1] = (mac_low >> 8) & 0xFF;
    d->mac[2] = (mac_low >> 16) & 0xFF;
    d->mac[3] = (mac_low >> 24) & 0xFF;
    d->mac[4] = mac_high & 0xFF;
    d->mac[5] = (mac_high >> 8) & 0xFF;
    if (d->mac[0] == 0 && d->mac[1] == 0 && d->mac[2] == 0) {
        /* No NVM address visible: synthesise a locally administered one
         * derived from the PCI address (never all-zero) and program it
         * back so hardware filtering matches. */
        d->mac[0] = 0x02;
        d->mac[1] = 0x00;
        d->mac[2] = 0x5E;
        d->mac[3] = bus;
        d->mac[4] = dev;
        d->mac[5] = func;
        u32 lo = d->mac[0] | ((u32)d->mac[1] << 8) |
                 ((u32)d->mac[2] << 16) | ((u32)d->mac[3] << 24);
        u32 hi = d->mac[4] | ((u32)d->mac[5] << 8) | (1u << 31);
        driver_nic_igb_wreg(d, IGB_RAL0, lo);
        driver_nic_igb_wreg(d, IGB_RAL0 + 4, hi);
    }

    driver_nic_igb_wreg(d, IGB_CTRL, IGB_CTRL_FD | IGB_CTRL_ASDE | IGB_CTRL_SLU);
    driver_nic_igb_wreg(d, IGB_IMC, 0xFFFFFFFFu);

    /* Start PHY auto-negotiation via MDIC (PHY 1, register 0 = BMCR).
     * Without it STATUS.LU stays clear: the 82576 only delivers inbound
     * frames when the link is up (e1000x_rx_ready checks LU), so DHCP
     * offers would never reach the RX ring.  BMCR = 100M | full duplex
     * | AN enable | AN restart.  The negotiation completes after a
     * short delay, so poll STATUS.LU for up to ~2 s. */
    {
        const u32 MDIC   = 0x00020u;
        const u32 OP_WR  = 0x04000000u;
        const u32 READY  = 0x10000000u;
        u32 mdic = 0x1340u | (1u << 21) | OP_WR;   /* data | PHY1 | write */
        driver_nic_igb_wreg(d, MDIC, mdic);
        for (int t = 0; t < 1000000; t++) {
            if (driver_nic_igb_reg(d, MDIC) & READY) break;
        }
        u64 t0 = core_timer_ticks();
        while (core_timer_ticks() - t0 < 200) {          /* 200 ticks = 2 s */
            if (driver_nic_igb_reg(d, IGB_STATUS) & IGB_STATUS_LU) break;
            for (volatile int spin = 0; spin < 1000; spin++) { }
        }
    }

    for (int q = 0; q < IGB_NUM_QUEUES; q++) {
        if (driver_nic_igb_setup_txq(d, q) != 0) return -1;
        if (driver_nic_igb_setup_rxq(d, q) != 0) return -1;
    }

    driver_nic_igb_wreg(d, IGB_RCTL, 0);
    driver_nic_igb_wreg(d, IGB_RCTL,
             IGB_RCTL_EN | IGB_RCTL_UPE | IGB_RCTL_MPE |
             IGB_RCTL_BAM | IGB_RCTL_SECRC);
    driver_nic_igb_wreg(d, IGB_TIPG, 0x0060200Au);
    driver_nic_igb_wreg(d, IGB_TCTL,
             IGB_TCTL_EN | IGB_TCTL_PSP | (0x0Fu << 4) | (0x3Fu << 12));

    driver_nic_igb_pci_enable(d);
    d->up = 1;

    return 0;
}

static int driver_nic_igb_ops_send(driver_nic_device_t *ndev, const void *buf, int len);
static int driver_nic_igb_ops_recv(driver_nic_device_t *ndev, void *buf, int maxlen);
static int driver_nic_igb_ops_link(driver_nic_device_t *ndev);
static int driver_nic_igb_ops_mac(driver_nic_device_t *ndev, u8 mac[6]);

static const driver_nic_ops_t driver_nic_igb_nic_ops = {
    .send        = driver_nic_igb_ops_send,
    .recv        = driver_nic_igb_ops_recv,
    .link_status = driver_nic_igb_ops_link,
    .get_mac     = driver_nic_igb_ops_mac,
};

int driver_nic_igb_init(driver_pci_dev_t *pdev) {
    if (g_igb.up) return 0;

    u8 bus = 0, dev = 0, func = 0;
    if (pdev) {
        bus = pdev->bus; dev = pdev->dev; func = pdev->func;
        u32 ids = driver_pci_read_config(bus, dev, func, 0x00);
        u16 vid = (u16)(ids & 0xFFFF);
        u16 did = (u16)(ids >> 16);
        int known = (vid == 0x8086);
        for (unsigned i = 0; known && i < sizeof(driver_nic_igb_ids) / sizeof(driver_nic_igb_ids[0]); i++)
            if (did == driver_nic_igb_ids[i]) known = 1;
        if (!known) return -1;
    } else {
        int found = 0;
        for (unsigned i = 0; i < sizeof(driver_nic_igb_ids) / sizeof(driver_nic_igb_ids[0]) && !found; i++) {
            if (driver_pci_find_device(0x8086, driver_nic_igb_ids[i], &bus, &dev, &func) == 0)
                found = 1;
        }
        if (!found) return -1;
    }

    if (driver_nic_igb_setup(&g_igb, bus, dev, func) != 0) {
        screen_console_puts("igb: init failed\n");
        return -1;
    }

    driver_nic_device_t nd;
    memset(&nd, 0, sizeof(nd));
    strcpy(nd.name, "igb");
    nd.type = NIC_TYPE_IGB;
    for (int k = 0; k < 6; k++) nd.mac[k] = g_igb.mac[k];
    nd.bus = bus; nd.dev = dev; nd.func = func;
    nd.vendor_id = 0x8086;
    u32 ids = driver_pci_read_config(bus, dev, func, 0x00);
    nd.device_id = (u16)(ids >> 16);
    nd.priv = &g_igb;
    int idx = driver_nic_register(&nd, &driver_nic_igb_nic_ops);

    char line[96];
    char n[24];
    strcpy(line, "igb: 82575/82576 at ");
    u64_to_hex(bus, n, 2);  strcat(line, n); strcat(line, ":");
    u64_to_hex(dev, n, 2);  strcat(line, n); strcat(line, ".");
    u64_to_hex(func, n, 1); strcat(line, n);
    strcat(line, " mac=");
    for (int k = 0; k < 6; k++) {
        u64_to_hex(g_igb.mac[k], n, 2); strcat(line, n);
        if (k < 5) strcat(line, ":");
    }
    strcat(line, (idx >= 0) ? " [registered]\n" : " [registry full]\n");
    screen_console_puts(line);
    return idx >= 0 ? 0 : -1;
}

static int driver_nic_igb_ops_send(driver_nic_device_t *ndev, const void *buf, int len) {
    (void)ndev;
    driver_nic_igb_dev_t *d = &g_igb;
    if (!d->up || len <= 0 || len > IGB_BUF_SIZE) return -1;

    int slot = d->tx_tail;
    /* Wait until the descriptor write-back shows DD (done). */
    for (int t = 0; t < 1000000; t++) {
        if (d->tx_descs[0][slot].olinfo_status & IGB_STAT_DD) break;
    }
    memcpy(d->tx_bufs[0][slot], buf, len);
    d->tx_descs[0][slot].addr = (u64)(uintptr_t)d->tx_bufs[0][slot];
    d->tx_descs[0][slot].olinfo_status = 0;
    __asm__ volatile("sfence" ::: "memory");
    d->tx_descs[0][slot].cmd_type_len =
        ((u32)len & 0xFFFFu) |
        IGB_ADVTXD_DTYP_DATA | IGB_ADVTXD_DEXT |
        IGB_TXD_CMD_EOP | IGB_TXD_CMD_IFCS | IGB_TXD_CMD_RS;

    d->tx_tail = (d->tx_tail + 1) % IGB_NUM_DESC;
    driver_nic_igb_wreg(d, IGB_TXQ_BASE(0) + IGB_Q_DT, (u32)d->tx_tail);

    /* Wait for the write-back DD bit (RS was set). */
    int ok = 0;
    for (int t = 0; t < 1000000; t++) {
        if (d->tx_descs[0][slot].olinfo_status & IGB_STAT_DD) { ok = 1; break; }
    }
    if (ok) d->tx_packets++;
    return ok ? len : -1;
}

static int driver_nic_igb_ops_recv(driver_nic_device_t *ndev, void *buf, int maxlen) {
    (void)ndev;
    driver_nic_igb_dev_t *d = &g_igb;
    if (!d->up) return -1;


    volatile driver_nic_igb_rx_desc_t *desc = &d->rx_descs[0][d->rx_tail];
    if (!(desc->wb.status_error & IGB_STAT_DD)) return 0;

    int len = desc->wb.length;
    if (len > maxlen) len = maxlen;
    if (len > 0) memcpy(buf, d->rx_bufs[0][d->rx_tail], len);


    /* Recycle: clear the write-back and re-present the buffer. */
    desc->wb.status_error = 0;
    desc->wb.length = 0;
    desc->wb.vlan = 0;
    desc->hdr_addr = 0;
    desc->pkt_addr = (u64)(uintptr_t)d->rx_bufs[0][d->rx_tail];
    __asm__ volatile("sfence" ::: "memory");
    d->rx_tail = (d->rx_tail + 1) % IGB_NUM_DESC;
    u32 rdt = (u32)((d->rx_tail + IGB_NUM_DESC - 1) % IGB_NUM_DESC);
    driver_nic_igb_wreg(d, IGB_RXQ_BASE(0) + IGB_Q_DT, rdt);
    d->rx_packets++;
    return len;
}

static int driver_nic_igb_ops_link(driver_nic_device_t *ndev) {
    (void)ndev;
    driver_nic_igb_dev_t *d = &g_igb;
    if (!d->up || !d->mmio) return -1;
    return (driver_nic_igb_reg(d, IGB_STATUS) & IGB_STATUS_LU) ? 1 : 0;
}

static int driver_nic_igb_ops_mac(driver_nic_device_t *ndev, u8 mac[6]) {
    (void)ndev;
    driver_nic_igb_dev_t *d = &g_igb;
    if (!mac) return -1;
    for (int k = 0; k < 6; k++) mac[k] = d->mac[k];
    return 0;
}

void driver_nic_igb_print_state(void) {
    driver_nic_igb_dev_t *d = &g_igb;
    char line[128];
    char n[24];
    if (!d->up) {
        screen_console_puts("igb: not present\n");
        return;
    }
    strcpy(line, "igb: pci=");
    u64_to_hex(d->bus, n, 2); strcat(line, n); strcat(line, ":");
    u64_to_hex(d->dev, n, 2); strcat(line, n); strcat(line, ".");
    u64_to_hex(d->func, n, 1); strcat(line, n);
    strcat(line, " queues=");
    u64_to_str(IGB_NUM_QUEUES, n); strcat(line, n);
    strcat(line, " link=");
    strcat(line, (driver_nic_igb_reg(d, IGB_STATUS) & IGB_STATUS_LU) ? "up" : "down");
    strcat(line, " tx=");
    u64_to_str(d->tx_packets, n); strcat(line, n);
    strcat(line, " rx=");
    u64_to_str(d->rx_packets, n); strcat(line, n);
    strcat(line, "\n");
    screen_console_puts(line);
}
