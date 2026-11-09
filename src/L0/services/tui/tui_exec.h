/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS 3.0.0-WP04 G11c
 * File: services/tui/tui_exec.h
 * Purpose: the TUI desktop's command bridge to the ring-3 app world.
 *
 * v2.2.0's desktop ran on the L0 shell: shell_execute_captured() ran a
 * command line with captured console output, shell_execute() handed
 * the console over (the terminal applet), and shell_cmd_enum() listed
 * the shell's registered commands (the command bar).  WP-03 deleted
 * that shell; 3.0.0's executable surface is the embedded L1 app
 * registry (OC_USERPROGS[], tools/gen_userprogs.py).  This bridge is
 * the same adaptation the G11a sshd exec path made:
 *
 *   - tui_exec_captured(cmd, out, cap): spawn the L1 app named by the
 *     command line's first word and wait for it to retire INSIDE a
 *     core_kcap captured run (every console byte the app writes lands
 *     in the capture buffer; hook chaining + restore keep the serial
 *     mirror alive).  Arguments after the first word are dropped - the
 *     kernel-side spawn path (user_process_create) is argv-less; the
 *     command runs in its default form (declared in the G14 dossier).
 *   - tui_exec_interactive(cmd): spawn the app and let it own the
 *     system console live (the terminal applet runs `ush` this way),
 *     wait for retire, return 0.
 *   - tui_exec_enum(out, cap): list the registry (name/help/wp rows,
 *     registry order; the caller sorts as v2.2.0 did).
 *
 * The retire waits park on sti;hlt - the bridge runs inside the ring-3
 * service dispatch frame where core_sched_yield() is forbidden
 * (see tui_input.c for the full note).
 */
#ifndef OC_TUI_EXEC_H
#define OC_TUI_EXEC_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One row of the command enumeration (the v2.2.0 shell_cmd_info_t
 * shape, carried verbatim; the registry fills the fields). */
typedef struct {
    char name[32];
    char help[96];
    char wp[24];
} tui_cmd_info_t;

/* Copy up to `cap` registered apps into `out` (registry order, which
 * is A-Z from gen_userprogs.py).  Returns the number of rows written. */
int tui_exec_enum(tui_cmd_info_t *out, int cap);

/* Spawn the L1 app named by the first word of `cmd` and wait for it to
 * retire with its console output captured into out[0..cap).
 * Returns 0 on success (the app ran to completion), -1 unknown name or
 * spawn failure, -2 retire timeout. */
int tui_exec_captured(const char *cmd, char *out, int cap);

/* Spawn the app and hand it the live console (no capture); wait for
 * retire.  Returns 0 on success, -1 unknown name, -2 retire timeout. */
int tui_exec_interactive(const char *cmd);

#ifdef __cplusplus
}
#endif

#endif /* OC_TUI_EXEC_H */
