/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP05-ocbl
 * File: ocbl/common/ocbl_wdt.c
 * Purpose: 6300ESB watchdog driver (see ocbl_wdt.h).
 *
 * PCI access discipline (shared with the OCIRE driver, same
 * emulation): the config register at 0x60 latches on 16-bit writes,
 * the lock register at 0x68 only on 8-bit writes - a 32-bit
 * read-modify-write is silently ignored by the emulation, which is
 * why an armed watchdog would never fire.
 */
#include "ocbl_wdt.h"

#define PCI_ADDR 0xCF8u
#define PCI_DATA 0xCFCu

static void pci_addr(u8 bus, u8 dev, u8 fn, u8 off)
{
    u32 a = 0x80000000u | ((u32)bus << 16) | ((u32)dev << 11) |
            ((u32)fn << 8) | (off & 0xFC);
    __asm__ volatile("outl %0, %1" : : "a"(a), "dN"((u16)PCI_ADDR));
}

static u32 pci_read32(u8 bus, u8 dev, u8 fn, u8 off)
{
    u32 v;
    pci_addr(bus, dev, fn, off);
    __asm__ volatile("inl %1, %0" : "=a"(v) : "dN"((u16)PCI_DATA));
    return v;
}

static void pci_write16(u8 bus, u8 dev, u8 fn, u8 off, u16 val)
{
    pci_addr(bus, dev, fn, off);
    __asm__ volatile("outw %0, %1" : : "a"(val),
                     "dN"((u16)(PCI_DATA + (off & 3))));
}

static void pci_write8(u8 bus, u8 dev, u8 fn, u8 off, u8 val)
{
    pci_addr(bus, dev, fn, off);
    __asm__ volatile("outb %0, %1" : : "a"(val),
                     "dN"((u16)(PCI_DATA + (off & 3))));
}

static u8 pci_read8(u8 bus, u8 dev, u8 fn, u8 off)
{
    u32 v = pci_read32(bus, dev, fn, (u8)(off & ~3u));
    return (u8)(v >> ((off & 3) * 8));
}

static int g_found;
static u8 g_bus, g_dev, g_fn;
static u16 g_io;               /* BAR0 I/O base (BAR bit0 = 1) */
static u32 g_mem;              /* BAR0 MMIO base otherwise */

int ocbl_wdt_probe(void)
{
    g_found = 0;
    for (u16 bus = 0; bus < 2 && !g_found; bus++) {
        for (u8 dev = 0; dev < 32 && !g_found; dev++) {
            for (u8 fn = 0; fn < 8; fn++) {
                u32 id = pci_read32((u8)bus, dev, fn, 0);
                if (id == 0xFFFFFFFFu)
                    continue;
                if ((id & 0xFFFF) != 0x8086 ||
                    ((id >> 16) & 0xFFFF) != 0x25AB)
                    continue;
                u32 bar = pci_read32((u8)bus, dev, fn, 0x10);
                if (!bar)
                    continue;
                g_bus = (u8)bus;
                g_dev = dev;
                g_fn = fn;
                if (bar & 1) {
                    g_io = (u16)(bar & ~0x3u);
                    g_mem = 0;
                } else {
                    g_io = 0;
                    g_mem = bar & ~0xFu;
                }
                g_found = 1;
                break;
            }
        }
    }
    return g_found ? 0 : -1;
}

int ocbl_wdt_present(void)
{
    return g_found;
}

static void wdt_outw(u8 off, u16 val)
{
    if (g_io) {
        __asm__ volatile("outw %0, %1" : : "a"(val),
                         "dN"((u16)(g_io + off)));
    } else {
        *(volatile u16*)(u64)(g_mem + off) = val;
    }
}

static void wdt_outl(u8 off, u32 val)
{
    if (g_io) {
        __asm__ volatile("outl %0, %1" : : "a"(val),
                         "dN"((u16)(g_io + off)));
    } else {
        *(volatile u32*)(u64)(g_mem + off) = val;
    }
}

/* Datasheet unlock: write 0x80 then 0x86 to RELOAD; the config regs
 * accept ONE write per unlock. */
static void wdt_unlock(void)
{
    wdt_outw(0x0C, 0x80);
    wdt_outw(0x0C, 0x86);
}

int ocbl_wdt_arm(int seconds)
{
    if (!g_found)
        return -1;
    if (seconds < 1 || seconds > 2046)
        return -1;
    /* 1 kHz decrement scale; a stage counts seconds << 9 ticks and
     * one reload spans both stages. */
    u32 t = (u32)seconds << 9;
    /* Config write INVERTS the reboot sense: bit5 CLEAR selects
     * reboot-on-timeout + the 1 kHz scale. */
    pci_write16(g_bus, g_dev, g_fn, 0x60, 0x0000);
    wdt_unlock();
    wdt_outl(0x00, t);                    /* TIMER1 */
    wdt_unlock();
    wdt_outl(0x04, t);                    /* TIMER2 */
    wdt_unlock();
    wdt_outw(0x0C, 0x100);                /* RELOAD now */
    pci_write8(g_bus, g_dev, g_fn, 0x68, 0x02);   /* ENABLE */
    return 0;
}

int ocbl_wdt_kick(void)
{
    if (!g_found)
        return -1;
    wdt_unlock();
    wdt_outw(0x0C, 0x100);
    return 0;
}

int ocbl_wdt_disarm(void)
{
    if (!g_found)
        return -1;
    wdt_unlock();
    wdt_outw(0x0C, 0x100);
    pci_write8(g_bus, g_dev, g_fn, 0x68, 0x00);
    (void)pci_read8(g_bus, g_dev, g_fn, 0x68);
    return 0;
}
