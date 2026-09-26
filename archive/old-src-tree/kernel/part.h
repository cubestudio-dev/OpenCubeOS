/* SPDX-License-Identifier: Apache-2.0 */
/* Open Cube OS - WP-07
 * File: kernel/part.h
 * Purpose: Partition table parser — MBR and GPT.
 *
 * L1 extension interface (item 29): part_parse_mbr / part_parse_gpt
 * L1 extension interface (item 30): part_get_partitions
 */
#ifndef OC_PART_H
#define OC_PART_H

#include "types.h"

#define PART_MAX_PARTITIONS  16
#define PART_NAME_LEN        40

typedef enum {
    PART_TYPE_EMPTY = 0,
    PART_TYPE_MBR   = 1,
    PART_TYPE_GPT   = 2,
} part_table_type_t;

typedef struct {
    u8  present;
    u8  bootable;
    u8  type;               /* MBR partition type byte, or GPT type GUID[0] */
    u64 start_lba;
    u64 sectors;
    char name[PART_NAME_LEN]; /* GPT name or "MBR-N" */
} partition_t;

typedef struct {
    int              dev_idx;
    part_table_type_t table_type;
    int              count;
    partition_t      parts[PART_MAX_PARTITIONS];
} part_table_t;

/* Parse the partition table of a block device. Returns 0 on success.
 * Reads sector 0 (MBR) and, if the MBR has a GPT protective entry,
 * reads the GPT header + entries. */
int part_parse(int dev_idx, part_table_t *out);

/* Convenience wrappers. */
int part_parse_mbr(int dev_idx, part_table_t *out);
int part_parse_gpt(int dev_idx, part_table_t *out);

/* Get a partition by index (0-based). Returns NULL if out of range. */
partition_t *part_get_partition(part_table_t *tbl, int index);

/* Print the partition table (for `parted` command). */
void part_print(const part_table_t *tbl);

#endif /* OC_PART_H */
