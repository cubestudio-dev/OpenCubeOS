/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10d-fix2
 * File: kernel/core/core_power.c
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
 *   - Halt: flush every block device first (same guarantee shutdown/
 *     reboot give - BUG-0288), then cli + hlt loop (interrupts stay
 *     disabled so nothing wakes us).
 *
 * Every shutdown/reboot/halt path first flushes every present block
 * device: the FAT32 /etc volume, the A/B flag partition and any mounted
 * fs must survive the power transition (same guarantee the WP-10u
 * reboot gave).
 */
#include "core_power.h"
#include "driver_block_blk.h"
#include "driver_acpi.h"
#include "driver_acpi_power.h"
#include "screen_console.h"
#include "core_timer.h"
#include "core_sched.h"

#include <stdint.h>

/* ------------------------------------------------------------------ */
/* port I/O (self-contained; ota_ab_update.c keeps its own copies)         */
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

void core_power_flush_blk(void) {
    int n = driver_block_num_devices();
    for (int i = 0; i < n; i++) {
        driver_block_device_t *dev = driver_block_get_device(i);
        if (dev && dev->present) driver_block_flush(dev);
    }
}

/* ------------------------------------------------------------------ */
/* halt                                                                */
/* ------------------------------------------------------------------ */

void core_power_halt(void) {
    /* BUG-0288 FIX (A3-11): halt previously stopped the CPU WITHOUT
     * flushing the block write-back cache, while shutdown/reboot both
     * flush first.  Dirty cache contents (driver_block_write_sectors
     * callers) were silently lost at the next power transition, and the
     * shutdown-failure fallback below halts too - one fix covers both.
     * Same ordering as the other two paths: flush BEFORE any output or
     * state change so a flush error is still visible. */
    core_power_flush_blk();
    screen_console_puts("System halted.\n");
    screen_console_show_cursor(0);
    __asm__ volatile("cli");
    for (;;) __asm__ volatile("hlt");
}

/* ------------------------------------------------------------------ */
/* reboot (moved from kernel/ota_ab_update.c, WP-10u path preserved)        */
/* ------------------------------------------------------------------ */

void core_power_reboot(void) {
    /* flush every block device so the update survives the reset */
    core_power_flush_blk();
    screen_console_puts("Rebooting...\n");
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

int core_power_shutdown(void) {
    core_power_flush_blk();
    screen_console_puts("Powering off...\n");

    /* WP-10-input_chipset: real ACPI S5 through the FADT + \_S5 package
     * when the platform provides it (the mainstream path); the legacy
     * ports below remain as fallback for ACPI-less platforms. */
    if (driver_acpi_available() && driver_acpi_power_slp_typ(5) >= 0) {
        if (driver_acpi_power_enter_s5() == 0) return 0;   /* unreachable */
    }

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

int core_power_suspend(void) {
    /* WP-10-input_chipset: real ACPI S3 (suspend to RAM) through the
     * FADT PM1a_CNT + \_S3 package + the FACS waking vector. The
     * machine stops in the sleep-write asm stub and resumes through
     * the 32-bit trampoline (see driver_acpi_power.c). */
    if (driver_acpi_available() && driver_acpi_power_slp_typ(3) >= 0) {
        screen_console_puts("Entering S3 (suspend to RAM)...\n");
        int rc = driver_acpi_power_enter_s3();
        if (rc == 0) {
            screen_console_puts("Resumed from S3.\n");
            return 0;
        }
        screen_console_puts("S3 resume failed.\n");
        return rc;
    }
    /* ACPI-less platform: be honest instead of pretending. */
    return OC_POWER_E_UNSUPPORTED;
}

/* ------------------------------------------------------------------ */
/* shell command handlers                                              */
/* ------------------------------------------------------------------ */

int shell_cmd_shutdown(const char *args) {
    (void)args;
    screen_console_puts("Shutting down...\n");
    if (core_power_shutdown() != 0) {
        screen_console_puts("Power off not supported. System halted.\n");
        core_power_halt();
    }
    return 0; /* unreachable on success */
}

int shell_cmd_suspend(const char *args) {
    (void)args;
    screen_console_puts("Suspending...\n");
    if (core_power_suspend() != 0) {
        screen_console_puts("Suspend not supported (ACPI required).\n");
        return 1;
    }
    return 0; /* unreachable until S3 exists */
}

/* BUG-0346 (RUNTIME): 'sleep' now means DELAY - the universal meaning
 * of the command.  It used to be a literal alias of suspend, so
 * 'sleep 2' printed "Suspending... Suspend not supported (ACPI
 * required)" instead of waiting 2 seconds.  Suspending to RAM keeps
 * its own command ('suspend'); this handler parses <seconds> with up
 * to 3 fractional digits (milliseconds) and yields until the wall
 * clock passes the deadline. */
int shell_cmd_sleep(const char *args) {
    if (!args || !args[0]) {
        screen_console_puts("usage: sleep <seconds>  (e.g. sleep 2, sleep 0.5)\n");
        return 1;
    }
    u64 whole = 0;
    u64 frac_ms = 0;
    int i = 0;
    while (args[i] >= '0' && args[i] <= '9') {
        if (whole < 1000000ull) whole = whole * 10u + (u64)(args[i] - '0');
        i++;
    }
    if (args[i] == '.') {
        i++;
        int digits = 0;
        while (args[i] >= '0' && args[i] <= '9') {
            if (digits < 3) frac_ms = frac_ms * 10u + (u64)(args[i] - '0');
            i++;
            digits++;
        }
        while (digits < 3) { frac_ms *= 10u; digits++; }  /* scale to ms */
    }
    while (args[i] == ' ') i++;
    if (args[i] != 0) {
        screen_console_puts("sleep: invalid time\n");
        return 1;
    }
    u64 deadline = core_timer_now_ms() + whole * 1000ull + frac_ms;
    while (core_timer_now_ms() < deadline) core_sched_yield();
    return 0;
}

int shell_cmd_reboot(const char *args) {
    (void)args;
    /* moved from ota_ab_update.c (WP-10u): flush + 8042 + 0xCF9 fallback */
    core_power_reboot();
    return 0; /* unreachable */
}

/* ------------------------------------------------------------------ */
/* L1 extension surface (kernel/ext.h, WP-10d-fix2)                    */
/* ------------------------------------------------------------------ */

int l1_ext_power_shutdown(void) {
    return core_power_shutdown();
}

int l1_ext_power_suspend(void) {
    return core_power_suspend();
}

void l1_ext_power_halt(void) {
    core_power_halt();
}

void l1_ext_power_reboot(void) {
    core_power_reboot();
}
