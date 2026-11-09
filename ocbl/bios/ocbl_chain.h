/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP05-ocbl */
#ifndef OCBL_CHAIN_H
#define OCBL_CHAIN_H

#include "ocbl_types.h"

/* Read the partition VBR and hand the machine over in real mode.
 * Returns only on failure (no signature / read error). */
int ocbl_chain_vbr(u8 drive, u64 first_lba, u32 ssz);

#endif /* OCBL_CHAIN_H */
