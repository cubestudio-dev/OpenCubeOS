/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-tui-input-encoding-hardening 4.5
 * File: ocbl/common/ocbl_idt.h
 * Purpose: the minimal long-mode IDT contract (see ocbl_idt.S/.c).
 */
#ifndef OCBL_IDT_H
#define OCBL_IDT_H

#include "ocbl_types.h"

/* Build the 256-gate table in the core's .bss and lidt it.
 * Call once, as the first action of ocbl_run() (both flavors). */
void ocbl_idt_setup(void);

#endif /* OCBL_IDT_H */
