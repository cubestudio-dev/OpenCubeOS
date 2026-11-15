/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP07-ocire-loop-perfect
 * File: ocire/ocire_update.h
 * Purpose: the WP-07 shell commands (see ocire_update.c).
 */
#ifndef OCIRE_UPDATE_H
#define OCIRE_UPDATE_H

/* `slots` - dump the recovery-disk A/B map + flags. */
void ocire_cmd_slots(void);

/* `ls [slotA:|slotB:|slot:][path]` - list ESP (default) or slot
 * volumes; the FAT32 mount surface. */
void ocire_cmd_ls(const char *arg);

/* `update` - the strictly manual, Ed25519-verified, dual-slot
 * self-update.  Returns 0 on success. */
int ocire_cmd_update(void);

#endif /* OCIRE_UPDATE_H */
