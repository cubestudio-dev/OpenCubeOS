/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-07
 * File: kernel/part.h
 * Purpose: Partition table parser — MBR and GPT.
 *
 * L1 extension interface (item 29): driver_block_part_parse_mbr / driver_block_part_parse_gpt
 * L1 extension interface (item 30): driver_block_part_get_partitions
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
} driver_block_part_table_type_t;

typedef struct {
    u8  present;
    u8  bootable;
    u8  type;               /* MBR partition type byte, or GPT type GUID[0] */
    u64 start_lba;
    u64 sectors;
    char name[PART_NAME_LEN]; /* GPT name or "MBR-N" */
    /* WP-10d-pre: full 16-byte GPT partition type GUID (0 when MBR).
     * grub-install needs it to find the BIOS boot partition on GPT
     * disks (GUID 21686148-6449-6E6F-744E-656564454649). */
    u8  gpt_type_guid[16];
} partition_t;

typedef struct {
    int              dev_idx;
    driver_block_part_table_type_t table_type;
    int              count;
    partition_t      parts[PART_MAX_PARTITIONS];
} driver_block_part_table_t;

/* Parse the partition table of a block device. Returns 0 on success.
 * Reads sector 0 (MBR) and, if the MBR has a GPT protective entry,
 * reads the GPT header + entries. */
int driver_block_part_parse(int dev_idx, driver_block_part_table_t *out);

/* Convenience wrappers. */
int driver_block_part_parse_mbr(int dev_idx, driver_block_part_table_t *out);
int driver_block_part_parse_gpt(int dev_idx, driver_block_part_table_t *out);

/* Get a partition by index (0-based). Returns NULL if out of range. */
partition_t *driver_block_part_get_partition(driver_block_part_table_t *tbl, int index);

/* Print the partition table (for `parted` command). */
void driver_block_part_print(const driver_block_part_table_t *tbl);

/* WP-10d-pre: register one partition of a disk as its own block device
 * named "<parent>pN" (e.g. "hda" + 2 -> "hdap2").  The child device
 * forwards reads/writes to the parent with a start-LBA offset.
 * Returns the new block-device index (>= 0) or the existing index when
 * the name is already registered, -1 on error.  Used by the A/B update
 * framework (ota_ab_update.c) and by the in-system abdisk / install
 * commands (disk_setup.c). */
int driver_block_part_register_child(const char *parent_name, int parent_idx,
                        int driver_block_part_no, u64 start_lba, u64 sectors);

/* WP-10d-pre: write an MBR partition table to a disk (sector 0).
 * `entries` holds up to 4 rows: {type, start_lba, sectors}; type 0 =
 * empty slot.  Boot flags are 0 (GRUB manages booting); CHS fields use
 * the standard 0xFEFFFF filler.  Returns 0 on success. */
int driver_block_part_write_mbr_table(int dev_idx,
                         const u8 types[4], const u32 starts[4],
                         const u32 sectors4[4]);

/* WP-10d-pre: scan every block device's partition table (MBR or GPT)
 * and register each partition as a "<parent>pN" child block device.
 * Runs once at boot, BEFORE the A/B disk scan, so partitions of any
 * layout (single-system install disks included) are addressable and
 * mountable right after a reboot - matching the mainstream behaviour
 * of operating systems that expose sda1/sda2/... automatically.
 * Idempotent: already-registered names are left untouched. */
void driver_block_part_scan_register_all(void);

#endif /* OC_PART_H */
