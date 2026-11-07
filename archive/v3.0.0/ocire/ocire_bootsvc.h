/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP02-ocire_recovery
 * File: ocire/ocire_bootsvc.h
 * Purpose: OCIRE boot supervisor entry (see ocire_bootsvc.c).
 */

#ifndef OCIRE_BOOTSVC_H
#define OCIRE_BOOTSVC_H

/* Run the supervisor decision loop (ocire.mode=sup).  Never returns
 * on the happy path (the machine belongs to OCOS); every failure
 * path returns and the caller falls back to the full recovery shell. */
void ocire_supervisor_run(void);

#endif /* OCIRE_BOOTSVC_H */
