/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP06b-ocsecl
 * File: ocse/ocse_iso.h
 * Purpose: read-only ISO9660 filesystem for the VFS - mounts the
 * OCSE install medium (the El Torito ISO the kernel was booted from)
 * so `tar -xf /install/ocos.tar` and `ocbl-install` can read their
 * payloads through the normal VFS path.
 *
 * Plain ISO9660 as produced by the project's xorriso line (level 1,
 * 8.3 uppercase names, no Joliet/Rock Ridge reliance): the Primary
 * Volume Descriptor sits at 2048-sector 16; directory records are
 * walked exactly like ocbl/common/ocbl_fs.c does (which has read
 * these very ISOs since WP-05). ";1" version suffixes are stripped,
 * matching is case-insensitive (every name on the medium is
 * uppercase; users type lowercase).
 *
 * Registered as fs type "iso9660" (case-insensitive name matching,
 * like FAT32). Every mutating op is explicitly refused - the medium
 * is read-only by nature.
 */
#ifndef OCSE_ISO_H
#define OCSE_ISO_H

#include "types.h"

/* Register the "iso9660" fs type with the VFS. Call once at boot,
 * before the first mount("iso9660", ...). */
void ocse_iso_init(void);

#endif /* OCSE_ISO_H */
