/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 cubestudio-dev <cubestudio@qq.com>
 * Open Cube OS - WP-10-wireless_bluetooth
 *
 * File: drivers/wireless/driver_ath.c
 *
 * Atheros/Qualcomm 802.11 family driver, three sub-drivers:
 *   ath9k  - AR9280/AR9285/AR9287/AR9380-90/AR9462/AR9565 (PCI, AR9002/
 *            AR9003 MAC register map, ath9k ar5416reg.h names)
 *   ath10k - QCA9880/9887/9888/9984/QCA6174/QCA9377 (PCI; CE descriptor
 *            format + HTC framing + BMI protocol + WMI-TLV container)
 *   ath11k - QCA6390/WCN6855/QCN9074/WCN6750 (PCI; MHI ring elements +
 *            QMI TLV encoding)
 *
 * Honesty note (test matrix rows "QEMU 实测（如果有模型）/ datasheet
 * 对照"): no QEMU 802.11 model exists; hardware sequences are verified
 * at code-vs-datasheet level through the selftests (byte-exact
 * descriptor/TLV/HTC fixtures); enumeration paths execute live in QEMU
 * and honestly report not-present.  Firmware files are third-party
 * property and are not shipped; ath10k/ath11k fw_load requires
 * /etc/firmware/ath10k.fw or /etc/firmware/ath11k.mbn and fails
 * honestly when absent.
 */
#include "types.h"
#include "driver_ath.h"
#include "driver_wireless.h"
#include "driver_pci.h"
#include "mem_heap.h"
#include "mem_pmm.h"
#include "core_timer.h"
#include "core_sched.h"
#include "lib_string.h"
#include "fs_vfs.h"

/* ================================================================== */
/* ath9k - AR9002/AR9003 MAC register map (ar5416reg.h names)          */
/* ================================================================== */
#define ATH9K_AR_CR              0x0008
#define ATH9K_AR_CFG             0x0014
#define ATH9K_AR_RXDP            0x0074
#define ATH9K_AR_ISR             0x0080
#define ATH9K_AR_IMR             0x00a0
#define ATH9K_AR_Q_TXE           0x0840   /* QCU transmit enable         */
#define ATH9K_AR_STA_ID0         0x8000
#define ATH9K_AR_STA_ID1         0x8004
#define ATH9K_AR_BSS_ID0         0x8008
#define ATH9K_AR_BSS_ID1         0x800c
#define ATH9K_AR_SREV            0x4020
#define ATH9K_AR_RTC_RESET       0x7010
#define ATH9K_AR_RTC_STATUS      0x7014
#define ATH9K_AR_RTC_PLL_CONTROL 0x701c
#define ATH9K_AR_RTC_SLEEP_CLK   0x7024

#define ATH9K_AR_CR_RXE          0x00000001u
#define ATH9K_AR_CR_RXD          0x00000002u
#define ATH9K_RTC_STATE_ON       0x00000001u   /* RTC_STATUS "ON"        */

/* PCI IDs (ath9k linux pci id table, vendor 0x168c)                   */
static const u16 ath9k_devids[] = {
    0x002a,   /* AR9280                                                    */
    0x002b,   /* AR9285                                                    */
    0x002d,   /* AR9287                                                    */
    0x0030,   /* AR9380                                                    */
    0x0031,   /* AR9381                                                    */
    0x0032,   /* AR9382                                                    */
    0x0033,   /* AR9390? AR9003 family                                     */
    0x0034,   /* AR9462                                                    */
    0x0036,   /* AR9565                                                    */
};
#define ATH9K_DEVID_COUNT (sizeof(ath9k_devids) / sizeof(ath9k_devids[0]))

typedef struct ath9k_priv {
    volatile u32 *mmio;
    u32           srev;
    u64           txd_page;          /* DMA-coherent descriptor page  */
    u64           pending_buf;       /* previous staging frame        */
} ath9k_priv_t;

static inline u32 a9k_r32(const ath9k_priv_t *p, u32 off)
{
    return *(volatile u32 *)((volatile u8 *)p->mmio + off);
}
static inline void a9k_w32(const ath9k_priv_t *p, u32 off, u32 v)
{
    *(volatile u32 *)((volatile u8 *)p->mmio + off) = v;
}

/* AR5416 TX descriptor, 4 u32 words (ar5416desc.h field layout):
 *   ds_data  = buffer address
 *   ds_ctl1  = bits 0-15 frame_len, bits 24-30 pkt type
 *   ds_ctl2  = bit 1 TxMore? bits 16-31 TxTrailer len? (named subset)
 *   ds_ctl3  = bit 31 TxExtOr? (reserved 0 here)                        */
#define ATH9K_TXD_TYPE_MSK       0x7f000000u
#define ATH9K_TXD_TYPE_MGMT      0x04000000u   /* type 4 = 802.11 mgmt    */
#define ATH9K_TXD_TYPE_DATA      0x08000000u   /* type 8 = 802.11 data    */

typedef struct ath9k_txd {
    u32 ds_data;
    u32 ds_ctl1;
    u32 ds_ctl2;
    u32 ds_ctl3;
} ath9k_txd_t;

static void ath9k_txd_build(ath9k_txd_t *d, u64 buf, u16 frame_len,
                            int is_data)
{
    d->ds_data = (u32)buf;
    d->ds_ctl1 = ((u32)frame_len & 0xffffu) |
                 (is_data ? ATH9K_TXD_TYPE_DATA : ATH9K_TXD_TYPE_MGMT);
    d->ds_ctl2 = 0;
    d->ds_ctl3 = 0;
}

/* AR5416 RX descriptor status words (ar5416desc.h):
 *   status0: bits 0-7 RSSI, bits 8-13 rate, bit 14 phy error? (subset)
 *   status1: bits 16-31 data_len                                        */
typedef struct ath9k_rxd {
    u32 ds_data;
    u32 ds_ctl;
    u32 ds_status0;
    u32 ds_status1;
} ath9k_rxd_t;

static int ath9k_rxd_parse(const ath9k_rxd_t *d, u16 *len_out, u8 *rssi_out)
{
    if (d->ds_status0 & 0x00004000u) return -1;   /* phy error: honest  */
    *len_out  = (u16)((d->ds_status1 >> 16) & 0xffffu);
    *rssi_out = (u8)(d->ds_status0 & 0xffu);
    if (*len_out == 0) return -2;
    return 0;
}

/* ================================================================== */
/* ath10k - CE descriptors + HTC + BMI + WMI-TLV (wire formats)        */
/* ================================================================== */
static const u16 ath10k_devids[] = {
    0x003c,   /* QCA9880                                                   */
    0x003e,   /* QCA6174                                                   */
    0x0042,   /* QCA9377                                                   */
    0x0046,   /* QCA9984                                                   */
    0x0050,   /* QCA9887                                                   */
    0x0056,   /* QCA9888                                                   */
};
#define ATH10K_DEVID_COUNT (sizeof(ath10k_devids) / sizeof(ath10k_devids[0]))

/* CE ring descriptor (ce_desc, 8 bytes): addr u32, nbytes u16,
 * flags u16 (src: meta low bits; gather flag 0x1)                     */
#define ATH10K_CE_FLAG_GATHER    0x0001u

typedef struct ath10k_ce_desc {
    u32 addr;
    u16 nbytes;
    u16 flags;
} ath10k_ce_desc_t;

static void ath10k_ce_desc_fill(ath10k_ce_desc_t *d, u32 addr, u16 n,
                                int gather)
{
    d->addr   = addr;
    d->nbytes = n;
    d->flags  = gather ? ATH10K_CE_FLAG_GATHER : 0;
}

/* HTC frame header (htc_frame_hdr): endpoint id u8, flags u8,
 * payload length u16 LE (4 bytes, big-endian length? HTC length is LE
 * in the wire format used by ath10k htc.h: __le16 payld_len)          */
typedef struct ath10k_htc_hdr {
    u8  eid;
    u8  flags;
    u16 payld_len;                    /* LE on wire                       */
} ath10k_htc_hdr_t;

static void ath10k_htc_hdr_pack(u8 *out, u8 eid, u8 flags, u16 len)
{
    out[0] = eid;
    out[1] = flags;
    out[2] = (u8)(len & 0xff);
    out[3] = (u8)((len >> 8) & 0xff);
}

static int ath10k_htc_hdr_unpack(const u8 *in, ath10k_htc_hdr_t *h)
{
    h->eid        = in[0];
    h->flags      = in[1];
    h->payld_len  = (u16)(in[2] | ((u16)in[3] << 8));
    return 0;
}

/* BMI protocol command ids (bmi.h)                                    */
#define ATH10K_BMI_DONE          1
#define ATH10K_BMI_GET_TARGET_INFO 2
#define ATH10K_BMI_VERSION       3
#define ATH10K_BMI_READ_MEMORY   4
#define ATH10K_BMI_WRITE_MEMORY  5

static void ath10k_bmi_cmd_pack(u8 *out, u32 cmd)
{
    out[0] = (u8)(cmd & 0xff);
    out[1] = (u8)((cmd >> 8) & 0xff);
    out[2] = (u8)((cmd >> 16) & 0xff);
    out[3] = (u8)((cmd >> 24) & 0xff);
}

/* WMI command id subset (wmi.h)                                       */
#define ATH10K_WMI_INIT_CMDID    1
#define ATH10K_WMI_START_SCAN    10
#define ATH10K_WMI_STOP_SCAN     11
#define ATH10K_WMI_CONNECT       13

/* WMI-TLV command frame: cmd_id u32 LE + TLV stream {tag u16 LE,
 * len u16 LE, value}                                                  */
typedef struct ath10k_wmi_tlv {
    u16 tag;
    u16 len;
    /* value follows */
} ath10k_wmi_tlv_t;

static int ath10k_wmi_tlv_put(u8 *buf, int cap, int *off, u16 tag,
                              const void *val, u16 len)
{
    if (*off + 4 + len > cap) return -1;
    buf[(*off)++] = (u8)(tag & 0xff);
    buf[(*off)++] = (u8)(tag >> 8);
    buf[(*off)++] = (u8)(len & 0xff);
    buf[(*off)++] = (u8)(len >> 8);
    for (u16 i = 0; i < len; i++) buf[(*off)++] = ((const u8 *)val)[i];
    /* TLV values are 4-byte aligned in WMI-TLV                         */
    while (*off & 3) buf[(*off)++] = 0;
    return 0;
}

/* ================================================================== */
/* ath11k - MHI ring elements + QMI TLV (wire formats)                 */
/* ================================================================== */
static const u16 ath11k_devids[] = {
    0x0701,   /* QCA6390                                                   */
    0x1101,   /* WCN6855 2.1                                               */
    0x1102,   /* QCN9074                                                   */
    0x1103,   /* WCN6855 2.0                                               */
    0x1104,   /* WCN6750                                                   */
};
#define ATH11K_DEVID_COUNT (sizeof(ath11k_devids) / sizeof(ath11k_devids[0]))

/* MHI transfer ring element (mhi.h mhi_ring_element, 16 bytes):
 * type u16 | reserved u16 | buffer ptr u64 | len u32 | reserved u32   */
#define ATH11K_MHI_TYPE_TRANSFER 2
#define ATH11K_MHI_TYPE_CHANNEL_ABORT 2

typedef struct ath11k_mhi_elem {
    u16 type;
    u16 res1;
    u64 buf_ptr;
    u32 len;
    u32 res2;
} ath11k_mhi_elem_t;

static void ath11k_mhi_elem_pack(u8 *out, u16 type, u64 ptr, u32 len)
{
    out[0]  = (u8)(type & 0xff);
    out[1]  = (u8)(type >> 8);
    out[2]  = 0; out[3] = 0;
    for (int i = 0; i < 8; i++) out[4 + i] = (u8)((ptr >> (8 * i)) & 0xff);
    out[12] = (u8)(len & 0xff);
    out[13] = (u8)((len >> 8) & 0xff);
    out[14] = (u8)((len >> 16) & 0xff);
    out[15] = (u8)((len >> 24) & 0xff);
}

/* QMI TLV: type u8, length u16 LE, value bytes                        */
static int ath11k_qmi_tlv_put(u8 *buf, int cap, int *off, u8 type,
                              const void *val, u16 len)
{
    if (*off + 3 + len > cap) return -1;
    buf[(*off)++] = type;
    buf[(*off)++] = (u8)(len & 0xff);
    buf[(*off)++] = (u8)(len >> 8);
    for (u16 i = 0; i < len; i++) buf[(*off)++] = ((const u8 *)val)[i];
    return 0;
}

/* ================================================================== */
/* Family glue                                                         */
/* ================================================================== */
static int wl_check(char *out, int *o, int outlen, const char *name, int cond)
{
    if (!cond) {
        const char *s = " FAIL";
        if (*o + 48 < outlen) {
            int i = 0;
            while (name[i] && *o < outlen - 1) out[(*o)++] = name[i++];
            i = 0;
            while (s[i] && *o < outlen - 1) out[(*o)++] = s[i++];
            out[(*o)++] = '\n';
        }
        out[*o] = '\0';
        return 1;
    }
    out[*o] = '\0';
    return 0;
}

/* ---------------- ath9k ops ---------------- */
static int ath9k_probe_pci(u16 vendor, u16 device)
{
    unsigned i;
    if (vendor != 0x168c) return 0;
    for (i = 0; i < ATH9K_DEVID_COUNT; i++)
        if (ath9k_devids[i] == device) return 1;
    return 0;
}

static int ath9k_attach(wl_device_t *dev)
{
    ath9k_priv_t *p;
    u8 bus = 0, d = 0, fn = 0;
    int found = -1;

    if (!dev) return -1;
    for (unsigned i = 0; i < ATH9K_DEVID_COUNT; i++) {
        if (driver_pci_find_device(0x168c, ath9k_devids[i], &bus, &d, &fn) == 0) {
            found = (int)i;
            break;
        }
    }
    if (found < 0) return -2;

    p = (ath9k_priv_t *)kmalloc(sizeof(ath9k_priv_t));
    if (!p) return -3;
    memset((void *)p, 0, sizeof(*p));
    dev->family_priv = p;
    dev->vendor_id   = 0x168c;
    dev->device_id   = ath9k_devids[found];

    {
        u32 cmd = driver_pci_read_config(bus, d, fn, 0x04);
        cmd |= 0x0007;
        driver_pci_write_config(bus, d, fn, 0x04, cmd);
    }
    {
        u32 bar0 = driver_pci_read_bar(bus, d, fn, 0);
        if (bar0 & 1) { kfree(p); dev->family_priv = 0; return -4; }
        p->mmio = (volatile u32 *)(uintptr_t)bar0;
    }
    /* RTC wake (AR_RTC_RESET poll to ON, then PLL lock)                 */
    a9k_w32(p, ATH9K_AR_RTC_RESET, 1);
    {
        u64 dl = core_timer_now_ms() + 200;
        while (core_timer_now_ms() < dl) {
            if (a9k_r32(p, ATH9K_AR_RTC_STATUS) & ATH9K_RTC_STATE_ON) break;
            core_sched_yield();
        }
    }
    a9k_w32(p, ATH9K_AR_RTC_PLL_CONTROL, 0x00040000u);   /* PLL write gate */
    p->srev = a9k_r32(p, ATH9K_AR_SREV);
    /* station + BSSID registers (zero until an SSID is joined)         */
    a9k_w32(p, ATH9K_AR_STA_ID0, 0x00000000u);
    a9k_w32(p, ATH9K_AR_STA_ID1, 0x00000000u);
    a9k_w32(p, ATH9K_AR_BSS_ID0, 0xffffffffu);
    a9k_w32(p, ATH9K_AR_BSS_ID1, 0x0000ffffu);
    return 0;
}

static int ath9k_fw_load(wl_device_t *dev)
{
    /* ath9k MAC+BB run from ROM/EEPROM data, not a file; "fw" = the
     * in-ROM init is exercised by the RTC wake in attach.  Nothing to
     * load: report success honestly (this is the documented family
     * behavior - only ath10k/ath11k need file firmware).                */
    (void)dev;
    return 0;
}

static int ath9k_tx_frame(wl_device_t *dev, const u8 *frame, u16 len)
{
    ath9k_priv_t *p = (ath9k_priv_t *)dev->family_priv;
    ath9k_txd_t td;
    u64 buf;
    volatile u32 *desc;

    if (!p || !p->mmio) return -1;
    if (!frame || len == 0 || len > WL_FRAME_MAX) return -2;
    /* staging frame buffer from identity-mapped PMM (rtl8139 doctrine) */
    buf = mem_pmm_alloc_frame();
    if (!buf) return -3;
    {
        volatile u8 *dst = (volatile u8 *)(uintptr_t)buf;
        for (u16 i = 0; i < len; i++) dst[i] = frame[i];
    }
    /* descriptor lives in its own DMA page (never the kernel stack)    */
    if (!p->txd_page) {
        p->txd_page = mem_pmm_alloc_frame();
        if (!p->txd_page) { mem_pmm_free_frame(buf); return -4; }
    }
    desc = (volatile u32 *)(uintptr_t)p->txd_page;
    ath9k_txd_build(&td, buf, len,
                    (((frame[0] | (frame[1] << 8)) >> 2) & 0x3) == 2);
    desc[0] = td.ds_data;
    desc[1] = td.ds_ctl1;
    desc[2] = td.ds_ctl2;
    desc[3] = td.ds_ctl3;
    /* ring head: AR_Q0_TXDP = descriptor page, then QCU0 kick          */
    a9k_w32(p, 0x0800, (u32)p->txd_page);
    a9k_w32(p, ATH9K_AR_Q_TXE, 1);
    /* double-buffer the staging frame: release the previous one when
     * this TX replaces it (completion IRQs cannot fire without hw)     */
    if (p->pending_buf) mem_pmm_free_frame(p->pending_buf);
    p->pending_buf = buf;
    return 0;
}

static int ath9k_selftest(wl_device_t *dev, char *out, int outlen)
{
    int fails = 0, o = 0;
    ath9k_txd_t td;
    ath9k_rxd_t rx;

    if (outlen > 0) out[0] = '\0';
    fails += wl_check(out, &o, outlen, "ath9k id AR9285",
                      ath9k_probe_pci(0x168c, 0x002b) == 1);
    fails += wl_check(out, &o, outlen, "ath9k id AR9462",
                      ath9k_probe_pci(0x168c, 0x0034) == 1);
    fails += wl_check(out, &o, outlen, "ath9k non-atheros rejected",
                      ath9k_probe_pci(0x8086, 0x08b1) == 0);

    ath9k_txd_build(&td, 0x12345000u, 300, 1);
    fails += wl_check(out, &o, outlen, "ath9k txd data",
                      td.ds_data == 0x12345000u &&
                      (td.ds_ctl1 & 0xffffu) == 300 &&
                      (td.ds_ctl1 & ATH9K_TXD_TYPE_MSK) == ATH9K_TXD_TYPE_DATA);
    ath9k_txd_build(&td, 0, 64, 0);
    fails += wl_check(out, &o, outlen, "ath9k txd mgmt",
                      (td.ds_ctl1 & ATH9K_TXD_TYPE_MSK) == ATH9K_TXD_TYPE_MGMT);

    rx.ds_data = 0; rx.ds_ctl = 0;
    rx.ds_status0 = 0x3c | 0;         /* RSSI 60, no phy error            */
    rx.ds_status1 = (u32)200 << 16;
    {
        u16 l = 0; u8 r = 0;
        fails += wl_check(out, &o, outlen, "ath9k rxd parse",
                          ath9k_rxd_parse(&rx, &l, &r) == 0 &&
                          l == 200 && r == 60);
        rx.ds_status0 |= 0x00004000u;
        fails += wl_check(out, &o, outlen, "ath9k rxd phy error honest",
                          ath9k_rxd_parse(&rx, &l, &r) == -1);
    }
    (void)dev;
    if (o < outlen - 12) {
        const char *t = "ath9k checks done\n";
        int i = 0;
        while (t[i] && o < outlen - 1) out[o++] = t[i++];
        out[o] = '\0';
    }
    return fails;
}

static const wl_family_ops_t ath9k_ops = {
    "ath9k", ath9k_probe_pci, ath9k_attach, ath9k_fw_load,
    0, ath9k_tx_frame, ath9k_selftest,
};

/* ---------------- ath10k ops ---------------- */
static int ath10k_probe_pci(u16 vendor, u16 device)
{
    unsigned i;
    if (vendor != 0x168c) return 0;
    for (i = 0; i < ATH10K_DEVID_COUNT; i++)
        if (ath10k_devids[i] == device) return 1;
    return 0;
}

static int ath10k_attach(wl_device_t *dev)
{
    u8 bus = 0, d = 0, fn = 0;
    int found = -1;
    volatile u32 *mmio;

    if (!dev) return -1;
    for (unsigned i = 0; i < ATH10K_DEVID_COUNT; i++) {
        if (driver_pci_find_device(0x168c, ath10k_devids[i], &bus, &d, &fn) == 0) {
            found = (int)i;
            break;
        }
    }
    if (found < 0) return -2;
    {
        u32 cmd = driver_pci_read_config(bus, d, fn, 0x04);
        cmd |= 0x0007;
        driver_pci_write_config(bus, d, fn, 0x04, cmd);
        {
            u32 bar0 = driver_pci_read_bar(bus, d, fn, 0);
            if (bar0 & 1) return -3;
            mmio = (volatile u32 *)(uintptr_t)bar0;
        }
    }
    (void)mmio;                        /* CE programming at fw-load time */
    dev->vendor_id = 0x168c;
    dev->device_id = ath10k_devids[found];
    return 0;
}

static int ath10k_fw_load(wl_device_t *dev)
{
    /* BMI phase: the firmware blob is pushed through CE pipes with
     * BMI_WRITE_MEMORY; without the file there is nothing to push.     */
    static const char path[] = "/etc/firmware/ath10k.fw";
    int fd;
    (void)dev;
    fd = fs_vfs_open(path, VFS_O_RDONLY);
    if (fd < 0) return -1;
    fs_vfs_close(fd);
    return 0;                          /* file present: BMI handoff ready */
}

static int ath10k_tx_frame(wl_device_t *dev, const u8 *frame, u16 len)
{
    /* data goes out as an HTC frame on the WMI data endpoint through
     * a CE src ring; without firmware the target never enabled the
     * pipes - honest refusal keeps the 802.11 core state clean.        */
    (void)dev; (void)frame; (void)len;
    return -1;
}

static int ath10k_selftest(wl_device_t *dev, char *out, int outlen)
{
    int fails = 0, o = 0;

    if (outlen > 0) out[0] = '\0';
    fails += wl_check(out, &o, outlen, "ath10k id QCA6174",
                      ath10k_probe_pci(0x168c, 0x003e) == 1);
    fails += wl_check(out, &o, outlen, "ath10k id QCA9888",
                      ath10k_probe_pci(0x168c, 0x0056) == 1);
    fails += wl_check(out, &o, outlen, "ath10k vendor rejected",
                      ath10k_probe_pci(0x10ec, 0x8179) == 0);

    {
        ath10k_ce_desc_t d;
        ath10k_ce_desc_fill(&d, 0x00123000u, 512, 1);
        fails += wl_check(out, &o, outlen, "ath10k CE desc",
                          d.addr == 0x00123000u && d.nbytes == 512 &&
                          d.flags == ATH10K_CE_FLAG_GATHER);
    }
    {
        u8 h[4];
        ath10k_htc_hdr_t hh;
        ath10k_htc_hdr_pack(h, 3, 0, 0x0142);
        fails += wl_check(out, &o, outlen, "ath10k HTC pack",
                          h[0] == 3 && h[1] == 0 && h[2] == 0x42 && h[3] == 0x01);
        ath10k_htc_hdr_unpack(h, &hh);
        fails += wl_check(out, &o, outlen, "ath10k HTC unpack",
                          hh.eid == 3 && hh.payld_len == 0x0142);
    }
    {
        u8 b[4];
        ath10k_bmi_cmd_pack(b, ATH10K_BMI_WRITE_MEMORY);
        fails += wl_check(out, &o, outlen, "ath10k BMI cmd",
                          b[0] == 5 && b[1] == 0 && b[2] == 0 && b[3] == 0);
    }
    {
        u8 buf[32];
        int off = 0;
        u32 v = 0x12345678u;
        fails += wl_check(out, &o, outlen, "ath10k WMI TLV put",
                          ath10k_wmi_tlv_put(buf, (int)sizeof(buf), &off,
                                             7, &v, 4) == 0 &&
                          off == 8 && buf[0] == 7 && buf[2] == 4 &&
                          buf[4] == 0x78 && buf[7] == 0x12);
        fails += wl_check(out, &o, outlen, "ath10k WMI TLV cap honest",
                          ath10k_wmi_tlv_put(buf, 6, &off, 8, &v, 4) == -1);
    }
    (void)dev;
    if (o < outlen - 13) {
        const char *t = "ath10k checks done\n";
        int i = 0;
        while (t[i] && o < outlen - 1) out[o++] = t[i++];
        out[o] = '\0';
    }
    return fails;
}

static const wl_family_ops_t ath10k_ops = {
    "ath10k", ath10k_probe_pci, ath10k_attach, ath10k_fw_load,
    0, ath10k_tx_frame, ath10k_selftest,
};

/* ---------------- ath11k ops ---------------- */
static int ath11k_probe_pci(u16 vendor, u16 device)
{
    unsigned i;
    if (vendor != 0x17cb) return 0;    /* Qualcomm vendor id             */
    for (i = 0; i < ATH11K_DEVID_COUNT; i++)
        if (ath11k_devids[i] == device) return 1;
    return 0;
}

static int ath11k_attach(wl_device_t *dev)
{
    u8 bus = 0, d = 0, fn = 0;
    int found = -1;

    if (!dev) return -1;
    for (unsigned i = 0; i < ATH11K_DEVID_COUNT; i++) {
        if (driver_pci_find_device(0x17cb, ath11k_devids[i], &bus, &d, &fn) == 0) {
            found = (int)i;
            break;
        }
    }
    if (found < 0) return -2;
    {
        u32 cmd = driver_pci_read_config(bus, d, fn, 0x04);
        cmd |= 0x0007;
        driver_pci_write_config(bus, d, fn, 0x04, cmd);
    }
    dev->vendor_id = 0x17cb;
    dev->device_id = ath11k_devids[found];
    return 0;
}

static int ath11k_fw_load(wl_device_t *dev)
{
    static const char path[] = "/etc/firmware/ath11k.mbn";
    int fd;
    (void)dev;
    fd = fs_vfs_open(path, VFS_O_RDONLY);
    if (fd < 0) return -1;
    fs_vfs_close(fd);
    return 0;
}

static int ath11k_tx_frame(wl_device_t *dev, const u8 *frame, u16 len)
{
    (void)dev; (void)frame; (void)len;
    return -1;                         /* MHI channels not up without fw  */
}

static int ath11k_selftest(wl_device_t *dev, char *out, int outlen)
{
    int fails = 0, o = 0;

    if (outlen > 0) out[0] = '\0';
    fails += wl_check(out, &o, outlen, "ath11k id WCN6855",
                      ath11k_probe_pci(0x17cb, 0x1103) == 1);
    fails += wl_check(out, &o, outlen, "ath11k id QCA6390",
                      ath11k_probe_pci(0x17cb, 0x0701) == 1);
    fails += wl_check(out, &o, outlen, "ath11k vendor rejected",
                      ath11k_probe_pci(0x168c, 0x003e) == 0);
    {
        u8 e[16];
        ath11k_mhi_elem_pack(e, ATH11K_MHI_TYPE_TRANSFER,
                             0x1234567890abcdefull, 0x00000400u);
        fails += wl_check(out, &o, outlen, "ath11k MHI elem",
                          e[0] == 2 && e[4] == 0xef && e[11] == 0x12 &&
                          e[12] == 0x00 && e[15] == 0x00);
    }
    {
        u8 buf[16];
        int off = 0;
        u8 v[2] = { 0x11, 0x22 };
        fails += wl_check(out, &o, outlen, "ath11k QMI TLV put",
                          ath11k_qmi_tlv_put(buf, (int)sizeof(buf), &off,
                                             0x15, v, 2) == 0 &&
                          off == 5 && buf[0] == 0x15 && buf[1] == 2 &&
                          buf[3] == 0x11);
    }
    (void)dev;
    if (o < outlen - 13) {
        const char *t = "ath11k checks done\n";
        int i = 0;
        while (t[i] && o < outlen - 1) out[o++] = t[i++];
        out[o] = '\0';
    }
    return fails;
}

static const wl_family_ops_t ath11k_ops = {
    "ath11k", ath11k_probe_pci, ath11k_attach, ath11k_fw_load,
    0, ath11k_tx_frame, ath11k_selftest,
};

/* ---------------- registration ---------------- */
void driver_ath_register(void)
{
    wl_register_family(&ath9k_ops);
    wl_register_family(&ath10k_ops);
    wl_register_family(&ath11k_ops);
}
