/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP02-ocire_recovery
 * File: ocire/ocire_disk.h
 * Purpose: recovery-disk discovery + the boot mailbox for OCIRE.
 *
 * WP-02 disk layout (produced by tools/make_ocire_disk.sh):
 *
 *   hd0 - the GRUB dual-boot disk: MBR (type 0x0C FAT32 partition
 *         from LBA 2048) with GRUB in the MBR + post-MBR gap.  This
 *         is the OCOS system volume - the repair TARGET:
 *           /BOOT/OPENCUBE.ELF, /BOOT/OCIRE.ELF,
 *           /BOOT/GRUB/GRUB.CFG, /EFI/BOOT/BOOTX64.EFI
 *
 *   hd1 - the OCIRE recovery disk: MBR with ONE type-0xEE shadow
 *         entry (no GPT header behind it - the WP-01 isolation).
 *         The shadow partition holds a FAT12 volume:
 *           /BOOT/OCIRE.ELF    the boot kernel (GRUB loads it with a
 *                              whole-disk blocklist)
 *           /GOLDEN/...        the golden OCOS system image
 *                              (OPENCUBE.ELF, GRUB.CFG, BOOTX64.EFI,
 *                              MANIFEST.TXT with SHA-256 digests)
 *           /AGENT/AGENT.ELF   the ring-3 log agent injected at kexec
 *           /LOG/BOOT.LOG      fixed-size 64 KiB ring: sector 0 is a
 *                              header (magic, write cursor), the rest
 *                              is log data (the OCOS agent appends)
 *           /MAILBOX.BIN       the boot signal mailbox (below)
 *
 * Mailbox protocol (OCIRE <-> OCOS, fixed 128 bytes):
 *   magic   "OCIREMB2"
 *   state   0 NONE, 1 WAIT_BOOT_SIGNAL (supervisor armed), 2
 *           SIGNAL_OK (OCOS booted), 3 ENTER_FULL (reboot into the
 *           full recovery shell)
 *   The supervisor writes WAIT_BOOT_SIGNAL before handing the machine
 *   to OCOS; OCOS writes SIGNAL_OK (+boot_count) when its boot is
 *           confirmed.  A watchdog reset that lands the supervisor on
 *           a still-WAIT_BOOT_SIGNAL mailbox means "OCOS never came
 *           up" -> crash notice -> ENTER_FULL -> reboot -> full shell.
 *   log_start_lba / log_sectors tell the OCOS-side kernel channel
 *           where the log ring lives (absolute LBA) so OCOS needs no
 *           FAT12 code of its own; agent_sha256 lets OCOS refuse a
 *           tampered agent image (process-protection layer 2).
 */

#ifndef OCIRE_DISK_H
#define OCIRE_DISK_H

#include "ocire_types.h"
#include "ocire_fat.h"
#include "ocire_ata.h"

#define OCIRE_MAILBOX_MAGIC   "OCIREMB2"
#define OCIRE_MAILBOX_BYTES   128

#define OCIRE_MB_STATE_NONE        0
#define OCIRE_MB_STATE_WAIT_SIGNAL 1
#define OCIRE_MB_STATE_SIGNAL_OK   2
#define OCIRE_MB_STATE_ENTER_FULL  3

/* ---- WP-07: the A/B slot flags sector ---------------------------- */

#define OCIRE_AB_MAGIC      "OCIREAB1"
#define OCIRE_AB_FLAGS_LBA  1024u     /* recovery-disk LBA (MBR gap) */
#define OCIRE_AB_MAX_ATTEMPTS  2u     /* give up the try-slot after  */

#pragma pack(push, 1)
typedef struct {
    char magic[8];        /* OCIRE_AB_MAGIC                             */
    u8  active_slot;      /* 1 = A, 2 = B (last CONFIRMED good slot)    */
    u8  try_slot;         /* 0 = none; else the candidate being tested  */
    u8  attempts;         /* boot attempts already made on try_slot     */
    u8  flags;            /* OCIRE_AB_F_*                               */
    u16 boot_count;       /* successful OCIRE boots (informational)     */
    u16 rsvd;             /* 0                                          */
    u32 cksum;            /* ones-complement sum of the preceding 16
                           * bytes (u16 words 0..7, cksum skipped - #52
                           * discipline, mirrored by make_ocire_disk.sh
                           * and the OCBL writer)                       */
} ocire_abflags_t;        /* 24 bytes                                   */
#pragma pack(pop)

#define OCIRE_AB_F_DIRTY   0x01u   /* update in progress marker */

#pragma pack(push, 1)
typedef struct {
    char magic[8];
    u32  state;
    u32  boot_count;      /* successful OCOS boots seen by this disk */
    u32  fail_count;      /* watchdog timeouts seen by this disk */
    u32  log_start_lba;   /* absolute LBA of /LOG/BOOT.LOG data start */
    u32  log_sectors;     /* ring size in sectors (incl. header sect) */
    u8   agent_sha256[32];
    /* #52 (WP-fix-p2-p3-cleanup): carved from the old reserved[68].
     * ver=1 mailboxes carry a ones-complement checksum over every
     * field except the cksum word itself, so a power-cut torn write
     * (half new / half old fields passing the magic check) can no
     * longer smuggle an illegal state to the supervisor.  ver=0 =
     * legacy layout (pre-fix disks): accepted, state-range check
     * only.  Total size stays OCIRE_MAILBOX_BYTES (128). */
    u32  ver;
    u32  cksum;
    u8   reserved[60];
} ocire_mailbox_t;
#pragma pack(pop)

typedef struct {
    /* The OCOS volume (hd0 p1, FAT32) - repair target. */
    int              ocos_idx;      /* ata index, -1 = none */
    ocire_fat_vol_t  ocos_vol;
    u64              ocos_part_lba;
    u64              ocos_part_sectors;

    /* The OCIRE volume: the ACTIVE slot (FAT32 dual-slot disk, or
     * the legacy FAT12 0xEE shadow, or a single FAT32 slot). */
    int              ocire_idx;     /* ata index, -1 = none */
    ocire_fat_vol_t  ocire_vol;
    u64              ocire_part_lba;

    /* WP-07: the full dual-slot map of the recovery disk. */
    int              dual_slot;     /* 1 = p1/p2 A+B pair found */
    int              my_slot;       /* 1 = A, 2 = B, 0 = unknown */
    u64              slot_lba[3];   /* [1] = A part LBA, [2] = B */
    u64              slot_sectors[3];
    ocire_abflags_t  ab;            /* the flags sector (zeros when
                                     * absent/invalid) */
    int              ab_valid;

    /* The ESP (GPT EFI System Partition or MBR 0xEF/0x0C FAT32),
     * the update drop zone. */
    int              esp_idx;       /* ata index, -1 = none */
    ocire_fat_vol_t  esp_vol;
    u64              esp_part_lba;

    /* Located files (valid after ocire_disk_scan). */
    ocire_fat_file_t mb_file;       /* /MAILBOX.BIN */
    ocire_fat_file_t log_file;      /* /LOG/BOOT.LOG */
    ocire_mailbox_t  mb;
} ocire_layout_t;

/* Scan all ATA disks for the OCIRE + OCOS volumes.  Returns:
 *   0  both found (mailbox + log located)
 *  -1  no OCIRE volume
 *  -2  no OCOS volume
 *  -3  OCIRE volume without mailbox/log (malformed recovery disk)
 * The result is cached in a singleton; ocire_layout() returns it.
 * The WP-07 dual-slot map and the ESP are filled in along the way
 * (absent slots leave dual_slot=0 / esp_idx=-1).
 */
int ocire_disk_scan(void);
const ocire_layout_t *ocire_layout(void);

/* ---- WP-07: A/B slot flags access ---------------------------------
 * Read/write the flags sector on the recovery disk (LBA 1024).
 * ocire_ab_read validates magic + checksum; on mismatch the cached
 * copy is zeroed and ab_valid is cleared.  ocire_ab_write
 * recomputes the checksum and writes the sector back.  Returns 0
 * on success, negative on I/O / no-disk errors. */
int ocire_ab_read(ocire_layout_t *L);
int ocire_ab_write(ocire_layout_t *L);

/* Slot promotion: called once OCIRE is up and running from a
 * try-slot - confirms the candidate (active=try, try=0).  Returns
 * 0 when the flags were updated, 1 when nothing needed doing,
 * negative on error. */
int ocire_ab_confirm_boot(ocire_layout_t *L);

/* The OTHER slot number (1<->2); 0 when not on a dual-slot disk. */
int ocire_ab_other_slot(const ocire_layout_t *L);

/* Mount (or re-mount) one recovery slot by number (1/2).  Returns
 * 0 and leaves the volume in *vol, negative when the partition is
 * absent or not a FAT32 OCIRE slot. */
int ocire_slot_mount(ocire_layout_t *L, int slot, ocire_fat_vol_t *vol);

/* Read the mailbox from the recovery volume (into the cached copy).
 * Returns 0. */
int ocire_mailbox_read(void);

/* Write the cached mailbox back to the recovery volume (read-modify-
 * write of its data sector).  Returns 0. */
int ocire_mailbox_write(void);

/* The golden-image manifest entry (see /GOLDEN/MANIFEST.TXT written
 * by make_ocire_disk.sh): "SHA256HEX SIZE PATH" per line. */
#define OCIRE_GOLDEN_MAX 8

typedef struct {
    char sha_hex[65];
    u64  size;
    char path[32];        /* OCOS-volume relative, 8.3 upper */
} ocire_golden_ent_t;

typedef struct {
    int n;
    ocire_golden_ent_t e[OCIRE_GOLDEN_MAX];
} ocire_golden_t;

/* Parse /GOLDEN/MANIFEST.TXT from the recovery volume.  Returns 0. */
int ocire_golden_load(ocire_golden_t *g);

/* Read one golden file's bytes into buf (cap).  Returns the byte
 * count or negative. */
s64 ocire_golden_read(const char *path, u8 *buf, u64 cap);

#endif /* OCIRE_DISK_H */
