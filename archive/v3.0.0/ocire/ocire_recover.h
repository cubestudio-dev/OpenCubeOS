/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP02-ocire_recovery
 * File: ocire/ocire_recover.h
 * Purpose: the WP-02 full recovery shell commands (see ocire_recover.c).
 */

#ifndef OCIRE_RECOVER_H
#define OCIRE_RECOVER_H

/* logread: dump the boot-log ring (header facts + recorded text). */
void ocire_cmd_logread(void);

/* syscheck: ten-point integrity self-check of the whole recovery
 * layout (disks, volumes, mailbox, log ring, agent digest, golden
 * manifest, golden files, system files, kernel copy). */
void ocire_cmd_syscheck(void);

/* repair quick: fix metadata (MBRs, mailbox, log header) and restore
 * any system file whose digest differs from the golden manifest,
 * without touching anything else. */
void ocire_cmd_repair_quick(void);

/* repair full: DESTRUCTIVE reformat of the OCOS system volume + full
 * rewrite of every system file from the golden image (asks for a
 * typed YES first). */
void ocire_cmd_repair_full(void);

/* return: mark a normal boot (mailbox NONE) and reboot into
 * OpenCubeOS through the supervisor path.  Never returns. */
void ocire_cmd_return(void);

#endif /* OCIRE_RECOVER_H */
