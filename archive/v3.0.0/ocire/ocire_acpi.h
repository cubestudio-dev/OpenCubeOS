/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP-01-ocire-core
 * File: ocire/ocire_acpi.h
 * Purpose: Minimal ACPI for the OCIRE recovery kernel: locate the FADT,
 *          resolve the \_S5 sleep type from the DSDT, and provide real
 *          shutdown (S5) / reboot (RESET_REG + 8042) paths.
 */

#ifndef OCIRE_ACPI_H
#define OCIRE_ACPI_H

#include "ocire_types.h"

/* Locate RSDP -> XSDT -> FADT -> DSDT and resolve \_S5.
 * Returns 0 when the FADT was found (SLP_TYP may still be missing),
 * -1 when there is no usable ACPI at all. */
int  ocire_acpi_init(void);

int  ocire_acpi_available(void);       /* FADT found */
int  ocire_acpi_slp_typ_s5(void);     /* SLP_TYPa or -1 */

/* Enter S5 through PM1a_CNT.  Returns 0 = never (power off happened);
 * -1 = the port path was not available (caller falls back). */
int  ocire_acpi_enter_s5(void);

/* Reset through the FADT RESET_REG (I/O space only).  Returns -1 when
 * the platform provides no ACPI reset register. */
int  ocire_acpi_reset(void);

#endif /* OCIRE_ACPI_H */
