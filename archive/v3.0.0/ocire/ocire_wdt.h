/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP02-ocire_recovery
 * File: ocire/ocire_wdt.h
 * Purpose: Intel 6300ESB hardware watchdog + wall-clock delay for
 *          OCIRE.
 *
 * The 6300ESB watchdog is the recovery loop's enforcement hardware:
 * a real Intel server chipset timer (also emulated by QEMU as
 * -device i6300esb) with a two-stage countdown clocked at 1 kHz and
 * an arm/disarm protocol via PCI config + one I/O BAR:
 *
 *   PCI 8086:25ab, BAR0 (I/O):
 *     +0x00 TIMER1 (u32)  first-stage count
 *     +0x04 TIMER2 (u32)  second-stage count
 *     +0x0C RELOAD (u16)  unlock (write 0x80 then 0x86) + reload
 *                         (write 0x100) register
 *   PCI cfg:
 *     0x60  Config: bit5 REBOOT (reset machine on timeout)
 *     0x68  Lock:   bit1 ENABLE
 *
 * Timer values are seconds << 9 (two 1024-count stages at 1 kHz);
 * 60 s is written to both stages - the WP-02 "one minute" window.
 *
 * The supervisor arms the window before handing the machine to OCOS;
 * a hung OCOS that never sends its boot signal leaves the timer to
 * reset the machine back into the supervisor, which then sees the
 * stale WAIT_BOOT_SIGNAL mailbox and routes to the full recovery
 * shell.
 */

#ifndef OCIRE_WDT_H
#define OCIRE_WDT_H

#include "ocire_types.h"

/* Probe the PCI bus for the 6300ESB watchdog (8086:25ab) and resolve
 * BAR0.  Returns 0 when the device is present, -1 otherwise. */
int ocire_wdt_probe(void);

int ocire_wdt_present(void);          /* 1 after a successful probe */

/* Arm the watchdog for `seconds` (1..2046; both timer stages get the
 * full value) and enable reboot-on-timeout.  Returns 0, -1 when no
 * device / seconds out of range. */
int ocire_wdt_arm(int seconds);

/* Stop the watchdog (disable + del).  Returns 0, -1 when absent. */
int ocire_wdt_disarm(void);

/* Kick (reload) without changing the timeout. */
int ocire_wdt_kick(void);

/* Busy-wait `seconds` using the CMOS RTC seconds field (OCIRE has no
 * timer interrupts; the RTC is the one clock always present).  Used
 * for the visible crash-notice pause. */
void ocire_sleep_sec(int seconds);

#endif /* OCIRE_WDT_H */
