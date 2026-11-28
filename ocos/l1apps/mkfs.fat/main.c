/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP-compat-and-security-hardening - L1 app
 * "mkfs.fat" (the POSIX-conventional alias of mkfs.fat32).
 *
 * Same one-command-one-app contract as every l1apps entry: the ring-3
 * half of the `mkfs.fat` command; the service body lives in the L0
 * "block" service (L1SVC_BLK_MKFS_FAT32).  The kernel side accepts
 * both bare (hdb) and /dev/-prefixed (/dev/hdb) device tokens and
 * refuses writes to OCIRE-armed recovery disks with EPERM at the
 * block driver layer - the B2 acceptance case is literally
 *   mkfs.fat /dev/hdb
 * expecting "refused, sectors unchanged". */
#include "l1_api_syscall.h"
#include "l1_svc_ops.h"

int app_main(int argc, char **argv)
{ return l1svc_main(argc, argv, L1SVC_CAT_BLOCK, L1SVC_BLK_MKFS_FAT32); }
