/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-camera_misc
 * File: shell/shell_cmds_ir.h
 *
 * `ir` / `irtest` commands (WP-10-camera_misc).
 */
#ifndef OC_SHELL_CMDS_IR_H
#define OC_SHELL_CMDS_IR_H

int shell_cmd_ir(const char *args);
int shell_cmd_irtest(const char *args);

void shell_cmds_ir_register(void);

#endif /* OC_SHELL_CMDS_IR_H */
