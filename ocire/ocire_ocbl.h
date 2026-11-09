/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP07a-ocbl_state
 * File: ocire/ocire_ocbl.h
 * Purpose: the OCIRE side of the WP07a boot-state monitor - the
 *          reserved-area reader interface (see include/ocbl_state.h
 *          for the shared contract).
 *
 * OCBL writes a boot record into the fixed reserved area at 0x9B000
 * either when it judges a failed boot (BOOT_FAILED: the kernel hung
 * with the watchdog relay armed) or when the user picks the OCIRE
 * menu entry (USER_REQUEST).  OCIRE reads it right after the console
 * comes up and prints the failure point + the repair advice, or the
 * user-request notice.  The block survives warm resets (watchdog and
 * 8042 pulses preserve DRAM); consumers must always validate the
 * magic + checksum, and a cold boot simply leaves no record.
 */
#ifndef OCIRE_OCBL_H
#define OCIRE_OCBL_H

#include "ocire_types.h"

/* One-line status for callers: 1 = a valid record was read and
 * reported this call, 0 = no valid record (clean/normal boot). */
int ocire_ocbl_report(void);

#endif /* OCIRE_OCBL_H */
