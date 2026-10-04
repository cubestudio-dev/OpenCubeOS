/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10d-fix2
 * File: kernel/power.c
 * Purpose: power management - shutdown / suspend / halt / reboot.
 *
 * Real hardware paths only - no stubs:
 *   - QEMU/PIIX4 ACPI power-off: PM1a_CNT at port 0x604, write 0x2000
 *     (SLP_EN bit 13).  This is the port the i440fx machine exposes when
 *     ACPI is enabled (the default under SeaBIOS).
 *   - Bochs and old QEMU builds: port 0xB004, value 0x2000.
 *   - Legacy APM: word write of 0x0000 to port 0xF000.
 *   - Reboot: 8042 keyboard-controller pulse (port 0x64, cmd 0xFE) with
 *     the ACPI RESET_REG (port 0xCF9, "hard reset" sequence 0x02 then
 *     0x06) as fallback - the same path the WP-10u reboot used.
 *   - Halt: cli + hlt loop (interrupts stay disabled so nothing wakes us).
 *
 * Every shutdown/reboot path first flushes every present block device:
 * the FAT32 /etc volume, the A/B flag partition and any mounted fs must
 * survive the power transition (same guarantee the WP-10u reboot gave).
 */
#include "power.h"
#include "blk.h"
#include "console.h"

#include <stdint.h>

/* ------------------------------------------------------------------ */
/* port I/O (self-contained; ab_update.c keeps its own copies)         */
/* ------------------------------------------------------------------ */

static inline void pwr_outb(u16 port, u8 val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline void pwr_outw(u16 port, u16 val) {
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline u8 pwr_inb(u16 port) {
    u8 v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static void pwr_io_wait(void) {
    for (int i = 0; i < 100000; i++) {
        u8 st = pwr_inb(0x64);
        if (!(st & 2)) break;
    }
}

/* ------------------------------------------------------------------ */
/* block-device flush (before every power transition)                  */
/* ------------------------------------------------------------------ */

void power_flush_blk(void) {
    int n = blk_num_devices();
    for (int i = 0; i < n; i++) {
        blk_device_t *dev = blk_get_device(i);
        if (dev && dev->present) blk_flush(dev);
    }
}

/* ------------------------------------------------------------------ */
/* halt                                                                */
/* ------------------------------------------------------------------ */

void power_halt(void) {
    oc_console_puts("System halted.\n");
    oc_console_show_cursor(0);
    __asm__ volatile("cli");
    for (;;) __asm__ volatile("hlt");
}

/* ------------------------------------------------------------------ */
/* reboot (moved from kernel/ab_update.c, WP-10u path preserved)        */
/* ------------------------------------------------------------------ */

void power_reboot(void) {
    /* flush every block device so the update survives the reset */
    power_flush_blk();
    oc_console_puts("Rebooting...\n");
    /* 8042 keyboard controller reset (mainstream PC method) */
    pwr_io_wait();
    pwr_outb(0x64, 0xFE);
    /* fallback: ACPI RESET_REG port 0xCF9 (works on QEMU/edk2) */
    pwr_io_wait();
    pwr_outb(0xCF9, 0x02);
    pwr_outb(0xCF9, 0x06);
    for (;;) {
        __asm__ volatile("hlt");
    }
}

/* ------------------------------------------------------------------ */
/* shutdown                                                            */
/* ------------------------------------------------------------------ */

int power_shutdown(void) {
    power_flush_blk();
    oc_console_puts("Powering off...\n");

    /* 1. QEMU / PIIX4 ACPI PM1a_CNT (SLP_EN, SLP_TYP = 0) */
    pwr_outw(0x604, 0x2000);
    /* 2. Bochs-compatible shutdown port */
    pwr_outw(0xB004, 0x2000);
    /* 3. legacy APM shutdown word */
    pwr_outw(0xF000, 0x0000);

    /* give the ports a moment; if we are still here the machine
     * does not support any known power-off path */
    for (volatile int i = 0; i < 2000000; i++) { /* spin briefly */ }
    return OC_POWER_E_UNSUPPORTED;
}

/* ------------------------------------------------------------------ */
/* suspend                                                             */
/* ------------------------------------------------------------------ */

int power_suspend(void) {
    /* ACPI S3 (suspend to RAM) needs FADT parsing, a wake vector and a
     * resume path - WP-10e territory.  Be honest instead of pretending. */
    return OC_POWER_E_UNSUPPORTED;
}

/* ------------------------------------------------------------------ */
/* shell command handlers                                              */
/* ------------------------------------------------------------------ */

int cmd_shutdown(const char *args) {
    (void)args;
    oc_console_puts("Shutting down...\n");
    if (power_shutdown() != 0) {
        oc_console_puts("Power off not supported. System halted.\n");
        power_halt();
    }
    return 0; /* unreachable on success */
}

int cmd_suspend(const char *args) {
    (void)args;
    oc_console_puts("Suspending...\n");
    if (power_suspend() != 0) {
        oc_console_puts("Suspend not supported (ACPI required).\n");
        return 1;
    }
    return 0; /* unreachable until S3 exists */
}

int cmd_reboot(const char *args) {
    (void)args;
    /* moved from ab_update.c (WP-10u): flush + 8042 + 0xCF9 fallback */
    power_reboot();
    return 0; /* unreachable */
}

/* ------------------------------------------------------------------ */
/* L1 extension surface (kernel/ext.h, WP-10d-fix2)                    */
/* ------------------------------------------------------------------ */

int oc_ext_power_shutdown(void) {
    return power_shutdown();
}

int oc_ext_power_suspend(void) {
    return power_suspend();
}

void oc_ext_power_halt(void) {
    power_halt();
}

void oc_ext_power_reboot(void) {
    power_reboot();
}
