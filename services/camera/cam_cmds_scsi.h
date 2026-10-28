/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS - WP-10-camera_misc
 * File: services/camera/cam_cmds_scsi.h
 *
 * `scsi` (status) and `scsitest` (in-kernel self test) commands.
 */
#ifndef OC_CAM_CMDS_SCSI_H
#define OC_CAM_CMDS_SCSI_H

int cam_cmd_scsi(const char *args);
int cam_cmd_scsitest(const char *args);

#endif /* OC_CAM_CMDS_SCSI_H */
