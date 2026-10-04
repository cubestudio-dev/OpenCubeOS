/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10u
 * File: kernel/ota_ab_update.h
 * Purpose: A/B slot framework + in-system update (Windows-Update-style).
 *
 * Slot layout (MBR disk, the A/B update disk):
 *
 *     +--------------------+
 *     | p1 boot/flags 32MB |  FAT32: /boot/next_B, /boot/ok_B,
 *     |                    |         /boot/bootfail_B, /boot/grub/grub.cfg
 *     +--------------------+
 *     | p2 slot A     128M |  FAT32: /boot/opencube.elf, /etc, /docs, ...
 *     +--------------------+
 *     | p3 slot B     128M |  FAT32: same structure as slot A
 *     +--------------------+
 *     | p4 /data     rest  |  FAT32: user data, downloaded packages
 *     +--------------------+
 *
 * Boot selection (grub.cfg, both CD and disk):
 *   - /boot/next_B present AND /boot/bootfail_B absent  -> boot slot B
 *   - /boot/next_B present AND /boot/bootfail_B present -> ROLLBACK to A
 *     (the B kernel writes bootfail_B pessimistically at boot and removes
 *      it when the system is fully up; so bootfail_B means "B was tried
 *      and never came up")
 *   - /boot/ok_B present (B confirmed)                  -> boot slot B
 *   - otherwise                                          -> boot slot A
 *
 * The current slot reaches the kernel through the multiboot2 command
 * line ("oc.slot=A" / "oc.slot=B" / "oc.slot=ISO", set by grub.cfg).
 *
 * Update flow (oc> update):
 *   1. check update.json (version + package_url + package_sha256 +
 *      package_size) via the WP-09-fix5 checkupdate transport;
 *   2. download the package (HTTP or HTTPS, streamed to /data);
 *   3. verify the SHA256;
 *   4. gunzip (RFC 1952/1951) + untar (ustar) into slot B;
 *   5. write boot flags for slot B, print "reboot required".
 * Rollback (oc> rollback) clears the flags so the boot goes back to A.
 *
 * All error codes are negative; ota_update_strerror() renders them.
 */
#ifndef OC_AB_UPDATE_H
#define OC_AB_UPDATE_H

#include "types.h"
#include "ota_update.h"

/* L1-facing alias (kernel/ext.h uses the l1_ext_update_* names). */
typedef ota_update_pkg_info_t l1_ext_update_pkg_info_t;

#ifdef __cplusplus
extern "C" {
#endif

/* Slot ids. */
#define OC_AB_SLOT_ISO  0     /* no A/B disk (CD-only boot) */
#define OC_AB_SLOT_A    1
#define OC_AB_SLOT_B    2

/* Mount points created by ota_ab_init(). */
#define OC_AB_MOUNT_BOOT  "/ab/boot"
#define OC_AB_MOUNT_A     "/ab/a"
#define OC_AB_MOUNT_B     "/ab/b"
#define OC_AB_MOUNT_DATA  "/data"

/* Flag file names on the boot/flags partition. */
#define OC_AB_FLAG_NEXT_B      "/boot/next_B"
#define OC_AB_FLAG_OK_B        "/boot/ok_B"
#define OC_AB_FLAG_BOOTFAIL_B  "/boot/bootfail_B"

/* Default location of a downloaded update package. */
#define OC_UPDATE_PKG_PATH  "/data/update-pkg.tar.gz"
#define OC_UPDATE_PKG_TMP   "/tmp/update-pkg.tar.gz"

/* A/B state snapshot (status command + L1 extension). */
typedef struct {
    char current_version[32];   /* OC_UPDATE_CURRENT_VERSION            */
    char available_version[32]; /* last check: server version, else ""  */
    char current_boot[8];       /* "A" / "B" / "ISO"                    */
    char next_boot[8];          /* "A" / "B" / "" (boot flags)          */
    int  online_update;         /* config online_update: 1 yes / 0 no   */
    int  ota_update_available;      /* 0 up to date, 1 new, -1 unknown      */
    int  ota_ab_present;            /* 1 when the A/B disk was found        */
} ota_update_status_t;

/* ---- lifecycle (called from kmain) ---------------------------------- */

/* Scan block devices for the A/B layout, register the partition block
 * devices ("<parent>p1".."p4"), mount the four slots, and read the
 * boot slot from the multiboot2 command line (oc.slot=...).
 * Returns 0 when the A/B disk was found, 1 when absent (no error),
 * negative on a hard failure. */
int ota_ab_init(void);

/* WP-10d-pre: re-run the A/B disk discovery at runtime (after the
 * in-system abdisk command created the layout).  Best-effort umount of
 * the ab mounts, then a fresh discovery + mount. */
int ota_ab_rescan(void);

/* Parse the boot slot out of the multiboot2 command line ("oc.slot=A").
 * Call before ota_ab_init(); unknown/absent -> OC_AB_SLOT_ISO. */
void ota_ab_set_boot_slot_arg(const char *cmdline);

/* Pessimistic failure marker for slot B: called EARLY in kmain while
 * slot == B.  Creates /boot/bootfail_B on the flags partition (if the
 * flags partition is available).  The boot is only "confirmed" once
 * ota_update_confirm_boot() runs with the system fully up. */
void ota_ab_boot_early(void);

/* Confirm a successful boot (called at the end of kmain): for slot B
 * remove bootfail_B + next_B and write ok_B (B becomes the default). */
void ota_update_confirm_boot(void);

/* ---- slot / flags queries ------------------------------------------- */

/* Current boot slot (from the command line). */
int ota_ab_current_slot(void);

/* "A" / "B" / "ISO" for a slot id. */
const char *ota_ab_slot_name(int slot);

/* Slot filesystem mount point ("/ab/a", "/ab/b") or "" for ISO. */
const char *ota_ab_slot_path(int slot);

/* Parent block device name of the A/B disk ("hda", "hdb", ...) or ""
 * when no A/B disk was found.  Partition devices are "<name>p1..p4". */
const char *ota_ab_disk_name(void);

/* 1 when the A/B disk was found and its flags partition is mounted. */
int ota_ab_flags_ready(void);

/* Read the boot flags: *next_boot / *ok_b / *bootfail_b are set to 1/0.
 * Returns 0 on success (flags readable), negative when unavailable. */
int ota_ab_read_flags(int *next_boot, int *ok_b, int *bootfail_b);

/* ---- update core (also the L1 extension surface, see ext.h) --------- */

/* Set the next boot slot ("A" or "B").  For B: create next_B, clear
 * ok_B + bootfail_B.  For A: clear all three (GRUB falls back to A).
 * Returns 0 on success, negative on error. */
int ota_update_set_boot(const char *slot);

/* Rollback to slot A: clear the boot flags so the next boot uses A.
 * Returns 0 on success, negative on error. */
int ota_update_rollback(void);

/* Fill *out with the current update state.  Returns 0 on success. */
int ota_update_get_status(ota_update_status_t *out);

/* Download url to a VFS path (HTTP or HTTPS, streamed, heap buffers
 * only).  Returns the byte count written (>=0) or a negative error. */
int ota_update_download(const char *url, const char *path);

/* Stream a file through SHA256 and compare with a hex digest
 * (case-insensitive).  Returns 0 on a match, negative on error. */
int ota_update_verify(const char *path, const char *crypto_sha256_hex);

/* Install a package (tar.gz) into a slot ("A" or "B"): gunzip + untar
 * to the slot mount, map "kernel/opencube.elf" -> /boot/opencube.elf,
 * mirror boot/grub.cfg to the flags partition, verify the manifest
 * checksum.  Returns 0 on success, negative on error. */
int ota_update_install(const char *pkg_path, const char *slot);

/* Human-readable text for ota_update_* / ota_update_check_* return codes. */
const char *ota_update_strerror(int rc);

/* ---- shell commands (registered in kmain.c) -------------------------- */

int shell_cmd_update(const char *args);     /* update [local <path> | --local <path> | --status] */
int shell_cmd_rollback(const char *args);   /* rollback            */
int shell_cmd_reboot(const char *args);     /* reboot              */

#ifdef __cplusplus
}
#endif

#endif /* OC_AB_UPDATE_H */
