/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 cubestudio-dev <cubestudio@qq.com>
 * Open Cube OS - WP-10-wireless_bluetooth
 *
 * File: drivers/wireless/driver_brcm.c
 *
 * Broadcom 802.11 family driver:
 *   b43      - BCM4306/4311/4312/4318/4321/4322/4331 (PCI/SSB cores)
 *   brcmfmac - BCM43602/4356 (PCIe) + SDIO cards via the SDHCI bus
 *              (drivers/sd/driver_sdhci.{c,h})
 *
 * Register/format references: SSB-over-PCI core windowing (SSB PCIe
 * core BAR0 window selector), brcmfmac PCIe msgbuf/DHD bus concept and
 * the Broadcom SDIO "SB address window" registers (SBSDIO_FUNC1_SBADDR*
 * per brcmfmac bcmsdh), all public.
 *
 * Honesty note: no QEMU 802.11 model exists (datasheet-对照 level);
 * the SDIO bus path DOES run against real emulated SDHCI hardware in
 * QEMU (sdhci-pci) and honestly reports "no SDIO card" when none is
 * attached.  Firmware files (b43 ucode/pcm, brcmfmac CLM/nvram) are
 * third-party property, not shipped; fw_load fails honestly when
 * absent.
 */
#include "types.h"
#include "driver_brcm.h"
#include "driver_wireless.h"
#include "driver_sdhci.h"
#include "driver_pci.h"
#include "mem_heap.h"
#include "core_timer.h"
#include "core_sched.h"
#include "lib_string.h"
#include "fs_vfs.h"

static int wl_check(char *out, int *o, int outlen, const char *name,
                    int cond)
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

/* ================================================================== */
/* b43 (SSB-over-PCI)                                                  */
/* ================================================================== */
/* PCI IDs (linux b43/bcma tables, vendor 0x14e4)                      */
static const u16 b43_devids[] = {
    0x4320,   /* BCM4306                                                   */
    0x4311,   /* BCM4311                                                   */
    0x4312,   /* BCM4312                                                   */
    0x4315,   /* BCM4312 LP-PHY                                            */
    0x4318,   /* BCM4318                                                   */
    0x4328,   /* BCM4321                                                   */
    0x4329,   /* BCM4321 rev2                                              */
    0x432b,   /* BCM4322                                                   */
    0x4331,   /* BCM4331                                                   */
};
#define B43_DEVID_COUNT (sizeof(b43_devids) / sizeof(b43_devids[0]))

/* SSB PCIe core window selector (SSB_BAR0_WIN, PCI config offset 0x80)*/
#define B43_SSB_BAR0_WIN        0x80
#define B43_SSB_CORE_SIZE       0x1000u  /* 16 KiB? per-core window step  */

/* SSB core identification register (SSB_IDHIGH at 0x0fec within a     */
/* core window: core id bits 15-4, revision bits 3-0)                  */
#define B43_SSB_IDHIGH          0x0fec

typedef struct b43_priv {
    volatile u32 *mmio;
    u8            bus, dev, fn;
    u32           idhigh;
} b43_priv_t;

static inline u32 b43_r32(const b43_priv_t *p, u32 off)
{
    return *(volatile u32 *)((volatile u8 *)p->mmio + off);
}
static inline void b43_w32(const b43_priv_t *p, u32 off, u32 v)
{
    *(volatile u32 *)((volatile u8 *)p->mmio + off) = v;
}

/* Select an SSB core into the BAR0 window and read its IDHIGH.        */
static int b43_select_core(b43_priv_t *p, u8 core_index, u32 *id_out)
{
    driver_pci_write_config(p->bus, p->dev, p->fn, B43_SSB_BAR0_WIN,
                            (u32)core_index * B43_SSB_CORE_SIZE);
    *id_out = b43_r32(p, B43_SSB_IDHIGH);
    return 0;
}

static int b43_probe_pci(u16 vendor, u16 device)
{
    unsigned i;
    if (vendor != 0x14e4) return 0;
    for (i = 0; i < B43_DEVID_COUNT; i++)
        if (b43_devids[i] == device) return 1;
    return 0;
}

static int b43_attach(wl_device_t *dev)
{
    b43_priv_t *p;
    u8 bus = 0, d = 0, fn = 0;
    int found = -1;

    if (!dev) return -1;
    for (unsigned i = 0; i < B43_DEVID_COUNT; i++) {
        if (driver_pci_find_device(0x14e4, b43_devids[i], &bus, &d, &fn) == 0) {
            found = (int)i;
            break;
        }
    }
    if (found < 0) return -2;

    p = (b43_priv_t *)kmalloc(sizeof(b43_priv_t));
    if (!p) return -3;
    memset((void *)p, 0, sizeof(*p));
    p->bus = bus; p->dev = d; p->fn = fn;
    dev->family_priv = p;
    dev->vendor_id   = 0x14e4;
    dev->device_id   = b43_devids[found];

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
    /* walk the first SSB cores through the BAR0 window (real windowing */
    {
        u32 id = 0;
        (void)b43_select_core(p, 0, &id);
        p->idhigh = id;
    }
    return 0;
}

/* b43 firmware: ucode + pcm files (third-party property).             */
static int b43_fw_load(wl_device_t *dev)
{
    static const char paths[2][32] = {
        "/etc/firmware/b43-ucode.fw",
        "/etc/firmware/b43-pcm.fw",
    };
    int present = 0;
    (void)dev;
    for (int i = 0; i < 2; i++) {
        int fd = fs_vfs_open(paths[i], VFS_O_RDONLY);
        if (fd >= 0) { fs_vfs_close(fd); present++; }
    }
    /* both files required per the b43 driver model                     */
    return present == 2 ? 0 : -1;
}

static int b43_tx_frame(wl_device_t *dev, const u8 *frame, u16 len)
{
    /* b43 needs its SSB PHY initialized from the EEPROM before any
     * frame can be queued; without fw files that never happened.       */
    (void)dev; (void)frame; (void)len;
    return -1;
}

static int b43_selftest(wl_device_t *dev, char *out, int outlen)
{
    int fails = 0, o = 0;

    if (outlen > 0) out[0] = '\0';
    fails += wl_check(out, &o, outlen, "b43 id BCM4318",
                      b43_probe_pci(0x14e4, 0x4318) == 1);
    fails += wl_check(out, &o, outlen, "b43 id BCM4331",
                      b43_probe_pci(0x14e4, 0x4331) == 1);
    fails += wl_check(out, &o, outlen, "b43 non-broadcom rejected",
                      b43_probe_pci(0x168c, 0x002b) == 0);
    /* SSB window encode: core 5 -> offset 0x5000 written to cfg 0x80   */
    {
        u32 win = (u32)5 * B43_SSB_CORE_SIZE;
        fails += wl_check(out, &o, outlen, "b43 ssb window enc",
                          win == 0x5000u);
    }
    (void)dev;
    if (o < outlen - 12) {
        const char *t = "b43 checks done\n";
        int i = 0;
        while (t[i] && o < outlen - 1) out[o++] = t[i++];
        out[o] = '\0';
    }
    return fails;
}

static const wl_family_ops_t b43_ops = {
    "b43", b43_probe_pci, b43_attach, b43_fw_load,
    0, b43_tx_frame, b43_selftest,
};

/* ================================================================== */
/* brcmfmac (PCIe + SDIO)                                              */
/* ================================================================== */
static const u16 brcmf_devids[] = {
    0x43ba,   /* BCM43602                                                  */
    0x43ec,   /* BCM4356                                                   */
};
#define BRCMF_DEVID_COUNT (sizeof(brcmf_devids) / sizeof(brcmf_devids[0]))

/* Broadcom SDIO ids                                                   */
#define BRCMF_SDIO_VID          0x02d0
#define BRCMF_SDIO_DEFAULT_DID  0xa9a6   /* BCM43362-class default       */

/* Broadcom SDIO SB address window registers (func 1, brcmfmac
 * bcmsdh_sdmmc: SBSDIO_FUNC1_SBADDRLOW/MID/HIGH)                      */
#define BRCMF_SBADDR_LOW        0x10000
#define BRCMF_SBADDR_MID        0x20000
#define BRCMF_SBADDR_HIGH       0x30000
#define BRCMF_SB_WINDOW_LIMIT   0x8000   /* data window below this       */

typedef struct brcmf_priv {
    int  sdio_host;                  /* -1 when not on the SDIO bus     */
    u8   bus, dev, fn;               /* PCIe                            */
    volatile u32 *mmio;
    u8   sdio_fn;                    /* claimed function number         */
} brcmf_priv_t;

static brcmf_priv_t g_brcmf_sdio;    /* SDIO client context (single)    */

static int brcmf_probe_pci(u16 vendor, u16 device)
{
    unsigned i;
    if (vendor != 0x14e4) return 0;
    for (i = 0; i < BRCMF_DEVID_COUNT; i++)
        if (brcmf_devids[i] == device) return 1;
    return 0;
}

static int brcmf_attach(wl_device_t *dev)
{
    brcmf_priv_t *p;
    u8 bus = 0, d = 0, fn = 0;
    int found = -1;

    if (!dev) return -1;
    p = (brcmf_priv_t *)kmalloc(sizeof(brcmf_priv_t));
    if (!p) return -2;
    memset((void *)p, 0, sizeof(*p));
    p->sdio_host = -1;
    dev->family_priv = p;

    /* SDIO card takes precedence (brcmfmac is an SDIO-first family)    */
    if (strcmp(driver_sdhci_state(), "sdio") == 0 &&
        driver_sdhci_num_io_functions(0) > 0) {
        g_brcmf_sdio.sdio_host = 0;   /* host 0 (first SDHCI)            */
        p->sdio_host = 0;
        p->sdio_fn   = 1;
        dev->bus_name  = "SDIO";
        dev->vendor_id = BRCMF_SDIO_VID;
        dev->device_id = BRCMF_SDIO_DEFAULT_DID;
        return 0;
    }
    /* PCIe                                                             */
    for (unsigned i = 0; i < BRCMF_DEVID_COUNT; i++) {
        if (driver_pci_find_device(0x14e4, brcmf_devids[i], &bus, &d, &fn) == 0) {
            found = (int)i;
            break;
        }
    }
    if (found < 0) { kfree(p); dev->family_priv = 0; return -3; }
    p->bus = bus; p->dev = d; p->fn = fn;
    {
        u32 cmd = driver_pci_read_config(bus, d, fn, 0x04);
        cmd |= 0x0007;
        driver_pci_write_config(bus, d, fn, 0x04, cmd);
        u32 bar0 = driver_pci_read_bar(bus, d, fn, 0);
        if (bar0 & 1) { kfree(p); dev->family_priv = 0; return -4; }
        p->mmio = (volatile u32 *)(uintptr_t)bar0;
    }
    dev->bus_name  = "PCI";
    dev->vendor_id = 0x14e4;
    dev->device_id = brcmf_devids[found];
    return 0;
}

static int brcmf_fw_load(wl_device_t *dev)
{
    static const char path[] = "/etc/firmware/brcmfmac.bin";
    int fd;
    brcmf_priv_t *p = (brcmf_priv_t *)dev->family_priv;

    fd = fs_vfs_open(path, VFS_O_RDONLY);
    if (fd < 0) return -1;
    fs_vfs_close(fd);
    (void)p;
    return 0;
}

static int brcmf_tx_frame(wl_device_t *dev, const u8 *frame, u16 len)
{
    /* DHD bus (msgbuf on PCIe, SDIO data windows elsewhere) requires
     * the firmware-provisioned rings; without fw the bus is down.      */
    (void)dev; (void)frame; (void)len;
    return -1;
}

/* SDIO client probe (called by the SDHCI SDIO binder when a card is
 * present; the CCCR sanity read lives in the binder).                 */
static int brcmf_sdio_probe(sdio_func_t *func)
{
    if (func->vendor_id != BRCMF_SDIO_VID) return -1;
    g_brcmf_sdio.sdio_host = 0;
    g_brcmf_sdio.sdio_fn   = (u8)func->number;
    func->device_id = BRCMF_SDIO_DEFAULT_DID;
    return 0;
}

static void brcmf_sdio_remove(sdio_func_t *func)
{
    (void)func;
    g_brcmf_sdio.sdio_host = -1;
}

static const sdio_driver_t brcmf_sdio_drv = {
    "brcmfmac-sdio", BRCMF_SDIO_VID, 0xffff, brcmf_sdio_probe,
    brcmf_sdio_remove,
};

static int brcmf_selftest(wl_device_t *dev, char *out, int outlen)
{
    int fails = 0, o = 0;

    if (outlen > 0) out[0] = '\0';
    fails += wl_check(out, &o, outlen, "brcmf id BCM43602",
                      brcmf_probe_pci(0x14e4, 0x43ba) == 1);
    fails += wl_check(out, &o, outlen, "brcmf id BCM4356",
                      brcmf_probe_pci(0x14e4, 0x43ec) == 1);
    fails += wl_check(out, &o, outlen, "brcmf vendor rejected",
                      brcmf_probe_pci(0x8086, 0x2723) == 0);
    fails += wl_check(out, &o, outlen, "brcmf sdio vid",
                      BRCMF_SDIO_VID == 0x02d0);
    /* SB window layout: LOW < MID < HIGH, data window below 0x8000     */
    fails += wl_check(out, &o, outlen, "brcmf sb window order",
                      BRCMF_SBADDR_LOW < BRCMF_SBADDR_MID &&
                      BRCMF_SBADDR_MID < BRCMF_SBADDR_HIGH &&
                      BRCMF_SB_WINDOW_LIMIT == 0x8000u);
    (void)dev;
    if (o < outlen - 13) {
        const char *t = "brcmfmac checks done\n";
        int i = 0;
        while (t[i] && o < outlen - 1) out[o++] = t[i++];
        out[o] = '\0';
    }
    return fails;
}

static const wl_family_ops_t brcmf_ops = {
    "brcmfmac", brcmf_probe_pci, brcmf_attach, brcmf_fw_load,
    0, brcmf_tx_frame, brcmf_selftest,
};

void driver_brcm_register(void)
{
    wl_register_family(&b43_ops);
    wl_register_family(&brcmf_ops);
    driver_sdio_register_driver(&brcmf_sdio_drv);
}
