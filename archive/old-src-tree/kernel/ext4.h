/* SPDX-License-Identifier: Apache-2.0 */
/* Open Cube OS - WP-07
 * File: kernel/ext4.h
 * Purpose: ext4 filesystem driver — read-only support via VFS.
 */
#ifndef OC_EXT4_H
#define OC_EXT4_H

#include "types.h"

/* Register the ext4 driver with the VFS. */
void ext4_init(void);

#endif /* OC_EXT4_H */
