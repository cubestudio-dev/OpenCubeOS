/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-camera_misc
 * File: services/camera/cam_cmds_pargame.h
 *
 * `parallel` / `paralleltest` / `gameport` / `gameporttest` commands.
 */
#ifndef OC_CAM_CMDS_PARGAME_H
#define OC_CAM_CMDS_PARGAME_H

int cam_cmd_parallel(const char *args);
int cam_cmd_paralleltest(const char *args);
int cam_cmd_gameport(const char *args);
int cam_cmd_gameporttest(const char *args);

#endif /* OC_CAM_CMDS_PARGAME_H */
