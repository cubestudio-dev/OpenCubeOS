/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 2.0.0 - services/camera L1 surface (SYS_L1SVC cat
 * "camera"): the camera + legacy-port diagnostics family (UVC camera,
 * SCSI, gameport, parallel port, IR - the v1 "camera" command class).
 * 3.0.0-WP03: the bodies moved INTO the service folder
 * (cam_cmds_uvc.c, cam_cmds_scsi.c, cam_cmds_pargame.c, cam_cmds_ir.c
 * - rescued verbatim when the oc> shell died, renamed
 * shell_cmd_* -> cam_cmd_*), referenced by the op table. */
#include "core_registry.h"
#include "l1svc_defs.h"
#include "cam_cmds_uvc.h"
#include "cam_cmds_scsi.h"
#include "cam_cmds_pargame.h"
#include "cam_cmds_ir.h"
#include "core_kcap.h"

typedef int (*cam_cmd_fn_t)(const char *);

static int cam_l1_dispatch(u32 op, const char *arg, char *out, int cap) {
    if (!out || cap <= 0) return -1;
    cam_cmd_fn_t fn = 0;
    switch (op) {
        case L1SVC_CAM_UVC:           fn = cam_cmd_uvc;           break;
        case L1SVC_CAM_UVCTEST:       fn = cam_cmd_uvctest;       break;
        case L1SVC_CAM_SCSI:          fn = cam_cmd_scsi;          break;
        case L1SVC_CAM_SCSITEST:      fn = cam_cmd_scsitest;      break;
        case L1SVC_CAM_GAMEPORT:      fn = cam_cmd_gameport;      break;
        case L1SVC_CAM_GAMEPORTTEST:  fn = cam_cmd_gameporttest;  break;
        case L1SVC_CAM_PARALLEL:      fn = cam_cmd_parallel;      break;
        case L1SVC_CAM_PARALLELTEST:  fn = cam_cmd_paralleltest;  break;
        case L1SVC_CAM_IR:            fn = cam_cmd_ir;            break;
        case L1SVC_CAM_IRTEST:        fn = cam_cmd_irtest;        break;
        default: return -1;
    }
    return core_kcap_run_fn(fn, arg, out, cap) == 0 ? 0 : -1;
}

/* v2.0.0 group N: camera service identity (UVC + legacy-port probes);
 * device probing stays in kmain/driver init. */
OC_SERVICE_DECL_L1("camera", "camera service (UVC + scsi/gameport/parallel/ir probes + ring-3 surface)",
                   (oc_service_init_fn_t)0, (oc_service_late_fn_t)0,
                   cam_l1_dispatch);
