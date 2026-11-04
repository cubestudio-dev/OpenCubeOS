/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP07a-ocbl_state
 * File: ocbl/common/ocbl_statemon.h
 * Purpose: the OCBL side of the WP07a boot-state monitor - state
 *          area ownership, post-reset failure diagnosis, the
 *          reserved-area boot record and the 8042 reset pulse (see
 *          include/ocbl_state.h for the shared contract).
 *
 * Call order inside ocbl_run() / efi_main():
 *
 *   1. ocbl_statemon_boot_check(ok)  once, EARLY: `ok` is the
 *                                   monitor-window availability
 *                                   (BIOS: an E820 usable region
 *                                   covers the fixed pages - see
 *                                   ocbl_statemon_bios_window_ok;
 *                                   UEFI: the AllocatePages reserve
 *                                   succeeded).  On ok it diagnoses
 *                                   the previous boot from the state
 *                                   area; on a judged failure it
 *                                   writes the BOOT_FAILED record,
 *                                   shows the screen notice, dwells
 *                                   and pulses the 8042 reset - it
 *                                   does not return in that case.
 *                                   On !ok the whole monitor
 *                                   DISABLES for this boot (no low-
 *                                   memory writes at all) and the
 *                                   kernel learns it from the
 *                                   " oc.stmon=off" cmdline flag.
 *   2. ocbl_statemon_route_pending() after the scan, before the
 *                                   menu: a pending unrouted
 *                                   BOOT_FAILED record auto-boots
 *                                   OCIRE (the caller resolves the
 *                                   entry index).
 *   3. ocbl_statemon_user_request() when the user's menu pick is an
 *                                   OCIRE entry (writes USER_REQUEST).
 *   4. ocbl_statemon_probe_hangat(v) after mounting a monitored
 *                                   payload volume (the /HANGAT
 *                                   diagnostic hook, see below).
 *   5. ocbl_statemon_arm_payload()  just before the mb2 handoff of a
 *                                   monitored (OCOS) payload: marks
 *                                   the state area armed and hands
 *                                   the boot-phase watchdog budget
 *                                   to the relay.
 *
 * The /HANGAT diagnostic hook: when a one-byte file /HANGAT
 * containing '1'..'6' exists on the payload volume, OCBL appends
 * "oc.hang=N" to the kernel command line and the kernel deliberately
 * halts right after recording stage N (with the phase watchdog armed,
 * so the real watchdog -> reset -> diagnosis chain fires).  '7' halts
 * after L1_READY with the runtime heartbeat running - the
 * runtime-hang scenario.  The hook exists so the shipped images can
 * be failure-tested without test-built kernels; with no file present
 * it is a complete no-op.
 */
#ifndef OCBL_STATEMON_H
#define OCBL_STATEMON_H

#include "ocbl_types.h"
#include "ocbl_fs.h"

/* Early boot diagnosis (see above).  `window_ok` = the fixed
 * monitor pages are free for us this boot.  Never returns on the
 * judged failure path (screen notice + dwell + 8042 pulse). */
void ocbl_statemon_boot_check(int window_ok);

/* BIOS flavor helper: 1 = an E820 usable region covers both monitor
 * pages (the cached mmap must already be queried). */
int ocbl_statemon_bios_window_ok(void);

/* 1 = a valid BOOT_FAILED record is pending and unrouted: the caller
 * must auto-boot the OCIRE entry.  Marks the record routed (one-shot
 * auto-recovery - the next boot goes back to the normal menu). */
int ocbl_statemon_route_pending(void);

/* Record a USER_REQUEST boot record (the user picked OCIRE). */
void ocbl_statemon_user_request(void);

/* Arm the state area + the boot-phase watchdog budget for a
 * monitored payload handoff. */
void ocbl_statemon_arm_payload(void);

/* Read /HANGAT from the payload volume ('1'..'7' or absent). */
void ocbl_statemon_probe_hangat(const ocbl_volume_t* v);

/* The extra kernel command line (" oc.hang=N" or ""). */
const char* ocbl_statemon_cmdline_extra(void);

#endif /* OCBL_STATEMON_H */
