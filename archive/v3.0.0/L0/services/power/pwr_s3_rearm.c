/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP03
 * File: services/power/pwr_s3_rearm.c
 * Purpose: platform re-arm after a live S3 (suspend-to-RAM) resume.
 *
 * Moved verbatim out of drivers/acpi/driver_acpi_power.c (G2): the
 * sequence replays PIC / IRQ / PIT / LAPIC / PCI-command / NIC /
 * legacy-e1000 / serial-UART state that the platform reset cleared.
 * It lives here because a driver must not call into the net root
 * service (net_e1000_resume) - the power service may depend on every
 * layer.  core_power_suspend() runs this, then the ACPI-side rearm
 * (driver_acpi_power_rearm_acpi), then re-enables interrupts: the
 * original single-path ordering is preserved exactly.
 */
#include "types.h"
#include "arch_pic.h"
#include "arch_irq.h"
#include "core_timer.h"
#include "arch_lapic.h"
#include "driver_pci.h"
#include "nic_svc.h"          /* drivers/nic framework: active NIC replay */
#include "net_core.h"         /* legacy built-in e1000 fallback resume    */
#include "screen_serial_in.h" /* COM1 RX IRQ reprogram                   */

void pwr_s3_rearm(void)
{
    /* PIC was reset by the firmware */
    arch_pic_remap();
    arch_irq_rearm();
    /* PIT was reset with the platform. fix2: reprogram it WITHOUT
     * core_timer_init() - that memset wiped the whole soft-timer
     * table, silently killing the 10 ms network poll timer (and
     * every other registered timer) after every resume. */
    core_timer_resume();
    /* LAPIC re-enable when present */
    arch_lapic_init();
    /* fix2: the platform reset cleared every PCI command register
     * (bus mastering + MMIO/IO decode gone). Re-enable the devices
     * the kernel drives, then replay the active NIC's register
     * programming - otherwise the resumed system is half-dead:
     * shell alive but disk/NIC/USB silent. */
    driver_pci_restore_commands();
    driver_nic_resume();
    net_e1000_resume();
    /* the platform reset cleared the UART's RX interrupt enable, so
     * serial input died with the sleep - reprogram the COM1 RX path
     * (the IRQ4 handler slot is RAM-resident and still live). */
    screen_serial_in_init();
}
