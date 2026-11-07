/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP02-ocire_recovery
 * File: ocire/ocire_wdt.c
 * Purpose: Intel 6300ESB watchdog driver (see ocire_wdt.h).
 *
 * The register protocol mirrors the Intel 6300ESB datasheet and the
 * canonical Linux driver (drivers/watchdog/i6300esb.c): unlock the
 * reload register with the 0x80/0x86 magic pair, program both timer
 * stages, then reload and flip the enable bit in PCI config.
 */

#include "ocire_wdt.h"
#include "ocire_io.h"
#include "ocire_util.h"

/* ---- PCI config access (CF8/CFC) ---- */

#define PCI_ADDR 0xCF8
#define PCI_DATA 0xCFC

static u32 pci_read32(u8 bus, u8 dev, u8 fn, u8 off) {
    oc_outl(PCI_ADDR, 0x80000000u | ((u32)bus << 16) | ((u32)dev << 11) |
                       ((u32)fn << 8) | (off & 0xFC));
    return oc_inl(PCI_DATA);
}

/* Real-width config writes: the 6300ESB emulation (like the real
 * chipset) reacts on the ACCESS WIDTH of the config write - the
 * config register at 0x60 only latches on 16-bit writes and the lock
 * register at 0x68 only on 8-bit ones.  A 32-bit read-modify-write
 * is silently ignored (it falls through to the generic config space
 * handler), which is why an armed watchdog would never fire. */
static void pci_addr(u8 bus, u8 dev, u8 fn, u8 off) {
    oc_outl(PCI_ADDR, 0x80000000u | ((u32)bus << 16) | ((u32)dev << 11) |
                       ((u32)fn << 8) | (off & 0xFC));
}

static void pci_write16(u8 bus, u8 dev, u8 fn, u8 off, u16 val) {
    pci_addr(bus, dev, fn, off);
    oc_outw((u16)(PCI_DATA + (off & 3)), val);
}

static void pci_write8(u8 bus, u8 dev, u8 fn, u8 off, u8 val) {
    pci_addr(bus, dev, fn, off);
    oc_outb((u16)(PCI_DATA + (off & 3)), val);
}

static u8 pci_read8(u8 bus, u8 dev, u8 fn, u8 off) {
    u32 v = pci_read32(bus, dev, fn, off & ~3u);
    return (u8)(v >> ((off & 3) * 8));
}

/* ---- device state ---- */

static int g_found;
static u8  g_bus, g_dev, g_fn;
static u16 g_io;                  /* BAR0 I/O base (when g_is_io) */
static u32 g_mem;                 /* BAR0 MMIO base (when !g_is_io) */
static int g_is_io;

/* BAR0 on the QEMU machine is a 32-bit MEMORY BAR (the emulation
 * maps the register block as MMIO; real 6300ESB boards may use an
 * I/O BAR - both are accepted here).  OCIRE's 4 GiB identity map
 * makes the MMIO base directly dereferenceable. */
int ocire_wdt_probe(void) {
    g_found = 0;
    for (u16 bus = 0; bus < 2 && !g_found; bus++) {
        for (u8 dev = 0; dev < 32 && !g_found; dev++) {
            for (u8 fn = 0; fn < 8; fn++) {
                u32 id = pci_read32((u8)bus, dev, fn, 0);
                if (id == 0xFFFFFFFFu) continue;
                if ((id & 0xFFFF) != 0x8086 || ((id >> 16) & 0xFFFF) != 0x25AB)
                    continue;
                u32 bar = pci_read32((u8)bus, dev, fn, 0x10);
                if (bar == 0) continue;            /* not mapped yet */
                g_bus = (u8)bus; g_dev = dev; g_fn = fn;
                if (bar & 1) {
                    g_is_io = 1;
                    g_io = (u16)(bar & ~0x3u);
                } else {
                    g_is_io = 0;
                    g_mem = bar & ~0xFu;
                }
                g_found = 1;
                break;
            }
        }
    }
    return g_found ? 0 : -1;
}

int ocire_wdt_present(void) {
    return g_found;
}

/* ---- register helpers ---- */

static void wdt_outw(u8 off, u16 val) {
    if (g_is_io) {
        oc_outw((u16)(g_io + off), val);
    } else {
        *(volatile u16 *)(uintptr)(g_mem + off) = val;
    }
}

static void wdt_outl(u8 off, u32 val) {
    if (g_is_io) {
        oc_outl((u16)(g_io + off), val);
    } else {
        *(volatile u32 *)(uintptr)(g_mem + off) = val;
    }
}

/* Unlock sequence for the reload register (datasheet: write 0x80
 * then 0x86 to RELOAD; after that the config regs accept ONE write
 * before needing another unlock). */
static void wdt_unlock(void) {
    wdt_outw(0x0C, 0x80);
    wdt_outw(0x0C, 0x86);
}

static int missing(void) {
    return g_found ? 0 : -1;
}

int ocire_wdt_arm(int seconds) {
    if (missing()) return -1;
    if (seconds < 1 || seconds > 2046) return -1;

    /* Timer counts run at 1 kHz; a stage counts down `seconds << 9`
     * ticks.  Both stages get the full window (the canonical setup -
     * a single reload spans both). */
    u32 t = (u32)seconds << 9;

    /* Config register (0x60, 16-bit access).  The QEMU emulation (and
     * the real ESB register layout it follows) INVERTS the reboot
     * sense: reboot_enabled becomes true when the write does NOT set
     * bit 5 (i6300esb_config_write: reboot_enabled = (data &
     * ESB_WDT_REBOOT) == 0).  Writing 0x0000 selects "reboot on
     * timeout" + the 1 kHz decrement scale. */
    pci_write16(g_bus, g_dev, g_fn, 0x60, 0x0000);

    wdt_unlock();
    wdt_outl(0x00, t);                 /* TIMER1 */
    wdt_unlock();
    wdt_outl(0x04, t);                 /* TIMER2 */
    wdt_unlock();
    wdt_outw(0x0C, 0x100);             /* RELOAD: prevent timeout now */

    /* Lock register (0x68): ENABLE (bit1). */
    pci_write8(g_bus, g_dev, g_fn, 0x68, 0x02);
    return 0;
}

int ocire_wdt_disarm(void) {
    if (missing()) return -1;
    /* Reset the timers first (as the datasheet suggests), then clear
     * the enable bit. */
    wdt_unlock();
    wdt_outw(0x0C, 0x100);
    pci_write8(g_bus, g_dev, g_fn, 0x68, 0x00);
    (void)pci_read8(g_bus, g_dev, g_fn, 0x68);
    return 0;
}

int ocire_wdt_kick(void) {
    if (missing()) return -1;
    wdt_unlock();
    wdt_outw(0x0C, 0x100);
    return 0;
}

/* ---- CMOS RTC seconds polling (sleep helper) ---- */

#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71
#define RTC_SEC   0x00
#define RTC_REG_B 0x0B

static u8 cmos_read(u8 reg) {
    oc_outb(CMOS_ADDR, reg);
    oc_io_wait();
    return oc_inb(CMOS_DATA);
}

static int rtc_bcd_mode(void) {
    return (cmos_read(RTC_REG_B) & 0x04) ? 0 : 1;   /* bit 2 = binary */
}

static u8 rtc_seconds(void) {
    u8 s = cmos_read(RTC_SEC);
    if (rtc_bcd_mode()) {
        s = (u8)((s & 0x0F) + ((s >> 4) & 0x07) * 10);
    }
    return s;
}

void ocire_sleep_sec(int seconds) {
    if (seconds <= 0) return;
    u8 last = rtc_seconds();
    int elapsed = 0;
    while (elapsed < seconds) {
        u8 s = rtc_seconds();
        if (s != last) {
            last = s;
            elapsed++;
        }
        oc_io_wait();                    /* gentle port-read delay */
    }
}
