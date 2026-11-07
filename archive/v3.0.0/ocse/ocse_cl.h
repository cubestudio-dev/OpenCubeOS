/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP06c-installer_gui
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

/* WP-06c: the GUI wizard's command channel - run one line through
 * the SAME tokenizer, command table and handlers as the interactive
 * shell (no output hits the console; the serial line keeps its copy).
 * 'out' receives the captured output (NUL-terminated). Returns the
 * command's exit code (0 = ok; the shell's 0x100 GUI request cannot
 * occur - the wizard never issues `gui`). */
int ocse_cl_exec(const char* line, char* out, int outsz);

#endif /* OCSE_CL_H */
