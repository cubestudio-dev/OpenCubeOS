/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-05/WP-07
 * File: kernel/file_cmds.h
 * Purpose: Register all file-operation shell commands.
 */
#ifndef OC_FILE_CMDS_H
#define OC_FILE_CMDS_H

/* Register all WP-05/WP-07 file operation shell commands. Call once at boot. */
void file_cmds_register(void);

#endif /* OC_FILE_CMDS_H */
