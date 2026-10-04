/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10b
 * File: kernel/driver_nic_rtl8139.c
 * Purpose: Realtek RTL8139 10/100 NIC driver (PCI 10EC:8139) for the
 *          WP-10b nic framework.  Also matches the PCI-bridged variants
 *          10EC:8138 (TP-Link) and 1113:1211 (Accton).
 *
 * Programming model (RTL8139 datasheet / OSDev reference):
 *   - BAR0 (I/O space, 0x100 bytes) is the register window; the driver
 *     uses I/O ports (inb/outb/inw/inl/outl).
 *   - Four fixed TX descriptor slots (TSAD0-3 / TSD0-3).
 *   - One contiguous RX ring buffer (64 KiB + 16 wrap) with a read
 *     pointer (CAPR); each packet is prefixed with a 4-byte header
 *     (status 16b + length 16b).
 *   - Polling mode: IMR = 0.
 */
#include "driver_nic.h"
#include "screen_console.h"
#include "lib_string.h"
#include "mem_pmm.h"

#include <stdint.h>

/* ---- x86 I/O port access ---- */
static inline void outb_p(u16 port, u8 v)  { __asm__ volatile("outb %0,%1" : : "a"(v), "Nd"(port)); }
static inline void outw_p(u16 port, u16 v) { __asm__ volatile("outw %0,%1" : : "a"(v), "Nd"(port)); }
static inline void outl_p(u16 port, u32 v) { __asm__ volatile("outl %0,%1" : : "a"(v), "Nd"(port)); }
static inline u8   inb_p(u16 port)         { u8 r; __asm__ volatile("inb %1,%0" : "=a"(r) : "Nd"(port)); return r; }
static inline u16  inw_p(u16 port)         { u16 r; __asm__ volatile("inw %1,%0" : "=a"(r) : "Nd"(port)); return r; }
static inline u32  inl_p(u16 port)         { u32 r; __asm__ volatile("inl %1,%0" : "=a"(r) : "Nd"(port)); return r; }

/* ---- Register offsets (I/O space; per Linux 8139too / datasheet) ---- */
#define R8139_IDR0     0x00   /* MAC, 6 bytes */
#define R8139_TSD0     0x10   /* TX status desc 0..3 (4B each) */
#define R8139_TSAD0    0x20   /* TX start address desc 0..3 (4B each) */
#define R8139_RBSTART  0x30   /* RX buffer start (4B) */
#define R8139_CR       0x37   /* command (1B) */
#define R8139_CAPR     0x38   /* current address of packet read (2B) */
#define R8139_IMR      0x3C   /* interrupt mask (2B) */
#define R8139_ISR      0x3E   /* interrupt status (2B) */
#define R8139_TCR      0x40   /* transmit config (4B) */
#define R8139_RCR      0x44   /* receive config (4B) */
#define R8139_MSR      0x58   /* media status (1B) */
#define R8139_TP_POLL  0xD9   /* transmit poll (normal queue = bit 6) */
#define R8139_CONFIG1  0x52

/* CR bits */
#define R8139_CR_RST   0x10
#define R8139_CR_RE    0x08
#define R8139_CR_TE    0x04
#define R8139_CR_BUFE  0x01

/* TSD bits -- NOTE the OWN semantics (matches QEMU's model and Linux
 * 8139too): the chip SETS bit13 when the descriptor is handed back to
 * the host, and a plain size write (bit13 clear) triggers the send.
 * Writing the bit as a "go" flag stalls the transmitter. */
#define R8139_TSD_TOK  0x00008000u   /* bit 15: transmit OK */
#define R8139_TSD_OWN  0x00002000u   /* bit 13: handed back to host */
#define R8139_TSD_SIZE 0x00001FFFu   /* bits 0-12 */

/* ISR bits */
#define R8139_ISR_ROK  0x01
#define R8139_ISR_TOK  0x04
#define R8139_ISR_ALL  0xFFFF

/* RCR bits */
#define R8139_RCR_AAP  0x00000001u   /* accept all physical */
#define R8139_RCR_APM  0x00000002u   /* accept physical match */
#define R8139_RCR_AM   0x00000004u   /* accept multicast */
#define R8139_RCR_AB   0x00000008u   /* accept broadcast */
#define R8139_RCR_WRAP 0x00000080u
#define R8139_RCR_FTH  (0x7u << 13)  /* FIFO threshold: none (store all) */

#define R8139_RX_RING  65536   /* datasheet: 64 KiB + 16 wrap */
#define R8139_RX_WRAP  16
#define R8139_TX_SLOTS 4

typedef struct {
    u16  io;
    u8   bus, dev, func;
    int  up;
    u8   mac[6];
    u8  *rx_ring;              /* 64 KiB + 16 wrap, identity-mapped */
    u32  rx_buf_phys;
    u16  capr;                 /* read offset into rx_ring */
    u8   tx_cur;               /* next TX slot */
    u8  *tx_bufs[R8139_TX_SLOTS];  /* identity-mapped DMA buffers */
    u64  tx_packets, rx_packets;
} driver_nic_rtl8139_dev_t;

static driver_nic_rtl8139_dev_t g_rtl8139;

static const u16 driver_nic_rtl8139_ids[] = { 0x8139, 0x8138 };
static const u16 driver_nic_rtl8139_vendors[] = { 0x10EC, 0x1113 };

static int driver_nic_rtl8139_ops_send(driver_nic_device_t *dev, const void *buf, int len);
static int driver_nic_rtl8139_ops_recv(driver_nic_device_t *dev, void *buf, int maxlen);
static int driver_nic_rtl8139_ops_link(driver_nic_device_t *dev);
static int driver_nic_rtl8139_ops_mac(driver_nic_device_t *dev, u8 mac[6]);

static const driver_nic_ops_t driver_nic_rtl8139_nic_ops = {
    .send        = driver_nic_rtl8139_ops_send,
    .recv        = driver_nic_rtl8139_ops_recv,
    .link_status = driver_nic_rtl8139_ops_link,
    .get_mac     = driver_nic_rtl8139_ops_mac,
};

static void driver_nic_rtl8139_pci_enable(driver_nic_rtl8139_dev_t *d) {
    u32 cs = driver_pci_read_config(d->bus, d->dev, d->func, 0x04);
    cs = (cs & 0xFFFF0000) | 0x0107;   /* IO | MEM | BM | SERR# */
    driver_pci_write_config(d->bus, d->dev, d->func, 0x04, cs);
}

static int driver_nic_rtl8139_setup(driver_nic_rtl8139_dev_t *d, u8 bus, u8 dev, u8 func) {
    memset(d, 0, sizeof(*d));
    d->bus = bus; d->dev = dev; d->func = func;

    /* BAR0 must be I/O space for this driver. */
    u32 bar0_raw = driver_pci_read_config(bus, dev, func, 0x10);
    if (!(bar0_raw & 0x1)) {
        /* MMIO-only BAR layout: still usable via the same offsets only
         * with MMIO accessors; keep it simple and fail clearly. */
        return -1;
    }
    d->io = (u16)(bar0_raw & 0xFFFC);
    driver_nic_rtl8139_pci_enable(d);

    /* MAC from IDR0-5. */
    for (int i = 0; i < 6; i++)
        d->mac[i] = inb_p(d->io + R8139_IDR0 + i);

    /* Software reset, then wait for it to clear. */
    outb_p(d->io + R8139_CR, R8139_CR_RST);
    for (int t = 0; t < 100000; t++) {
        if (!(inb_p(d->io + R8139_CR) & R8139_CR_RST)) break;
    }

    /* RX ring: one contiguous 64 KiB buffer (+16 wrap) from PMM.  The
     * chip wraps its internal RX pointer inside a 64 KiB window, so the
     * ring base must be 64 KiB aligned: allocate enough pages, carve
     * out the aligned block, and return the surplus frames. */
    {
        u32 ring_pages = (R8139_RX_RING + R8139_RX_WRAP) / PMM_PAGE_SIZE + 1;
        u64 raw = mem_pmm_alloc_contig(R8139_RX_RING / PMM_PAGE_SIZE + ring_pages);
        if (!raw) return -1;
        uintptr_t ring = (uintptr_t)raw;
        ring = (ring + R8139_RX_RING - 1) & ~((uintptr_t)R8139_RX_RING - 1);
        for (u64 pa = raw; pa < ring; pa += PMM_PAGE_SIZE)
            mem_pmm_free_frame(pa);
        u64 alloc_end = raw + (R8139_RX_RING / PMM_PAGE_SIZE + ring_pages)
                            * PMM_PAGE_SIZE;
        u64 ring_end = ring + (u64)ring_pages * PMM_PAGE_SIZE;
        for (u64 pa = ring_end; pa < alloc_end; pa += PMM_PAGE_SIZE)
            mem_pmm_free_frame(pa);
        d->rx_ring = (u8 *)ring;
    }
    memset(d->rx_ring, 0, R8139_RX_RING + R8139_RX_WRAP);
    d->rx_buf_phys = (u32)(uintptr_t)d->rx_ring;
    d->capr = 0;

    /* Unlock config registers then mask interrupts (polling). */
    outb_p(d->io + R8139_CONFIG1, inb_p(d->io + R8139_CONFIG1) & ~0x80);
    outw_p(d->io + R8139_IMR, 0);

    /* TX DMA buffers: identity-mapped PMM memory.  (Static .bss lives in
     * the -2 GiB kernel segment under -mcmodel=kernel; truncating those
     * addresses to the 32-bit DMA address produced garbage frames -- the
     * lesson is: every chip-visible buffer comes from PMM.) */
    for (int i = 0; i < R8139_TX_SLOTS; i++) {
        d->tx_bufs[i] = (u8 *)mem_pmm_alloc_frame();
        if (!d->tx_bufs[i]) return -1;
    }

    /* TX slots: clear all four. */
    for (int i = 0; i < R8139_TX_SLOTS; i++) {
        outl_p(d->io + R8139_TSAD0 + i * 4, 0);
        outl_p(d->io + R8139_TSD0 + i * 4, 0);
    }

    /* RX buffer + config. */
    outl_p(d->io + R8139_RBSTART, d->rx_buf_phys);
    outl_p(d->io + R8139_RCR,
           R8139_RCR_AAP | R8139_RCR_APM | R8139_RCR_AM |
           R8139_RCR_AB | R8139_RCR_WRAP | R8139_RCR_FTH);

    /* Acceptable transmit config: IFG normal + DMA burst max. */
    outl_p(d->io + R8139_TCR, 0x03000800u);

    /* Enable TX + RX. */
    outb_p(d->io + R8139_CR, R8139_CR_TE | R8139_CR_RE);

    d->up = 1;
    return 0;
}

int driver_nic_rtl8139_init(driver_pci_dev_t *pdev) {
    if (g_rtl8139.up) return 0;

    u8 bus = 0, dev = 0, func = 0;
    if (pdev) {
        bus = pdev->bus; dev = pdev->dev; func = pdev->func;
        u32 ids = driver_pci_read_config(bus, dev, func, 0x00);
        u16 vid = (u16)(ids & 0xFFFF);
        u16 did = (u16)(ids >> 16);
        int known = 0;
        for (unsigned i = 0; i < sizeof(driver_nic_rtl8139_ids) / sizeof(driver_nic_rtl8139_ids[0]); i++) {
            if (did == driver_nic_rtl8139_ids[i]) { known = 1; break; }
        }
        /* vendor check: Realtek or Accton */
        int vendor_ok = (vid == driver_nic_rtl8139_vendors[0]) || (vid == driver_nic_rtl8139_vendors[1]);
        if (!known || !vendor_ok) return -1;
    } else {
        int found = 0;
        for (unsigned i = 0; i < sizeof(driver_nic_rtl8139_ids) / sizeof(driver_nic_rtl8139_ids[0]) && !found; i++) {
            if (driver_pci_find_device(driver_nic_rtl8139_vendors[0], driver_nic_rtl8139_ids[i], &bus, &dev, &func) == 0)
                found = 1;
        }
        if (!found) {
            /* Accton-branded 8139 clone */
            if (driver_pci_find_device(0x1113, 0x1211, &bus, &dev, &func) == 0)
                found = 1;
        }
        if (!found) return -1;
    }

    if (driver_nic_rtl8139_setup(&g_rtl8139, bus, dev, func) != 0) {
        screen_console_puts("rtl8139: init failed\n");
        return -1;
    }

    driver_nic_device_t nd;
    memset(&nd, 0, sizeof(nd));
    strcpy(nd.name, "rtl8139");
    nd.type = NIC_TYPE_RTL8139;
    for (int k = 0; k < 6; k++) nd.mac[k] = g_rtl8139.mac[k];
    nd.bus = bus; nd.dev = dev; nd.func = func;
    nd.vendor_id = 0x10EC;
    u32 ids = driver_pci_read_config(bus, dev, func, 0x00);
    nd.device_id = (u16)(ids >> 16);
    nd.priv = &g_rtl8139;
    int idx = driver_nic_register(&nd, &driver_nic_rtl8139_nic_ops);

    char line[96];
    char n[24];
    strcpy(line, "rtl8139: 10/100 at ");
    u64_to_hex(bus, n, 2);  strcat(line, n); strcat(line, ":");
    u64_to_hex(dev, n, 2);  strcat(line, n); strcat(line, ".");
    u64_to_hex(func, n, 1); strcat(line, n);
    strcat(line, " io=0x");
    u64_to_hex(g_rtl8139.io, n, 4); strcat(line, n);
    strcat(line, " mac=");
    for (int k = 0; k < 6; k++) {
        u64_to_hex(g_rtl8139.mac[k], n, 2); strcat(line, n);
        if (k < 5) strcat(line, ":");
    }
    strcat(line, (idx >= 0) ? " [registered]\n" : " [registry full]\n");
    screen_console_puts(line);
    return idx >= 0 ? 0 : -1;
}

static int driver_nic_rtl8139_ops_send(driver_nic_device_t *ndev, const void *buf, int len) {
    (void)ndev;
    driver_nic_rtl8139_dev_t *d = &g_rtl8139;
    if (!d->up || len <= 0 || len > 1790) return -1;   /* datasheet max TX frame */

    int slot = d->tx_cur;
    /* Wait for an idle slot: after completion the chip sets TOK|OWN;
     * a slot whose TSD has neither bit is free.  Clear any stale state
     * before handing the buffer to the chip. */
    for (int t = 0; t < 1000000; t++) {
        u32 tsd = inl_p(d->io + R8139_TSD0 + slot * 4);
        if (!((tsd & R8139_TSD_TOK) && (tsd & R8139_TSD_OWN)) &&
            !(tsd & R8139_TSD_TOK)) break;
        outl_p(d->io + R8139_TSD0 + slot * 4, 0);
        break;
    }
    memcpy(d->tx_bufs[slot], buf, len);

    outl_p(d->io + R8139_TSAD0 + slot * 4,
           (u32)(uintptr_t)d->tx_bufs[slot]);
    /* Writing the size (with bit13 CLEAR) triggers the send; the chip
     * sets TOK when the frame is out. */
    outl_p(d->io + R8139_TSD0 + slot * 4,
           ((u32)len & R8139_TSD_SIZE));
    /* TP_POLL kick: harmless on hardware, required by some models. */
    outb_p(d->io + R8139_TP_POLL, 0x40);


    /* Wait for TOK (transmit OK). */
    int ok = 0;
    for (int t = 0; t < 1000000; t++) {
        u32 tsd = inl_p(d->io + R8139_TSD0 + slot * 4);
        if (tsd & R8139_TSD_TOK) { ok = 1; break; }
    }
    /* Acknowledge TX in ISR. */
    outw_p(d->io + R8139_ISR, R8139_ISR_TOK);
    d->tx_cur = (d->tx_cur + 1) % R8139_TX_SLOTS;
    if (ok) d->tx_packets++;
    return ok ? len : -1;
}

static int driver_nic_rtl8139_ops_recv(driver_nic_device_t *ndev, void *buf, int maxlen) {
    (void)ndev;
    driver_nic_rtl8139_dev_t *d = &g_rtl8139;
    if (!d->up) return -1;
    if (inb_p(d->io + R8139_CR) & R8139_CR_BUFE) return 0;   /* no data */

    u32 hdr = *(volatile u32 *)(d->rx_ring + d->capr);
    u16 status = (u16)(hdr & 0xFFFF);
    u16 plen = (u16)((hdr >> 16) & 0xFFFF);
    if (!(status & R8139_ISR_ROK) || plen == 0) {
        /* Bad header: reset the read pointer and bail out. */
        d->capr = 0;
        outw_p(d->io + R8139_CAPR, 0);
        outw_p(d->io + R8139_ISR, R8139_ISR_ROK);
        return 0;
    }

    u32 off = (u32)d->capr + 4;   /* skip the 4-byte header */
    int copy = plen;
    if (copy > maxlen) copy = maxlen;
    for (int i = 0; i < copy; i++) {
        ((u8 *)buf)[i] = d->rx_ring[(off + i) & (R8139_RX_RING - 1)];
    }

    /* Advance CAPR past the packet, 4-byte aligned, into the wrap area
     * as the datasheet requires (CAPR + length + 4, mod ring size). */
    u32 new_capr = ((u32)d->capr + plen + 4 + 3u) & ~3u;
    if (new_capr > R8139_RX_RING) new_capr -= R8139_RX_RING;
    d->capr = (u16)new_capr;
    outw_p(d->io + R8139_CAPR, (u16)(d->capr - R8139_RX_WRAP));
    outw_p(d->io + R8139_ISR, R8139_ISR_ROK);
    d->rx_packets++;
    return copy;
}

static int driver_nic_rtl8139_ops_link(driver_nic_device_t *ndev) {
    (void)ndev;
    driver_nic_rtl8139_dev_t *d = &g_rtl8139;
    if (!d->up) return -1;
    /* MSR bit2 = LinkB (0 = link up). */
    u8 msr = inb_p(d->io + R8139_MSR);
    return (msr & 0x04) ? 0 : 1;
}

static int driver_nic_rtl8139_ops_mac(driver_nic_device_t *ndev, u8 mac[6]) {
    (void)ndev;
    driver_nic_rtl8139_dev_t *d = &g_rtl8139;
    if (!mac) return -1;
    for (int k = 0; k < 6; k++) mac[k] = d->mac[k];
    return 0;
}

void driver_nic_rtl8139_print_state(void) {
    driver_nic_rtl8139_dev_t *d = &g_rtl8139;
    char line[128];
    char n[24];
    if (!d->up) {
        screen_console_puts("rtl8139: not present\n");
        return;
    }
    strcpy(line, "rtl8139: pci=");
    u64_to_hex(d->bus, n, 2); strcat(line, n); strcat(line, ":");
    u64_to_hex(d->dev, n, 2); strcat(line, n); strcat(line, ".");
    u64_to_hex(d->func, n, 1); strcat(line, n);
    strcat(line, " io=0x");
    u64_to_hex(d->io, n, 4); strcat(line, n);
    strcat(line, " link=");
    strcat(line, (driver_nic_rtl8139_ops_link(NULL) == 1) ? "up" : "down");
    strcat(line, " tx=");
    u64_to_str(d->tx_packets, n); strcat(line, n);
    strcat(line, " rx=");
    u64_to_str(d->rx_packets, n); strcat(line, n);
    strcat(line, "\n");
    screen_console_puts(line);
}
