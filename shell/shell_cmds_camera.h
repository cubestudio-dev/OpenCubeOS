/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-camera_misc
 * File: shell/shell_cmds_camera.h
 *
 * `uvc` and `uvctest` shell commands (WP-10-camera_misc).
 */
#ifndef OC_SHELL_CMDS_CAMERA_H
#define OC_SHELL_CMDS_CAMERA_H

/* uvc - camera subsystem status: registry, formats, frames, intervals */
int shell_cmd_uvc(const char *args);

/* uvctest - full in-kernel self test: JPEG decode against a real
 * baseline vector, UVC descriptor parse against a real UVC 1.1
 * descriptor vector, camera registry state machine, YUY2 conversion,
 * and (when hardware is present) the live probe/commit path */
int shell_cmd_uvctest(const char *args);

/* Register the WP-10-camera_misc camera commands (called from kmain). */
void shell_cmds_camera_register(void);

#endif /* OC_SHELL_CMDS_CAMERA_H */
