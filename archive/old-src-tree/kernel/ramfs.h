/* SPDX-License-Identifier: Apache-2.0 */
/* Open Cube OS - WP-05/WP-07
 * File: kernel/ramfs.h
 * Purpose: In-memory file system (ramfs) - registers with VFS at "/".
 */
#ifndef OC_RAMFS_H
#define OC_RAMFS_H

#include "types.h"

/* Initialize ramfs: register the "ramfs" fs type with VFS and mount it at "/",
 * creating a small default directory tree (/etc/, /tmp/, /dev/). */
void ramfs_init(void);

/* Return aggregate stats: total node count and total bytes stored. */
void ramfs_get_stats(int *total_nodes, int *total_size);

#endif /* OC_RAMFS_H */
