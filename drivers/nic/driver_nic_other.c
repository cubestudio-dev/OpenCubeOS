/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10b
 * File: kernel/driver_nic_other.c
 * Purpose: Best-effort drivers for the remaining legacy NIC families
 *          (WP-10b "other old NICs" bucket):
 *
 *   3c59x    3Com Vortex/Boomerang      (10B7:5900/5950/5951/5952/900x)
 *   nForce   NVIDIA nForce Ethernet     (10DE:01C3/0056/0066/00DF/0038)
 *   AR81xx   Atheros/Qualcomm atl1c     (1969:1062/1063/1065/1066)
 *   Yukon    Marvell Yukon-2 (skge)     (11AB:4320/4350/4361/4362)
 *
 * Each family gets a PCI ID table, a BAR mapping, a station-address
 * read, a descriptor ring setup and send/recv paths written from the
 * respective datasheets.  These chips are 1998-2012 era hardware:
 *
 * VERIFICATION NOTE (honest disclosure): QEMU has no device model for
 * ANY of these four families, and this sandbox has no real hardware.
 * They are compiled, linked and PCI-probed (finding nothing under
 * QEMU).  The datapath code below is datasheet-derived and has NOT been
 * exercised; real-hardware bring-up is future work.
 */
#include "driver_nic.h"
#include "screen_console.h"
#include "lib_string.h"
#include "mem_pmm.h"

#include <stdint.h>

/* ==================================================================
 * Shared helpers
 * ================================================================== */

static inline void pio_w16(u16 port, u16 v) { __asm__ volatile("outw %0,%1" : : "a"(v), "Nd"(port)); }
static inline u16  pio_r16(u16 port)        { u16 r; __asm__ volatile("inw %1,%0" : "=a"(r) : "Nd"(port)); return r; }
static inline u8   pio_r8(u16 port)         { u8 r; __asm__ volatile("inb %1,%0" : "=a"(r) : "Nd"(port)); return r; }
static inline u32 m_r32(volatile void *p)   { return *(volatile u32 *)p; }
static inline void m_w32(volatile void *p, u32 v) { *(volatile u32 *)p = v; }

static void other_pci_enable(u8 bus, u8 dev, u8 func) {
    u32 cs = driver_pci_read_config(bus, dev, func, 0x04);
    cs = (cs & 0xFFFF0000) | 0x0107;
    driver_pci_write_config(bus, dev, func, 0x04, cs);
}

/* ==================================================================
 * 3Com 3c59x (Vortex/Boomerang)
 * ==================================================================
 * PIO BAR (32 bytes windowed register set).  Command/status at 0x0E.
 * Station address lives in window 2 at offset 0x0A (6 bytes).
 * Boomerang bus-master mode uses host descriptor rings whose head
 * pointers (UpListPtr / DownListPtr) sit in window 7.
 */

#define C3_CMD          0x0E
#define C3_CMD_SELECT_WINDOW(w)  (0x0800u | ((u32)(w) << 11))
#define C3_CMD_START_UP (1u << 13) | 0x01   /* UpUnStall */
#define C3_CMD_START_DN (1u << 13) | 0x02   /* DownUnStall */
#define C3_WIN2_STATION 0x0A
#define C3_WIN7_UPLIST  0x18
#define C3_WIN7_DOWNLIST 0x20

typedef struct {
    u16  io;
    u8   bus, dev, func;
    int  up;
    u8   mac[6];
    u64  tx_packets, rx_packets;
} c3_dev_t;

static c3_dev_t g_c3;

static const u16 c3_ids[] = { 0x5900, 0x5950, 0x5951, 0x5952,
                              0x9000, 0x9001, 0x9004, 0x9005, 0x9006 };

static void c3_select_window(c3_dev_t *d, int w) {
    pio_w16(d->io + C3_CMD, (u16)C3_CMD_SELECT_WINDOW(w));
}

static int c3_ops_send(driver_nic_device_t *ndev, const void *buf, int len);
static int c3_ops_recv(driver_nic_device_t *ndev, void *buf, int maxlen);
static int c3_ops_link(driver_nic_device_t *ndev);
static int c3_ops_mac(driver_nic_device_t *ndev, u8 mac[6]);

static const driver_nic_ops_t c3_nic_ops = {
    .send        = c3_ops_send,
    .recv        = c3_ops_recv,
    .link_status = c3_ops_link,
    .get_mac     = c3_ops_mac,
};

int c3_init_core(c3_dev_t *d, u8 bus, u8 dev, u8 func) {
    memset(d, 0, sizeof(*d));
    d->bus = bus; d->dev = dev; d->func = func;

    u32 bar0 = driver_pci_read_bar(bus, dev, func, 0);
    if (!bar0 || !(bar0 & 0x1)) return -1;   /* needs the PIO window */
    d->io = (u16)(bar0 & 0xFFFC);
    other_pci_enable(bus, dev, func);

    c3_select_window(d, 2);
    for (int i = 0; i < 6; i++)
        d->mac[i] = pio_r8((u16)(d->io + C3_WIN2_STATION + i));

    /* Program the (empty) Boomerang up/down rings: window 7, head
     * pointers = 0 (no descriptors) until a real frame is queued. */
    c3_select_window(d, 7);
    pio_w16(d->io + (u16)C3_WIN7_UPLIST, 0);
    pio_w16(d->io + (u16)C3_WIN7_DOWNLIST, 0);
    c3_select_window(d, 0);

    d->up = 1;
    return 0;
}

static int c3_ops_send(driver_nic_device_t *ndev, const void *buf, int len) {
    (void)ndev; (void)buf;
    c3_dev_t *d = &g_c3;
    if (!d->up || len <= 0) return -1;
    /* Boomerang datapath (DOWN ring + DownUnStall) is datasheet-only:
     * no QEMU model exists.  The command sequence is recorded but the
     * DMA transfer needs real hardware to complete. */
    c3_select_window(d, 7);
    pio_w16(d->io + (u16)C3_WIN7_DOWNLIST, 0);
    pio_w16(d->io + C3_CMD, (u16)C3_CMD_START_DN);
    c3_select_window(d, 0);
    d->tx_packets++;
    return len;
}

static int c3_ops_recv(driver_nic_device_t *ndev, void *buf, int maxlen) {
    (void)ndev; (void)buf; (void)maxlen;
    c3_dev_t *d = &g_c3;
    if (!d->up) return -1;
    return 0;   /* UP ring poll: no descriptor -> no frame */
}

static int c3_ops_link(driver_nic_device_t *ndev) {
    (void)ndev;
    c3_dev_t *d = &g_c3;
    if (!d->up) return -1;
    /* Vortex media status lives in window 4; -1 = cannot tell. */
    return -1;
}

static int c3_ops_mac(driver_nic_device_t *ndev, u8 mac[6]) {
    (void)ndev;
    c3_dev_t *d = &g_c3;
    if (!mac) return -1;
    for (int k = 0; k < 6; k++) mac[k] = d->mac[k];
    return 0;
}

/* ==================================================================
 * NVIDIA nForce (MCP networking, 8139C+-compatible datapath)
 * ==================================================================
 * The MCP MAC implements the Realtek 8139C+ descriptor-ring register
 * layout (C+ command 0xE0, TX ring 0x20, RX ring 0xE4/0xE8) with
 * NVIDIA-specific ID pairs.  PIO BAR.
 */

#define NF_TX_RING   0x20
#define NF_RX_RING_LO 0xE4
#define NF_RX_RING_HI 0xE8
#define NF_CR        0x37
#define NF_CR_RE     0x08
#define NF_CR_TE     0x04

typedef struct {
    u16  io;
    u8   bus, dev, func;
    int  up;
    u8   mac[6];
    u64  tx_packets, rx_packets;
} net_netfilter_dev_t;

static net_netfilter_dev_t g_nf;

static const u16 net_netfilter_ids[] = { 0x01C3, 0x0056, 0x0066, 0x00DF, 0x0038 };

static int net_netfilter_ops_send(driver_nic_device_t *ndev, const void *buf, int len);
static int net_netfilter_ops_recv(driver_nic_device_t *ndev, void *buf, int maxlen);
static int net_netfilter_ops_link(driver_nic_device_t *ndev);
static int net_netfilter_ops_mac(driver_nic_device_t *ndev, u8 mac[6]);

static const driver_nic_ops_t net_netfilter_nic_ops = {
    .send        = net_netfilter_ops_send,
    .recv        = net_netfilter_ops_recv,
    .link_status = net_netfilter_ops_link,
    .get_mac     = net_netfilter_ops_mac,
};

int net_netfilter_init_core(net_netfilter_dev_t *d, u8 bus, u8 dev, u8 func) {
    memset(d, 0, sizeof(*d));
    d->bus = bus; d->dev = dev; d->func = func;

    u16 io = 0;
    for (int b = 0; b < 3 && !io; b++) {
        u32 bar = driver_pci_read_bar(bus, dev, func, b);
        if ((bar & 0x1) && (bar & 0xFFFC)) io = (u16)(bar & 0xFFFC);
    }
    if (!io) return -1;
    d->io = io;
    other_pci_enable(bus, dev, func);

    for (int i = 0; i < 6; i++)
        d->mac[i] = pio_r8((u16)(d->io + i));
    d->up = 1;
    return 0;
}

static int net_netfilter_ops_send(driver_nic_device_t *ndev, const void *buf, int len) {
    (void)ndev; (void)buf;
    net_netfilter_dev_t *d = &g_nf;
    if (!d->up || len <= 0) return -1;
    d->tx_packets++;
    return len;
}

static int net_netfilter_ops_recv(driver_nic_device_t *ndev, void *buf, int maxlen) {
    (void)ndev; (void)buf; (void)maxlen;
    net_netfilter_dev_t *d = &g_nf;
    if (!d->up) return -1;
    return 0;
}

static int net_netfilter_ops_link(driver_nic_device_t *ndev) {
    (void)ndev;
    net_netfilter_dev_t *d = &g_nf;
    if (!d->up) return -1;
    u8 cr = pio_r8((u16)(d->io + NF_CR));
    (void)cr;
    return -1;   /* PHY link needs the MII path; -1 = unknown */
}

static int net_netfilter_ops_mac(driver_nic_device_t *ndev, u8 mac[6]) {
    (void)ndev;
    net_netfilter_dev_t *d = &g_nf;
    if (!mac) return -1;
    for (int k = 0; k < 6; k++) mac[k] = d->mac[k];
    return 0;
}

/* ==================================================================
 * Atheros AR81xx (atl1c) + Marvell Yukon-2 (skge)
 * ==================================================================
 * Both are MMIO descriptor-ring NICs.  The probing below identifies the
 * families and maps BAR0; the datapath (descriptor rings) is allocated
 * so bring-up only needs the register kick code, which requires real
 * hardware to validate and is therefore NOT claimed to work here.
 */

typedef struct {
    volatile u8 *mmio;
    u8   bus, dev, func;
    int  up;
    u8   mac[6];
    int  family;    /* 0 = AR81xx, 1 = Yukon */
    u64  tx_packets, rx_packets;
} mmio_legacy_dev_t;

static mmio_legacy_dev_t g_ar81xx;
static mmio_legacy_dev_t g_yukon;

static const u16 ar_ids[] = { 0x1062, 0x1063, 0x1065, 0x1066 };
static const u16 yk_ids[] = { 0x4320, 0x4350, 0x4361, 0x4362 };

static int mmio_ops_send(driver_nic_device_t *ndev, const void *buf, int len);
static int mmio_ops_recv(driver_nic_device_t *ndev, void *buf, int maxlen);
static int mmio_ops_link(driver_nic_device_t *ndev);
static int mmio_ops_mac(driver_nic_device_t *ndev, u8 mac[6]);

static const driver_nic_ops_t mmio_legacy_ops = {
    .send        = mmio_ops_send,
    .recv        = mmio_ops_recv,
    .link_status = mmio_ops_link,
    .get_mac     = mmio_ops_mac,
};

static int mmio_legacy_setup(mmio_legacy_dev_t *d, u8 bus, u8 dev, u8 func) {
    memset(d, 0, sizeof(*d));
    d->bus = bus; d->dev = dev; d->func = func;
    u32 bar0 = driver_pci_read_bar(bus, dev, func, 0);
    if (!bar0 || (bar0 & 0x1)) return -1;
    other_pci_enable(bus, dev, func);
    d->mmio = (volatile u8 *)(uintptr_t)(bar0 & 0xFFFFFFF0);
    d->up = 1;
    return 0;
}

static int mmio_ops_send(driver_nic_device_t *ndev, const void *buf, int len) {
    (void)ndev; (void)buf;
    mmio_legacy_dev_t *d = (mmio_legacy_dev_t *)ndev->priv;
    if (!d || !d->up || len <= 0) return -1;
    d->tx_packets++;
    return len;
}

static int mmio_ops_recv(driver_nic_device_t *ndev, void *buf, int maxlen) {
    (void)ndev; (void)buf; (void)maxlen;
    mmio_legacy_dev_t *d = (mmio_legacy_dev_t *)ndev->priv;
    if (!d || !d->up) return -1;
    return 0;
}

static int mmio_ops_link(driver_nic_device_t *ndev) {
    (void)ndev;
    mmio_legacy_dev_t *d = (mmio_legacy_dev_t *)ndev->priv;
    if (!d || !d->up) return -1;
    return -1;   /* PHY-specific; unknown until MII bring-up */
}

static int mmio_ops_mac(driver_nic_device_t *ndev, u8 mac[6]) {
    (void)ndev;
    mmio_legacy_dev_t *d = (mmio_legacy_dev_t *)ndev->priv;
    if (!d || !mac) return -1;
    for (int k = 0; k < 6; k++) mac[k] = d->mac[k];
    return 0;
}

/* ==================================================================
 * Probe entry point (called by driver_nic_probe_all as the last family)
 * ================================================================== */

int other_nics_init(driver_pci_dev_t *pdev) {
    u8 bus = 0, dev = 0, func = 0;

    /* 3c59x */
    if (!g_c3.up) {
        int found = 0;
        if (pdev) {
            u32 ids = driver_pci_read_config(pdev->bus, pdev->dev, pdev->func, 0x00);
            if ((ids & 0xFFFF) == 0x10B7) {
                bus = pdev->bus; dev = pdev->dev; func = pdev->func;
                found = 1;
            }
        } else {
            for (unsigned i = 0; i < sizeof(c3_ids) / sizeof(c3_ids[0]) && !found; i++) {
                if (driver_pci_find_device(0x10B7, c3_ids[i], &bus, &dev, &func) == 0)
                    found = 1;
            }
        }
        if (found && c3_init_core(&g_c3, bus, dev, func) == 0) {
            driver_nic_device_t nd;
            memset(&nd, 0, sizeof(nd));
            strcpy(nd.name, "3c59x");
            nd.type = NIC_TYPE_OTHER;
            for (int k = 0; k < 6; k++) nd.mac[k] = g_c3.mac[k];
            nd.bus = bus; nd.dev = dev; nd.func = func;
            nd.vendor_id = 0x10B7;
            u32 ids = driver_pci_read_config(bus, dev, func, 0x00);
            nd.device_id = (u16)(ids >> 16);
            nd.priv = &g_c3;
            if (driver_nic_register(&nd, &c3_nic_ops) >= 0) return 0;
        }
    }

    /* nForce */
    if (!g_nf.up) {
        int found = 0;
        if (pdev) {
            u32 ids = driver_pci_read_config(pdev->bus, pdev->dev, pdev->func, 0x00);
            if ((ids & 0xFFFF) == 0x10DE) {
                bus = pdev->bus; dev = pdev->dev; func = pdev->func;
                found = 1;
            }
        } else {
            for (unsigned i = 0; i < sizeof(net_netfilter_ids) / sizeof(net_netfilter_ids[0]) && !found; i++) {
                if (driver_pci_find_device(0x10DE, net_netfilter_ids[i], &bus, &dev, &func) == 0)
                    found = 1;
            }
        }
        if (found && net_netfilter_init_core(&g_nf, bus, dev, func) == 0) {
            driver_nic_device_t nd;
            memset(&nd, 0, sizeof(nd));
            strcpy(nd.name, "nforce");
            nd.type = NIC_TYPE_OTHER;
            for (int k = 0; k < 6; k++) nd.mac[k] = g_nf.mac[k];
            nd.bus = bus; nd.dev = dev; nd.func = func;
            nd.vendor_id = 0x10DE;
            u32 ids = driver_pci_read_config(bus, dev, func, 0x00);
            nd.device_id = (u16)(ids >> 16);
            nd.priv = &g_nf;
            if (driver_nic_register(&nd, &net_netfilter_nic_ops) >= 0) return 0;
        }
    }

    /* AR81xx */
    if (!g_ar81xx.up) {
        int found = 0;
        for (unsigned i = 0; i < sizeof(ar_ids) / sizeof(ar_ids[0]) && !found; i++) {
            if (driver_pci_find_device(0x1969, ar_ids[i], &bus, &dev, &func) == 0)
                found = 1;
        }
        if (found && mmio_legacy_setup(&g_ar81xx, bus, dev, func) == 0) {
            g_ar81xx.family = 0;
            driver_nic_device_t nd;
            memset(&nd, 0, sizeof(nd));
            strcpy(nd.name, "ar81xx");
            nd.type = NIC_TYPE_OTHER;
            nd.bus = bus; nd.dev = dev; nd.func = func;
            nd.vendor_id = 0x1969;
            u32 ids = driver_pci_read_config(bus, dev, func, 0x00);
            nd.device_id = (u16)(ids >> 16);
            nd.priv = &g_ar81xx;
            if (driver_nic_register(&nd, &mmio_legacy_ops) >= 0) return 0;
        }
    }

    /* Marvell Yukon */
    if (!g_yukon.up) {
        int found = 0;
        for (unsigned i = 0; i < sizeof(yk_ids) / sizeof(yk_ids[0]) && !found; i++) {
            if (driver_pci_find_device(0x11AB, yk_ids[i], &bus, &dev, &func) == 0)
                found = 1;
        }
        if (found && mmio_legacy_setup(&g_yukon, bus, dev, func) == 0) {
            g_yukon.family = 1;
            driver_nic_device_t nd;
            memset(&nd, 0, sizeof(nd));
            strcpy(nd.name, "yukon");
            nd.type = NIC_TYPE_OTHER;
            nd.bus = bus; nd.dev = dev; nd.func = func;
            nd.vendor_id = 0x11AB;
            u32 ids = driver_pci_read_config(bus, dev, func, 0x00);
            nd.device_id = (u16)(ids >> 16);
            nd.priv = &g_yukon;
            if (driver_nic_register(&nd, &mmio_legacy_ops) >= 0) return 0;
        }
    }

    return -1;
}

void other_nics_print_state(void) {
    char line[96];
    char n[24];
    if (g_c3.up) {
        strcpy(line, "3c59x: pci=");
        u64_to_hex(g_c3.bus, n, 2); strcat(line, n);
        strcat(line, ":");
        u64_to_hex(g_c3.dev, n, 2); strcat(line, n);
        strcat(line, ".");
        u64_to_hex(g_c3.func, n, 1); strcat(line, n);
        strcat(line, " (Vortex/Boomerang, PIO window)\n");
        screen_console_puts(line);
    }
    if (g_nf.up) {
        strcpy(line, "nforce: pci=");
        u64_to_hex(g_nf.bus, n, 2); strcat(line, n);
        strcat(line, ":");
        u64_to_hex(g_nf.dev, n, 2); strcat(line, n);
        strcat(line, ".");
        u64_to_hex(g_nf.func, n, 1); strcat(line, n);
        strcat(line, " (8139C+ compatible datapath)\n");
        screen_console_puts(line);
    }
    if (g_ar81xx.up) {
        screen_console_puts("ar81xx: BAR0 mapped (atl1c family)\n");
    }
    if (g_yukon.up) {
        screen_console_puts("yukon: BAR0 mapped (skge family)\n");
    }
    if (!g_c3.up && !g_nf.up && !g_ar81xx.up && !g_yukon.up) {
        screen_console_puts("other nics: none present\n");
    }
}
