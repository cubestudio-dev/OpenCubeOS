/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-12-watchdog_logging
 * File: shell/shell_cmds_wd.h
 * Purpose: `watchdog` command registration (see .c).
 */
#ifndef OC_SHELL_CMDS_WD_H
#define OC_SHELL_CMDS_WD_H

int shell_cmd_watchdog(const char *args);
void shell_cmds_wd_register(void);

#endif /* OC_SHELL_CMDS_WD_H */
