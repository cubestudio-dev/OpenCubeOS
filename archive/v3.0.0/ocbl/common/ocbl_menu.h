/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP05-ocbl
 * File: ocbl/common/ocbl_menu.h
 * Purpose: the boot menu (B3): one line per scanned entry plus the
 *          fixed SHUTDOWN row, ALWAYS last.
 *
 *          Key contract (WP-05 spec): exactly THREE keys are live -
 *          Up, Down, Enter.  Every other key event (including any
 *          garbage on the serial line) is consumed and ignored.
 *
 *          Countdown: the default entry (first OCOS, else first
 *          entry) boots when the timer expires; Up/Down cancel the
 *          countdown and switch to manual mode.
 *
 *          Rendering: VGA 80x25 text (BIOS flavor) + serial ANSI
 *          mirror, so both the screendump harness and a raw serial
 *          console see the same menu.
 */
#ifndef OCBL_MENU_H
#define OCBL_MENU_H

#include "ocbl_types.h"

#define OCBL_MENU_TIMEOUT_SEC  10

/* Runs the menu (full-screen render + input loop).  Returns the
 * chosen entry index into ocbl_scan_result()->entries, or
 * OCBL_MENU_SHUTDOWN (= scan count) when Shutdown was picked or the
 * countdown expired on Shutdown-as-default (no entries at all). */
u32 ocbl_menu_run(void);

/* Serial-only summary of the final choice (used by the B3 dispatch
 * in ocbl_main; B4 replaces the "would boot" stop with the loader). */
void ocbl_menu_announce(u32 index);

#endif /* OCBL_MENU_H */
