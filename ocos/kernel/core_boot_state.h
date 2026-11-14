/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP07a-ocbl_state
 * File: src/L0/boot/boot_state.h
 * Purpose: the OCOS side of the WP07a boot-state monitor (see
 *          include/ocbl_state.h for the shared contract and the
 *          watchdog-relay architecture).
 *
 *          The kernel is the MONITORED side: it BLIND-WRITES the
 *          boot stage / heartbeat / progress clock into the fixed
 *          state area (it never reads the block back - the
 *          write-only contract), re-arms the 6300ESB watchdog with
 *          the phase budget at every stage advance, and pets it from
 *          the 1 s tick once the system is up.  A hung kernel stops
 *          feeding the dog; the hardware resets the machine; OCBL
 *          running after the reset reads the state area, records
 *          BOOT_FAILED and routes the next boot into OCIRE.
 *
 * Call points (all wired by WP07a):
 *   boot_state_early_init(cmdline)  kmain, right after the mb2 parse
 *   boot_state_advance(stage)       kmain phase boundaries + the L1
 *                                   launcher (OCST_STAGE_* codes)
 *   boot_state_l1_pending()         the syscall gate (one load)
 *   boot_state_l1_syscall()         core_syscall_dispatch, the first
 *                                   syscall from the default L1 entry
 *                                   (ush): L1_READY + heartbeat +
 *                                   the L1-isolation CR3 walk
 *   boot_state_clean_reboot()       core_power_reboot, before the
 *                                   8042 pulse (the CLEAN marker)
 */
#ifndef OC_BOOT_STATE_H
#define OC_BOOT_STATE_H

#include "types.h"
#include "ocbl_state.h"        /* 3.0.0-WP07a: the shared OCST_* contract */

/* Parse the oc.* monitor facts (oc.hang=N diagnostic halt, the
 * oc.stmon=off disable flag), probe the 6300ESB watchdog and take it
 * over (fixing the WP-05 latent gap: OCBL used to arm a 60 s window
 * at handoff that nothing in the kernel ever fed). */
void boot_state_early_init(const char *cmdline);

/* Record a forward stage transition into the state area and re-arm
 * the phase watchdog budget.  Out-of-order calls are ignored (the
 * stage only ever moves forward).  The /HANGAT diagnostic halt fires
 * from here, AFTER the stage write + the arm, so the failure chain
 * tests the real watchdog relay. */
void boot_state_advance(u32 stage);

/* 1 until the default L1 entry's first syscall marks L1_READY - the
 * per-syscall gate stays a single global load. */
int  boot_state_l1_pending(void);

/* The L1_READY transition (first syscall from the default entry):
 * stage write, runtime heartbeat start, isolation self-check. */
void boot_state_l1_syscall(void);

/* Mark the state area CLEAN just before a deliberate reboot so OCBL
 * does not judge the next boot a failure. */
void boot_state_clean_reboot(void);

#endif /* OC_BOOT_STATE_H */
