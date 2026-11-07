/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP-01-ocire-core
 * File: ocire/ocire_shell.h
 * Purpose: OCIRE minimal shell - exactly three commands: help,
 *          shutdown, reboot.  Recovery features arrive in WP-02;
 *          the OCOS<->OCIRE interface arrives in WP-03.
 */

#ifndef OCIRE_SHELL_H
#define OCIRE_SHELL_H

void ocire_shell_run(void);   /* never returns */

#endif /* OCIRE_SHELL_H */
