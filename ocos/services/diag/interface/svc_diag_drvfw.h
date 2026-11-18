/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* NAK service API umbrella: diag - WP-08 drv/dev surface.
 * The L1SVC_DIAG_DRV and L1SVC_DIAG_DEV op bodies (ops 40..51) live
 * in services/diag/src/svc_diag_drvfw.c; this header exports the
 * shared dispatch svc_diag_l1.c routes into. */
#ifndef SVC_DIAG_DRVFW_H
#define SVC_DIAG_DRVFW_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

int svc_diag_drvfw_dispatch(u32 op, const char *arg, char *out, int cap);

#ifdef __cplusplus
}
#endif

#endif /* SVC_DIAG_DRVFW_H */
