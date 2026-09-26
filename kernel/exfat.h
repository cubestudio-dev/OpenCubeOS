/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-07
 * File: kernel/exfat.h
 * Purpose: exFAT filesystem driver — read/write support via VFS.
 */
#ifndef OC_EXFAT_H
#define OC_EXFAT_H

#include "types.h"

/* Register the exFAT driver with the VFS. */
void exfat_init(void);

#endif /* OC_EXFAT_H */
