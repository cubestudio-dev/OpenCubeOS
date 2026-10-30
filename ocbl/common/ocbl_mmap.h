/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP05-ocbl */
#ifndef OCBL_MMAP_H
#define OCBL_MMAP_H

#include "ocbl_types.h"

typedef struct ocbl_mmap_entry {
    u64 addr;
    u64 len;
    u32 type;              /* E820: 1 usable, 2 reserved, 3 ACPI NVS,
                            * 4 ACPI reclaimable, 5 defective */
    u32 reserved;
} OCBL_PACKED ocbl_mmap_entry_t;

#define OCBL_MMAP_MAX       128
#define OCBL_MMAP_BUF       0x60000       /* fixed low-memory buffer  */
#define OCBL_MMAP_SCRATCH   0x61000       /* E820 entry scratch - MUST
                                           * stay outside OCBL_MMAP_BUF
                                           * (the entries array starts
                                           * at BUF+8, exactly where a
                                           * naive scratch would land -
                                           * live-caught in B2a) */

typedef struct ocbl_mmap {
    u32 count;
    ocbl_mmap_entry_t entries[OCBL_MMAP_MAX];
} ocbl_mmap_t;

/* Fill the map via int 15h E820.  Returns the entry count (0 on
 * complete failure - pre-E820 machines get the fallback path). */
u32 ocbl_mmap_query(void);

/* The map (fixed low-memory block, valid after ocbl_mmap_query). */
const ocbl_mmap_t* ocbl_mmap_get(void);

/* Convert the map to a multiboot2-compatible entry array in place
 * (u64 addr, u64 len, u32 type, u32 reserved - already the mb2 shape,
 * so this returns a pointer for direct tag emission in B5). */

#endif /* OCBL_MMAP_H */
