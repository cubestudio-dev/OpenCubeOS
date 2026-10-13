/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-input_chipset
 * File: drivers/pci/driver_pcie.c
 * Purpose: PCI/PCIe bus layer implementation - see driver_pcie.h.
 *
 * Notes:
 *  - The kernel identity-maps the first 4 GiB, so an ECAM base below
 *    4 GiB (QEMU q35: 0xB0000000) is dereferenced directly.
 *  - BAR sizing follows the standard save-write-1s-restore dance; it
 *    is only performed during scan for devices whose BARs the firmware
 *    left unassigned, so no live device register is disturbed.
 *  - MSI address/data follow the Intel xAPIC message format:
 *    address = 0xFEE00000 | (dest_apic_id << 12) | (1 << 3)  (rh=0),
 *    data    = vector (edge, fixed delivery, deassert handled by HW).
 */
#include "driver_pcie.h"
#include "driver_pci.h"
#include "driver_acpi.h"
#include "arch_lapic.h"
#include "screen_console.h"
#include "lib_string.h"

#include <stdint.h>

static driver_pcie_dev_t g_devs[PCIE_MAX_DEVICES];
static int g_dev_count;

static inline void pcie_outl(u16 port, u32 v) { __asm__ volatile("outl %0, %1" :: "a"(v), "Nd"(port)); }
static inline u32  pcie_inl(u16 port)         { u32 v; __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(port)); return v; }

/* ------------------------------------------------------------------ */
/* config access (MMCONFIG when available)                             */
/* ------------------------------------------------------------------ */

int driver_pcie_mmconfig_active(void) {
    return driver_acpi_mcfg_present() &&
           driver_acpi_mmconfig_base(0) != 0 &&
           driver_acpi_mmconfig_base(0) < 0x100000000ull;
}

u64 driver_pcie_mmconfig_base(void) {
    return driver_pcie_mmconfig_active() ? driver_acpi_mmconfig_base(0) : 0;
}

static int mmconfig_covers(u8 bus) {
    u8 start, end;
    if (driver_acpi_mmconfig_bus_range(0, &start, &end) != 0) return 0;
    return bus >= start && bus <= end;
}

u32 driver_pcie_read_config(u8 bus, u8 dev, u8 func, u16 offset) {
    if (driver_pcie_mmconfig_active() && mmconfig_covers(bus) && offset < 4096) {
        volatile const u8 *p = (volatile const u8*)(uintptr_t)
            (driver_acpi_mmconfig_base(0)
             + ((u64)bus << 20) + ((u64)dev << 15) + ((u64)func << 12) + (offset & ~3u));
        return *(volatile const u32*)(uintptr_t)p;
    }
    if (offset > 0xFF) return 0xFFFFFFFFu;
    return driver_pci_read_config(bus, dev, func, (u8)offset);
}

void driver_pcie_write_config(u8 bus, u8 dev, u8 func, u16 offset, u32 value) {
    if (driver_pcie_mmconfig_active() && mmconfig_covers(bus) && offset < 4096) {
        volatile u8 *p = (volatile u8*)(uintptr_t)
            (driver_acpi_mmconfig_base(0)
             + ((u64)bus << 20) + ((u64)dev << 15) + ((u64)func << 12) + (offset & ~3u));
        *(volatile u32*)(uintptr_t)p = value;
        return;
    }
    if (offset > 0xFF) return;
    driver_pci_write_config(bus, dev, func, (u8)offset, value);
}

/* ------------------------------------------------------------------ */
/* BAR decode helpers                                                  */
/* ------------------------------------------------------------------ */

static void pcie_size_bar(u8 bus, u8 dev, u8 func, int idx,
                          driver_pcie_dev_t *d) {
    u16 off = 0x10 + (u16)(idx * 4);
    u32 raw = driver_pcie_read_config(bus, dev, func, off);
    d->bar_raw[idx] = raw;
    d->bar_size[idx] = 0;
    d->bar_flags[idx] = 0;
    if (raw == 0 || raw == 0xFFFFFFFFu) return;

    u32 saved = raw;
    int is_io = raw & 1;
    int is64  = (!is_io) && ((raw >> 1) & 3) == 2;

    /* write all 1s, read back the size mask */
    u32 mask;
    driver_pcie_write_config(bus, dev, func, off, 0xFFFFFFFFu);
    mask = driver_pcie_read_config(bus, dev, func, off);
    driver_pcie_write_config(bus, dev, func, off, saved);

    if (is_io) {
        d->bar_flags[idx] |= 1;
        u32 sz = mask & 0xFFFFFFFCu;
        if (sz) d->bar_size[idx] = (~sz + 1);
    } else {
        if ((raw >> 1) & 3) d->bar_flags[idx] |= 2;   /* 32/64 flag */
        if ((raw >> 3) & 1) d->bar_flags[idx] |= 4;   /* prefetch */
        u32 sz = mask & 0xFFFFFFF0u;
        if (is64) {
            /* high dword is not sized here; size from the low dword */
            u32 hi = driver_pcie_read_config(bus, dev, func, off + 4);
            d->bar_raw[idx + 1] = hi;   /* remember for completeness */
        }
        if (sz) d->bar_size[idx] = (~sz + 1);
    }
}

/* ------------------------------------------------------------------ */
/* capability walk                                                     */
/* ------------------------------------------------------------------ */

static void pcie_walk_caps(u8 bus, u8 dev, u8 func, driver_pcie_dev_t *d) {
    u32 status = driver_pcie_read_config(bus, dev, func, 0x06);
    if (!(status & 0x10)) return;   /* CapList not supported */
    u8 ptr = (u8)(driver_pcie_read_config(bus, dev, func, 0x34) & 0xFF);
    int guard = 0;
    while (ptr && ptr >= 0x40 && guard++ < 24) {
        u32 hdr = driver_pcie_read_config(bus, dev, func, ptr & 0xFC);
        u8 cap_id = (u8)(hdr & 0xFF);
        u8 next   = (u8)((hdr >> 8) & 0xFF);
        switch (cap_id) {
        case 0x05: d->cap_msi = ptr; break;
        case 0x11: {
            d->cap_msix = ptr;
            u32 tbl = driver_pcie_read_config(bus, dev, func, (u16)(ptr + 4));
            d->msix_bar    = (u8)(tbl & 0x7);
            d->msix_offset = tbl & ~7u;
            u32 pba = driver_pcie_read_config(bus, dev, func, (u16)(ptr + 8));
            (void)pba;
            u32 ctrl = (hdr >> 16) & 0xFFFF;
            d->msix_count = (u32)(ctrl & 0x7FF) + 1;
            break;
        }
        case 0x10: {
            d->cap_pcie = ptr;
            u32 pc = driver_pcie_read_config(bus, dev, func, (u16)(ptr + 2));
            d->pcie_type = (pc >> 4) & 0xF;
            break;
        }
        default:
            break;
        }
        ptr = next;
    }
}

/* PCIe extended capability walk (MMCONFIG only) - printed by commands. */
void driver_pcie_print_caps(const driver_pcie_dev_t *d) {
    char buf[96]; char n[16];
    strcpy(buf, "  ");
    u64_to_hex(d->bus, n, 2); strcat(buf, n);
    strcat(buf, ":");
    u64_to_hex(d->dev, n, 2); strcat(buf, n);
    strcat(buf, ".");
    u64_to_hex(d->fn, n, 1); strcat(buf, n);
    strcat(buf, " caps:");
    if (d->cap_msi)  strcat(buf, " MSI");
    if (d->cap_msix) { strcat(buf, " MSI-X(x"); u64_to_str(d->msix_count, n); strcat(buf, n); strcat(buf, ")"); }
    if (d->cap_pcie) { strcat(buf, " PCIe(type"); u64_to_str(d->pcie_type, n); strcat(buf, n); strcat(buf, ")"); }
    if (!d->cap_msi && !d->cap_msix && !d->cap_pcie) strcat(buf, " (legacy)");
    strcat(buf, "\n");
    screen_console_puts(buf);

    /* extended caps via MMCONFIG */
    if (driver_pcie_mmconfig_active() && mmconfig_covers(d->bus) && d->cap_pcie) {
        u32 off = 0x100;
        int guard = 0;
        strcpy(buf, "    ext:");
        int any = 0;
        while (off && off < 0x1000 && guard++ < 16) {
            u32 hdr = driver_pcie_read_config(d->bus, d->dev, d->fn, off);
            if (hdr == 0xFFFFFFFFu || hdr == 0) break;
            u16 id = hdr & 0xFFFF;
            u16 next = (hdr >> 20) & 0xFFF;
            strcat(buf, " 0x");
            u64_to_hex(id, n, 4); strcat(buf, n);
            any = 1;
            off = next;
        }
        strcat(buf, any ? "\n" : " none\n");
        screen_console_puts(buf);
    }
}

/* ------------------------------------------------------------------ */
/* enumeration                                                         */
/* ------------------------------------------------------------------ */

static void pcie_record_dev(u8 bus, u8 dev, u8 func) {
    if (g_dev_count >= PCIE_MAX_DEVICES) return;
    driver_pcie_dev_t *d = &g_devs[g_dev_count];
    memset(d, 0, sizeof(*d));
    u32 id = driver_pcie_read_config(bus, dev, func, 0x00);
    u32 cls = driver_pcie_read_config(bus, dev, func, 0x08);
    d->bus = bus; d->dev = dev; d->fn = func;
    d->valid = 1;
    d->vendor = (u16)(id & 0xFFFF);
    d->device = (u16)((id >> 16) & 0xFFFF);
    d->revision  = (u8)(cls & 0xFF);
    d->prog_if   = (u8)((cls >> 8) & 0xFF);
    d->sub_class = (u8)((cls >> 16) & 0xFF);
    d->base_class= (u8)((cls >> 24) & 0xFF);
    u32 ht = driver_pcie_read_config(bus, dev, func, 0x0C);
    d->header_type = (u8)((ht >> 16) & 0x7F);
    u32 irq = driver_pcie_read_config(bus, dev, func, 0x3C);
    d->irq_pin  = irq & 0xFF;
    d->irq_line = (irq >> 8) & 0xFF;

    /* BAR sizing only for devices the firmware did not program (a BAR
     * at address 0 with a size would never have been usable) */
    if (d->header_type == 0x00) {
        for (int i = 0; i < 6; i++) {
            u16 off = 0x10 + (u16)(i * 4);
            u32 raw = driver_pcie_read_config(bus, dev, func, off);
            if (raw == 0xFFFFFFFFu) break;
            pcie_size_bar(bus, dev, func, i, d);
            if ((d->bar_flags[i] & 2) && d->bar_size[i]) i++;   /* skip 64-bit high dword */
        }
    }
    pcie_walk_caps(bus, dev, func, d);
    g_dev_count++;
}

static int pcie_scan_bus(u8 bus) {
    int found = 0;
    for (u8 dev = 0; dev < 32; dev++) {
        u32 id = driver_pcie_read_config(bus, dev, 0, 0x00);
        if (id == 0xFFFFFFFFu) continue;
        u32 ht = driver_pcie_read_config(bus, dev, 0, 0x0C);
        int multi = (ht >> 16) & 0x80;
        int funcs = multi ? 8 : 1;
        for (u8 fn = 0; fn < funcs; fn++) {
            u32 fid = driver_pcie_read_config(bus, dev, fn, 0x00);
            if (fid == 0xFFFFFFFFu) continue;
            pcie_record_dev(bus, dev, fn);
            found++;
            /* bridge: descend into the secondary bus */
            u32 ht2 = driver_pcie_read_config(bus, dev, fn, 0x0C);
            if (((ht2 >> 16) & 0x7F) == 0x01) {
                u32 br = driver_pcie_read_config(bus, dev, fn, 0x18);
                u8 secondary = (u8)((br >> 8) & 0xFF);
                if (secondary) found += pcie_scan_bus(secondary);
            }
        }
    }
    return found;
}

int driver_pcie_scan(void) {
    g_dev_count = 0;
    memset(g_devs, 0, sizeof(g_devs));
    return pcie_scan_bus(0);
}

const driver_pcie_dev_t* driver_pcie_get(int index) {
    if (index < 0 || index >= g_dev_count) return NULL;
    return &g_devs[index];
}
int driver_pcie_count(void) { return g_dev_count; }

/* ------------------------------------------------------------------ */
/* BAR assignment                                                      */
/* ------------------------------------------------------------------ */

/* allocation pool: memory BARs from 0xC0000000 up (below the QEMU ECAM
 * window), I/O BARs from 0x2000 up (PIIX4 PM uses 0x600-0x7xx, ATA
 * legacy ports stay below 0x400). */
static u32 g_mem_next = 0xC0000000u;
static u32 g_io_next  = 0x2000u;

int driver_pcie_assign_bars(void) {
    int assigned = 0;
    for (int i = 0; i < g_dev_count; i++) {
        driver_pcie_dev_t *d = &g_devs[i];
        if (d->header_type != 0x00) continue;
        for (int b = 0; b < 6; b++) {
            u32 size = d->bar_size[b];
            if (!size) continue;
            u16 off = 0x10 + (u16)(b * 4);
            u32 raw = driver_pcie_read_config(d->bus, d->dev, d->fn, off);
            int is_io = d->bar_flags[b] & 1;
            u32 cur_base = is_io ? (raw & 0xFFFFFFFCu) : (raw & 0xFFFFFFF0u);
            if (cur_base) continue;    /* firmware assigned it */
            u32 base;
            if (is_io) {
                base = (g_io_next + size - 1) & ~(size - 1);
                g_io_next = base + size;
                base |= 1u;
            } else {
                base = (g_mem_next + size - 1) & ~(size - 1);
                g_mem_next = base + size;
            }
            driver_pcie_write_config(d->bus, d->dev, d->fn, off, base);
            d->bar_raw[b] = driver_pcie_read_config(d->bus, d->dev, d->fn, off);
            assigned++;
        }
    }
    return assigned;
}

/* ------------------------------------------------------------------ */
/* MSI / MSI-X                                                         */
/* ------------------------------------------------------------------ */

static u32 msi_message_address(void) {
    u32 dest = arch_lapic_present() ? (arch_lapic_id() & 0xFF) : 0;
    /* xAPIC physical destination, redirectable off */
    return 0xFEE00000u | (dest << 12);
}

int driver_pcie_msi_enable(const driver_pcie_dev_t *dev, u8 vector) {
    if (!dev || !dev->cap_msi) return -1;
    u8 p = dev->cap_msi;
    u16 ctrl = (u16)(driver_pcie_read_config(dev->bus, dev->dev, dev->fn, p) >> 16);
    int is64 = (ctrl >> 7) & 1;

    u16 addr_off = (u16)(p + 4);
    u16 data_off;
    if (is64) {
        driver_pcie_write_config(dev->bus, dev->dev, dev->fn, addr_off, msi_message_address());
        driver_pcie_write_config(dev->bus, dev->dev, dev->fn, (u16)(addr_off + 4), 0);
        data_off = (u16)(addr_off + 8);
    } else {
        driver_pcie_write_config(dev->bus, dev->dev, dev->fn, addr_off, msi_message_address());
        data_off = (u16)(addr_off + 4);
    }
    driver_pcie_write_config(dev->bus, dev->dev, dev->fn, data_off, vector);

    /* enable (bit 0), one message, vector-per-vector masking off */
    ctrl &= (u16)~0x0080;              /* clear per-vector mask bit */
    ctrl = (u16)((ctrl & ~0x0001u) | 0x0001u);
    u32 merged = (u32)(ctrl << 16) | (driver_pcie_read_config(dev->bus, dev->dev, dev->fn, p) & 0xFFFF);
    driver_pcie_write_config(dev->bus, dev->dev, dev->fn, p, merged);
    return 0;
}

int driver_pcie_msix_enable(const driver_pcie_dev_t *dev, int entry, u8 vector) {
    if (!dev || !dev->cap_msix) return -1;
    if (entry < 0 || (u32)entry >= dev->msix_count) return -1;
    /* find the BAR backing the table */
    u8  bar = dev->msix_bar;
    u32 base = dev->bar_raw[bar];
    int is_io = dev->bar_flags[bar] & 1;
    if (!base || is_io) return -1;   /* MSI-X tables live in MMIO */
    volatile u32 *tbl = (volatile u32*)(uintptr_t)
        ((u64)base + dev->msix_offset + (u32)entry * 16);
    tbl[0] = msi_message_address();
    tbl[1] = 0;
    tbl[2] = vector;
    /* unmask this entry (bit0=masked) and enable MSI-X (ctrl bit 15) */
    tbl[2] &= ~1u;
    u8 p = dev->cap_msix;
    u32 hdr = driver_pcie_read_config(dev->bus, dev->dev, dev->fn, p);
    u16 ctrl = (u16)(hdr >> 16);
    ctrl &= (u16)~0x4000;   /* function mask off */
    ctrl = (u16)(ctrl | 0x8000);   /* MSI-X enable */
    u32 merged = (u32)(ctrl << 16) | (hdr & 0xFFFF);
    driver_pcie_write_config(dev->bus, dev->dev, dev->fn, p, merged);
    return 0;
}

int driver_pcie_msi_disable(const driver_pcie_dev_t *dev) {
    if (!dev) return -1;
    u8 p = dev->cap_msix ? dev->cap_msix : dev->cap_msi;
    if (!p) return -1;
    u32 hdr = driver_pcie_read_config(dev->bus, dev->dev, dev->fn, p);
    u16 ctrl = (u16)((hdr >> 16) & ~0x8001u);   /* clear enable bit (either flavor) */
    u32 merged = (u32)(ctrl << 16) | (hdr & 0xFFFF);
    driver_pcie_write_config(dev->bus, dev->dev, dev->fn, p, merged);
    return 0;
}
