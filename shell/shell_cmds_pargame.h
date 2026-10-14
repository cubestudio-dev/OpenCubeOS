/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-camera_misc
 * File: shell/shell_cmds_pargame.h
 *
 * `parallel` / `paralleltest` / `gameport` / `gameporttest` commands.
 */
#ifndef OC_SHELL_CMDS_PARGAME_H
#define OC_SHELL_CMDS_PARGAME_H

int shell_cmd_parallel(const char *args);
int shell_cmd_paralleltest(const char *args);
int shell_cmd_gameport(const char *args);
int shell_cmd_gameporttest(const char *args);

void shell_cmds_pargame_register(void);

#endif /* OC_SHELL_CMDS_PARGAME_H */
