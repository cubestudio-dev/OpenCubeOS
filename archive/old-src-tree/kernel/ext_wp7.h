/* SPDX-License-Identifier: Apache-2.0 */
/* Open Cube OS - WP-07
 * File: kernel/ext_wp7.h
 * Purpose: WP-07 extension API summary. Re-exports the WP-07 extension
 *          functions so L1 has one place to look.
 *
 * WP-07 adds SIX new extension points (on top of WP-01..WP-06's 26):
 *
 *  27. Block device register/unregister    - blk.h
 *  28. Block device read/write sectors     - blk.h
 *  29. Partition table parse MBR/GPT       - part.h
 *  30. Partition table query               - part.h
 *  31. Disk cache statistics               - blk_cache.h
 *  32. Disk cache flush                    - blk_cache.h
 *
 * All WP-01..WP-06 interfaces remain unchanged.
 */
#ifndef OC_EXT_WP7_H
#define OC_EXT_WP7_H

#include "blk.h"
#include "blk_cache.h"
#include "part.h"

#endif /* OC_EXT_WP7_H */
