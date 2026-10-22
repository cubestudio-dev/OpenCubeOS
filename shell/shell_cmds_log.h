/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-12-watchdog_logging
 * File: shell/shell_cmds_log.h
 * Purpose: dmesg / logcat / logctl command registration (see .c).
 */
#ifndef OC_SHELL_CMDS_LOG_H
#define OC_SHELL_CMDS_LOG_H

int shell_cmd_log_dmesg(const char *args);
int shell_cmd_log_logcat(const char *args);
int shell_cmd_log_logctl(const char *args);

void shell_cmds_log_register(void);

#endif /* OC_SHELL_CMDS_LOG_H */
