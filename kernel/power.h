/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10d-fix2
 * File: kernel/power.h
 * Purpose: power management - shutdown / suspend / halt / reboot.
 *
 * L0->L1 extension surface (4 interfaces, also wrapped as oc_ext_power_*
 * in kernel/ext.h):
 *
 *   void power_halt(void);      stop the CPU forever (cli + hlt loop).
 *   void power_reboot(void);    flush every block device, then reset via
 *                               the 8042 keyboard controller with the
 *                               ACPI RESET_REG (port 0xCF9) fallback.
 *   int  power_shutdown(void);  flush every block device, then ask the
 *                               machine to power off.  Tries, in order:
 *                                 1. QEMU/PIIX4 ACPI PM1a_CNT (port 0x604,
 *                                    value 0x2000 = SLP_EN)
 *                                 2. Bochs-compatible shutdown port
 *                                    (0xB004, value 0x2000)
 *                                 3. legacy APM shutdown word (0xF000, 0)
 *                               Returns -1 when the machine is still
 *                               running after all attempts (caller should
 *                               power_halt()).  On success this function
 *                               never returns.
 *   int  power_suspend(void);   suspend to RAM (ACPI S3).  Open Cube OS
 *                               does not implement the ACPI S3 wake path
 *                               yet, so this returns OC_POWER_E_UNSUPPORTED
 *                               and the caller prints an honest message.
 *
 * All block-device caches are flushed BEFORE any reset/shutdown attempt,
 * so an A/B update or user data survives the power transition.
 *
 * Shell commands (registered in kmain.c):
 *   shutdown / poweroff  -> power_shutdown (with "System halted." fallback)
 *   suspend / sleep      -> power_suspend (honest "not supported" reply)
 *   halt                 -> power_halt
 *   reboot               -> power_reboot (moved here from ab_update.c)
 */
#ifndef OC_POWER_H
#define OC_POWER_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* power_shutdown() return codes. */
#define OC_POWER_OFF          0   /* never observed: success never returns */
#define OC_POWER_E_UNSUPPORTED (-1) /* machine still running after all ports */

/* Stop the CPU forever: hide cursor, cli, hlt loop.  Never returns. */
void power_halt(void);

/* Flush all block devices and reset the machine.  Never returns. */
void power_reboot(void);

/* Flush all block devices and try every known power-off port.  Returns
 * OC_POWER_E_UNSUPPORTED when still running (never returns on success). */
int power_shutdown(void);

/* Suspend to RAM.  Returns OC_POWER_E_UNSUPPORTED (ACPI S3 wake path is
 * not implemented); never blocks, never powers down. */
int power_suspend(void);

/* Flush every present block device (used before shutdown/reboot). */
void power_flush_blk(void);

/* Shell command handlers (registered in kmain.c). */
int cmd_shutdown(const char *args);   /* shutdown + poweroff alias */
int cmd_suspend(const char *args);    /* suspend  + sleep alias    */
int cmd_reboot(const char *args);     /* reboot (moved from ab_update.c) */

#ifdef __cplusplus
}
#endif

#endif /* OC_POWER_H */
