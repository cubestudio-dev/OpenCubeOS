/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 2.0.0 - services/camera L1 surface (SYS_L1SVC cat
 * "camera"): the camera + legacy-port diagnostics family (UVC camera,
 * SCSI, gameport, parallel port, IR - the v1 "camera" command class).
 * Bodies stay in their v2 homes (recovery/shell_cmds_camera.c,
 * shell_cmds_scsi.c, shell_cmds_pargame.c, shell_cmds_ir.c - exported
 * via their headers), referenced by the op table. */
#include "core_registry.h"
#include "l1svc_defs.h"
#include "shell_cmds_camera.h"
#include "shell_cmds_scsi.h"
#include "shell_cmds_pargame.h"
#include "shell_cmds_ir.h"
#include "shell.h"

typedef int (*cam_cmd_fn_t)(const char *);

static int cam_l1_dispatch(u32 op, const char *arg, char *out, int cap) {
    if (!out || cap <= 0) return -1;
    cam_cmd_fn_t fn = 0;
    switch (op) {
        case L1SVC_CAM_UVC:           fn = shell_cmd_uvc;           break;
        case L1SVC_CAM_UVCTEST:       fn = shell_cmd_uvctest;       break;
        case L1SVC_CAM_SCSI:          fn = shell_cmd_scsi;          break;
        case L1SVC_CAM_SCSITEST:      fn = shell_cmd_scsitest;      break;
        case L1SVC_CAM_GAMEPORT:      fn = shell_cmd_gameport;      break;
        case L1SVC_CAM_GAMEPORTTEST:  fn = shell_cmd_gameporttest;  break;
        case L1SVC_CAM_PARALLEL:      fn = shell_cmd_parallel;      break;
        case L1SVC_CAM_PARALLELTEST:  fn = shell_cmd_paralleltest;  break;
        case L1SVC_CAM_IR:            fn = shell_cmd_ir;            break;
        case L1SVC_CAM_IRTEST:        fn = shell_cmd_irtest;        break;
        default: return -1;
    }
    return shell_run_fn_captured(fn, arg, out, cap) == 0 ? 0 : -1;
}

/* v2.0.0 group N: camera service identity (UVC + legacy-port probes);
 * device probing stays in kmain/driver init. */
OC_SERVICE_DECL_L1("camera", "camera service (UVC + scsi/gameport/parallel/ir probes + ring-3 surface)",
                   (oc_service_init_fn_t)0, (oc_service_late_fn_t)0,
                   cam_l1_dispatch);
