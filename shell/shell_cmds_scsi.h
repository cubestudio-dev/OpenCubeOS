/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-camera_misc
 * File: shell/shell_cmds_scsi.h
 *
 * `scsi` (status) and `scsitest` (in-kernel self test) commands.
 */
#ifndef OC_SHELL_CMDS_SCSI_H
#define OC_SHELL_CMDS_SCSI_H

int shell_cmd_scsi(const char *args);
int shell_cmd_scsitest(const char *args);
void shell_cmds_scsi_register(void);

#endif /* OC_SHELL_CMDS_SCSI_H */
