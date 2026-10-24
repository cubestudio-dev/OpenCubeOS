/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-12-watchdog_logging
 * File: shell/shell_cmds_health.h
 * Purpose: `health` command registration (see .c).
 */
#ifndef OC_SHELL_CMDS_HEALTH_H
#define OC_SHELL_CMDS_HEALTH_H

int shell_cmd_health(const char *args);
int shell_cmd_health_watch(const char *args);
int shell_cmd_health_export(const char *args);

#endif /* OC_SHELL_CMDS_HEALTH_H */
