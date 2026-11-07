/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP05-ocbl
 * File: ocbl/common/ocbl_part.h
 * Purpose: partition table traversal (MBR + GPT) for the OCBL scanner.
 *
 *          The scanner walks every BIOS drive (0x80.., the boot CD
 *          0xE0+) and turns each partition into a "volume window"
 *          (base LBA + size).  Filesystem detection happens one layer
 *          up (ocbl_fs).
 *
 *          SPECIAL CASE (WP-01/WP-02 declared design): the OCIRE
 *          recovery disk carries an MBR with ONE type-0xEE entry and
 *          NO GPT header - the entry IS the FAT12 shadow volume range.
 *          0xEE therefore does NOT mean "read the GPT" blindly: we
 *          look for the GPT header at LBA 1 first, and only fall back
 *          to treating the 0xEE entry as a plain partition when the
 *          header is absent (exact OCIRE shadow shape).
 */
#ifndef OCBL_PART_H
#define OCBL_PART_H

#include "ocbl_types.h"

#define OCBL_PART_MAX       32      /* per drive */
#define OCBL_PART_NAME_MAX  36      /* GPT UTF-16 name, ASCII collapsed */

typedef enum {
    OCBL_PT_NONE = 0,       /* no partition table (whole-disk fs?)  */
    OCBL_PT_MBR,
    OCBL_PT_GPT,
    OCBL_PT_SHADOW,         /* MBR 0xEE without GPT header (OCIRE)  */
} ocbl_parttype_t;

typedef struct ocbl_partition {
    u64  first_lba;
    u64  num_sectors;
    u32  mbr_type;          /* 0 for pure GPT entries */
    u8   gpt_type[16];      /* GUID, all-zero for MBR entries */
    char name[OCBL_PART_NAME_MAX];  /* GPT label or "" */
    bool is_esp;            /* GPT ESP / MBR 0xEF */
    u8   index;             /* 1-based slot (MBR table slot or GPT
                             * entry ordinal) - label naming uses it */
} ocbl_partition_t;

typedef struct ocbl_part_table {
    u8   drive;
    u8   kind;              /* ocbl_parttype_t */
    u32  count;
    ocbl_partition_t parts[OCBL_PART_MAX];
} ocbl_part_table_t;

/* Read and decode the partition table on `drive` (sector size `ssz`).
 * Returns true when a table was decoded (kind != NONE); false means
 * "no 0xAA55, no GPT" - the caller may still try a whole-disk fs. */
bool ocbl_part_scan(u8 drive, u32 ssz, ocbl_part_table_t* out);

/* GPT type GUID helpers exposed for tests/diagnostics. */
extern const u8 OCBL_GPT_ESP[16];
extern const u8 OCBL_GPT_LINUXFS[16];
extern const u8 OCBL_GPT_BASIC[16];

#endif /* OCBL_PART_H */
