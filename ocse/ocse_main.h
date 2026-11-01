/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP06a-ocse
 * File: ocse/ocse_main.h
 * Purpose: OCSE environment entry (assembled by kernel/main.c stage 24).
 */
#ifndef OCSE_MAIN_H
#define OCSE_MAIN_H

#include "types.h"
#include "arch_multiboot2.h"

/* Environment main loop: hardware checks, disk identification and the
 * GUI/CLI installer front-end. Never returns. */
void ocse_main_run(const arch_multiboot2_info_t *mbi);

#endif /* OCSE_MAIN_H */
