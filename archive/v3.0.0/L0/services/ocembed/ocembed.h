/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS 3.0.0-WP03
 * File: services/ocembed/ocembed.h
 * Purpose: the OCOS side of the OCIRE embed - the service kernel's
 *          recovery-supervision channel.
 *
 * When the OCIRE supervisor (the independent recovery kernel) hands
 * the machine to OCOS via kexec, it passes the boot facts on the
 * kernel command line:
 *     oc.log=<LBA>   absolute LBA of the /LOG/BOOT.LOG ring header
 *     oc.logsecs=<N> ring size in sectors (incl. the header sector)
 *     oc.mb=<LBA>    absolute LBA of the /MAILBOX.BIN first sector
 *     oc.wdt=ib700   the supervisor armed an i6300esb watchdog
 *     oc.agent=1     the log agent was injected (mb2 "ocire-agent")
 * and attaches the agent as a multiboot2 module.  This service
 * closes the loop that WP-02 declared open:
 *   - boot confirm: mailbox WAIT_BOOT_SIGNAL -> SIGNAL_OK (+boot_count)
 *     and the i6300esb watchdog DISARMED, the moment L0 is up
 *   - process protection: the agent runs as "ocire_guard" (kill is
 *     refused by sys_kill; layer 2 is its binary living on the
 *     OCIRE volume where OCOS cannot touch it)
 *   - the L1SVC_CAT_OCIRE channel (guard-only LOG_APPEND +
 *     KLOG_SNAPSHOT) - the agent's ONLY two doors into the kernel
 *   - whole-disk hard protection: every write through a block device
 *     covering the mailbox is REFUSED (disk_rw_test hda fails)
 * On a normal GRUB boot no fact is present, the service stays
 * dormant, and nothing above activates.
 */
#ifndef OC_OCEMBED_H
#define OC_OCEMBED_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Parse the oc.* facts from the boot command line (idempotent, cheap)
 * and capture the injected agent module.  Call EARLY in kmain, right
 * after the multiboot2 info was parsed and the console is up: the
 * facts only toggle this service; nothing else reads them. */
void ocembed_early_init(const char *cmdline);

/* 1 = this boot is OCIRE-supervised (the mailbox fact was present). */
int  ocembed_active(void);

/* The boot-confirmation step, called once the whole L0 is up (right
 * before "L0 ready"): writes SIGNAL_OK into the mailbox, disarms the
 * i6300esb watchdog, arms the whole-disk write protection and spawns
 * the injected agent as the protected "ocire_guard" process.  No-op
 * (and silent) on a normal boot. */
void ocembed_boot_confirm(void);

/* Status line for the boot log / diagnostics (also honest when the
 * embed is inactive).  Renders into out[cap]; returns out. */
const char *ocembed_status(char *out, int cap);

/* The L1SVC_CAT_OCIRE dispatch body (registered through the service
 * declaration below; guard-checked inside). */
int  ocembed_l1_dispatch(u32 op, const char *arg, char *out, int cap);

#ifdef __cplusplus
}
#endif

#endif /* OC_OCEMBED_H */
