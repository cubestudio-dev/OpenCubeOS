/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP05-ocbl
 * File: ocbl/common/ocbl_scan.h
 * Purpose: the boot-entry scanner: walks every BIOS drive + the boot
 *          CD, decodes partitions, mounts filesystems and produces
 *          the fixed-size entry table the TUI menu (B3) renders.
 *
 * DETECTION MATRIX (WP-05 spec, six classes):
 *   OCOS     - any volume with /boot/opencube.elf whose header carries
 *              the multiboot2 magic; the A/B flag protocol
 *              (/boot/next_B, /boot/bootfail_B, /boot/ok_B on the
 *              first system volume of a disk) picks the slot, same
 *              semantics GRUB implemented, now native.
 *   OCIRE    - the 0xEE shadow volume (or any volume) with
 *              /boot/ocire.elf.
 *   OCSE     - an ISO9660 volume carrying /ocse.marker (the WP-06
 *              install-media contract; the marker name is ours).
 *   WINDOWS  - a volume with /EFI/Microsoft/Boot/bootmgfw.efi.
 *   LINUX    - an ESP with a foreign EFI loader
 *              (grubx64.efi / shimx64.efi / systemd-bootx64.efi under
 *              /EFI/<vendor>/); the vendor dir names the entry.
 *   GENERIC  - an ESP with /EFI/BOOT/BOOTX64.EFI matching none of the
 *              above (removable media, EFI shells, other OSes).
 *   SHUTDOWN - synthesized, always last (B3 menu).
 *
 * KNOWN LIMIT (declared): no ext2/3/4 driver in 3.0.0 - Linux systems
 * are detected through their ESP loaders, never through /boot on an
 * ext root.  Same rule Windows follows (NTFS not parsed; detection is
 * ESP-based, booting is chainload-based).
 */
#ifndef OCBL_SCAN_H
#define OCBL_SCAN_H

#include "ocbl_types.h"
#include "ocbl_fs.h"

typedef struct ocbl_entry {
    u8  type;                   /* ocbl_ent_type_t */
    char label[OCBL_LABEL_MAX];
    /* payload location (B4 loader consumes; chain entries use efi_path) */
    u8  drive;
    u32 ssz;
    u64 base_lba;               /* volume base, drive sectors */
    u8  fs;                     /* ocbl_voltype_t */
    char path[64];              /* kernel path for mb2 loads */
    u32 file_size;
    /* EFI chainload path (WINDOWS/LINUX/GENERIC), volume-relative */
    char efi_path[96];
    /* OCOS slot state: 0 = plain, 1 = slot A chosen, 2 = slot B */
    u8  slot;
} ocbl_entry_t;

typedef struct ocbl_scan {
    u32 count;                  /* entries found (SHUTDOWN not included) */
    ocbl_entry_t entries[OCBL_MAX_ENTRIES];
} ocbl_scan_t;

/* The global scan result (fixed low-mem budget lives in .bss). */
ocbl_scan_t* ocbl_scan_result(void);

/* Run the full scan (all BIOS drives + boot CD).  Returns the entry
 * count.  Idempotent: re-running rebuilds the table from scratch. */
u32 ocbl_scan_all(void);

/* Debug dump over serial (B2b bring-up; the menu replaces it in B3). */
void ocbl_scan_dump(void);

#endif /* OCBL_SCAN_H */
