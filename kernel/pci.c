/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-06
 * File: kernel/pci.c
 * Purpose: PCI bus driver implementation.
 */
#include "pci.h"
#include "console.h"
#include "string.h"

static inline void outl(u16 port, u32 val) {
    __asm__ volatile("outl %0, %1" :: "a"(val), "Nd"(port));
}
static inline u32 inl(u16 port) {
    u32 v;
    __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

u32 pci_read_config(u8 bus, u8 dev, u8 func, u8 offset) {
    u32 addr = ((u32)bus << 16) | ((u32)dev << 11) | ((u32)func << 8)
               | ((u32)offset & 0xFC) | 0x80000000u;
    outl(PCI_CONFIG_ADDR, addr);
    return inl(PCI_CONFIG_DATA);
}

void pci_write_config(u8 bus, u8 dev, u8 func, u8 offset, u32 value) {
    u32 addr = ((u32)bus << 16) | ((u32)dev << 11) | ((u32)func << 8)
               | ((u32)offset & 0xFC) | 0x80000000u;
    outl(PCI_CONFIG_ADDR, addr);
    outl(PCI_CONFIG_DATA, value);
}

int pci_find_device(u16 vendor, u16 device, u8 *bus_out, u8 *dev_out, u8 *func_out) {
    for (u32 bus = 0; bus < 256; bus++) {
        for (u8 dev = 0; dev < 32; dev++) {
            for (u8 func = 0; func < 8; func++) {
                u32 id = pci_read_config((u8)bus, dev, func, 0);
                if (id == 0xFFFFFFFF) continue;
                if ((id & 0xFFFF) == vendor && ((id >> 16) & 0xFFFF) == device) {
                    if (bus_out) *bus_out = (u8)bus;
                    if (dev_out) *dev_out = dev;
                    if (func_out) *func_out = func;
                    return 0;
                }
            }
        }
    }
    return -1;
}

int pci_find_class(u32 class_code, u8 *bus_out, u8 *dev_out, u8 *func_out) {
    for (u32 bus = 0; bus < 256; bus++) {
        for (u8 dev = 0; dev < 32; dev++) {
            for (u8 func = 0; func < 8; func++) {
                u32 id = pci_read_config((u8)bus, dev, func, 0);
                if (id == 0xFFFFFFFF) continue;
                u32 cls = pci_read_config((u8)bus, dev, func, 0x08);
                if ((cls >> 24) == (class_code >> 24)) {
                    if (bus_out) *bus_out = (u8)bus;
                    if (dev_out) *dev_out = dev;
                    if (func_out) *func_out = func;
                    return 0;
                }
            }
        }
    }
    return -1;
}

/* WP-10a: exact 24-bit class match (base + subclass + prog-if), with an
 * occurrence index so callers can enumerate all matching controllers. */
int pci_find_class_exact(u32 class_code, int nth,
                         u8 *bus_out, u8 *dev_out, u8 *func_out) {
    return pci_find_class_mask(class_code, 0xFFFFFFu, nth,
                               bus_out, dev_out, func_out);
}

/* WP-10a: masked class match - only the bits set in `mask` (24-bit class
 * triple) are compared. */
int pci_find_class_mask(u32 class_code, u32 mask, int nth,
                        u8 *bus_out, u8 *dev_out, u8 *func_out) {
    int seen = 0;
    for (u32 bus = 0; bus < 256; bus++) {
        for (u8 dev = 0; dev < 32; dev++) {
            for (u8 func = 0; func < 8; func++) {
                u32 id = pci_read_config((u8)bus, dev, func, 0);
                if (id == 0xFFFFFFFF) continue;
                u32 cls = pci_read_config((u8)bus, dev, func, 0x08);
                if (((cls >> 8) & mask) != (class_code & mask)) continue;
                if (seen == nth) {
                    if (bus_out) *bus_out = (u8)bus;
                    if (dev_out) *dev_out = dev;
                    if (func_out) *func_out = func;
                    return 0;
                }
                seen++;
            }
        }
    }
    return -1;
}

u32 pci_read_bar(u8 bus, u8 dev, u8 func, int bar_index) {
    u8 offset = 0x10 + (u8)(bar_index * 4);
    u32 bar = pci_read_config(bus, dev, func, offset);
    if (bar & 1) {
        /* I/O space: bit 0 set, base = bits 31-2 */
        return bar & 0xFFFFFFFC;
    } else {
        /* Memory space. P2-24 FIX: check for 64-bit BAR (bits 1-2 = 0b10).
         * If 64-bit, read the next BAR register for the high 32 bits. */
        int type = (bar >> 1) & 3;
        if (type == 2 && bar_index < 5) {
            /* 64-bit memory BAR: combine low and high 32 bits. */
            u32 bar_hi = pci_read_config(bus, dev, func, offset + 4);
            u64 full = ((u64)bar_hi << 32) | (bar & 0xFFFFFFF0ULL);
            /* Return the low 32 bits (sufficient for < 4GiB addresses). */
            return (u32)(full & 0xFFFFFFFF);
        }
        /* 32-bit memory BAR: base = bits 31-4. */
        return bar & 0xFFFFFFF0;
    }
}

void pci_init(void) {
    /* Nothing to init, PCI config space is always accessible. */
}

void pci_list_devices(void) {
    oc_console_puts("PCI devices:\n");
    int found = 0;
    for (u32 bus = 0; bus < 256; bus++) {
        for (u8 dev = 0; dev < 32; dev++) {
            for (u8 func = 0; func < 8; func++) {
                u32 id = pci_read_config((u8)bus, dev, func, 0);
                if (id == 0xFFFFFFFF) continue;
                u32 cls = pci_read_config((u8)bus, dev, func, 0x08);
                char buf[80]; char n[20];
                oc_strcpy(buf, "  ");
                oc_u64_to_hex((u64)bus, n, 2); oc_strcpy(buf+oc_strlen(buf), n);
                oc_strcpy(buf+oc_strlen(buf), ":");
                oc_u64_to_hex((u64)dev, n, 2); oc_strcpy(buf+oc_strlen(buf), n);
                oc_strcpy(buf+oc_strlen(buf), ".");
                oc_u64_to_hex((u64)func, n, 1); oc_strcpy(buf+oc_strlen(buf), n);
                oc_strcpy(buf+oc_strlen(buf), " vendor=");
                oc_u64_to_hex((u64)(id & 0xFFFF), n, 4); oc_strcpy(buf+oc_strlen(buf), "0x"); oc_strcpy(buf+oc_strlen(buf), n);
                oc_strcpy(buf+oc_strlen(buf), " device=");
                oc_u64_to_hex((u64)((id >> 16) & 0xFFFF), n, 4); oc_strcpy(buf+oc_strlen(buf), "0x"); oc_strcpy(buf+oc_strlen(buf), n);
                oc_strcpy(buf+oc_strlen(buf), " class=");
                oc_u64_to_hex((u64)(cls >> 24), n, 2); oc_strcpy(buf+oc_strlen(buf), "0x"); oc_strcpy(buf+oc_strlen(buf), n);
                oc_strcpy(buf+oc_strlen(buf), "\n");
                oc_console_puts(buf);
                found++;
                if (found >= 16) {
                    oc_console_puts("  ... (truncated)\n");
                    return;
                }
            }
        }
    }
    if (found == 0) {
        oc_console_puts("  (no devices found)\n");
    }
}

/* Enable bus mastering and I/O/MEM access for a PCI device.
 * This sets bit 0 (I/O), bit 1 (MEM), and bit 2 (BUS_MASTER) in the
 * PCI command register at config offset 0x04.
 * Without bus master, the device cannot do DMA. */
void pci_enable_device(u8 bus, u8 dev, u8 func) {
    u32 cmd = pci_read_config(bus, dev, func, 0x04);
    cmd |= 0x07;  /* IO + MEM + BUS_MASTER */
    pci_write_config(bus, dev, func, 0x04, cmd);
}
