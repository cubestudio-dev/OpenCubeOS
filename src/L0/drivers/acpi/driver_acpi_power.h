/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-input_chipset
 * File: drivers/acpi/driver_acpi_power.h
 * Purpose: ACPI power management - S3/S4/S5 sleep states, SCI + GPE
 *          event handling, and the FACS waking-vector resume path.
 */
#ifndef OC_DRIVER_ACPI_POWER_H
#define OC_DRIVER_ACPI_POWER_H

#include "types.h"

/* Initialize the power module (after driver_acpi_init + AML load):
 * resolves sleep types for S1..S5 from \_S1..\_S5, prepares the
 * wakeup machinery and registers the SCI (GPE dispatch) handler.
 * Returns 0 when the full power surface is available. */
int driver_acpi_power_init(void);

/* Sleep-state availability (from \_Sx packages; bit i = S(i+1)). */
u32 driver_acpi_power_sleep_mask(void);
/* Resolved SLP_TYP value for a state (3=S3, 4=S4, 5=S5), -1 if absent. */
int driver_acpi_power_slp_typ(int state);

/* Enter S5 (soft off). Returns only on failure (error code). */
int driver_acpi_power_enter_s5(void);
/* Enter S4 (hibernate-style sleep; QEMU treats it as a power cut -
 * resume requires the boot loader resume support, reported honestly). */
int driver_acpi_power_enter_s4(void);
/* Enter S3 (suspend to RAM). Saves the minimal CPU state, arms the
 * FACS waking vector and writes SLP_EN|SLP_TYP. Returns 0 after a
 * successful resume, non-zero on failure. */
int driver_acpi_power_enter_s3(void);

/* 1 after a successful S3 resume (checked by the resume path). */
int driver_acpi_power_resumed(void);

/* 3.0.0-WP03 G2: ACPI-side re-arm (\_WAK + PM1a power button) after a
 * live S3 resume.  The platform-generic re-arm is pwr_s3_rearm() in
 * services/power; the caller runs both, then sti. */
void driver_acpi_power_rearm_acpi(void);

/* GPE handling: status snapshot (gpe0 enabled/status bytes), and the
 * SCI IRQ number in use (-1 when not routed). */
int  driver_acpi_power_sci_irq(void);
void driver_acpi_power_gpe_status(u8 *out, int cap);

/* SCI dispatch (registered on the PIC line named by FADT sci_int).
 * Frame type is the kernel's arch_irq_frame_t (opaque here). */
struct arch_irq_frame;
void driver_acpi_sci_handler(void *ctx, struct arch_irq_frame *frame);

/* Event counters for the acpipow command. */
u32 driver_acpi_power_sci_count(void);
u32 driver_acpi_power_gpe_count(void);
u32 driver_acpi_power_pwrbtn_count(void);

#endif /* OC_DRIVER_ACPI_POWER_H */
