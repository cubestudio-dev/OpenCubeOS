/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP06a-ocse
 * File: ocse/ocse_cl.h
 * Purpose: OCSECL - the OCSE command line (help / lang / hwcheck /
 * disks / gui / clear / halt). Shares the action enum with the GUI.
 */
#ifndef OCSE_CL_H
#define OCSE_CL_H

#include "ocse_gui.h"      /* OCSE_ACT_* */

/* Read-execute-print loop. Returns OCSE_ACT_GOTO_GUI when the user
 * types `gui` or presses Ctrl+Alt+F1 (main performs the switch). */
int ocse_cl_loop(void);

/* WP-06b batch-5: shared ustar extract engine (the `install` state
 * machine in ocse_inst.c calls this). Archive/destdir are paths as
 * the user would type them; returns a command exit code. */
int ocse_cl_tar_extract(const char* archive, const char* destdir);

#endif /* OCSE_CL_H */
