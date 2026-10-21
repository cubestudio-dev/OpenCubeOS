/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-11-crash_recovery
 * File: shell/shell_cmds_crash.h
 * Purpose: crash-recovery command registration (crashlog/crashtest/dumpinfo).
 */
#ifndef OC_SHELL_CMDS_CRASH_H
#define OC_SHELL_CMDS_CRASH_H

#include "shell.h"

/* command handlers (exported for the registration below and for any
 * L1/TUI bridge that wants to drive them directly) */
int shell_cmd_crash_crashlog(const char *args);
int shell_cmd_crash_crashtest(const char *args);
int shell_cmd_crash_dumpinfo(const char *args);

/* called once from kmain */
void shell_cmds_crash_register(void);

#endif /* OC_SHELL_CMDS_CRASH_H */
