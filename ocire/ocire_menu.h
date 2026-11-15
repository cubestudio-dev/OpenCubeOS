/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP07-ocire-loop-perfect
 * File: ocire/ocire_menu.h
 * Purpose: the WP-07 takeover menus (see ocire_menu.c).
 *
 *          Called from ocire_main after the boot-record report: a
 *          valid BOOT_FAILED (consecutive-failure routed) or
 *          WAKE_FAILED (SLEEP_ARMED residue) record raises the
 *          interactive [R]/[C]/[W] menu, which blocks until the
 *          user decides.  Any other boot falls straight through.
 */
#ifndef OCIRE_MENU_H
#define OCIRE_MENU_H

void ocire_menu_takeover(void);

#endif /* OCIRE_MENU_H */
