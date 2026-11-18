/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP08 - drivers/framework: probe failure isolation.
 *
 * One place records every metadata-driver probe/init failure: driver
 * name, error code, occurrence count, last timestamp.  A failure
 * NEVER escalates to a panic and never blocks the remaining drivers
 * (core_drv_registry.c just skips to the next one); the record keeps
 * it queryable for diagnostics and later retries (drv reload).
 *
 * Consumption: the diag service (drv failed) and future tooling. */
#ifndef OC_CORE_DRV_ISOLATION_H
#define OC_CORE_DRV_ISOLATION_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OC_DRV_FAILREC_MAX 16
#define OC_DRV_FAILNAME_MAX 24

typedef struct {
    char name[OC_DRV_FAILNAME_MAX];   /* failing driver name        */
    int  err;                         /* last (negative) error code */
    int  count;                       /* total failures by name     */
    u64  last_ts;                     /* ms since boot, last time   */
} oc_drv_failrec_t;

/* Record one failure (merges by name: count++, err/ts refreshed). */
void oc_drv_fail_note(const char *name, int err);

/* Query: at(i) returns 0 out of range; count() = distinct names. */
int oc_drv_fail_count(void);
const oc_drv_failrec_t *oc_drv_fail_at(int i);

/* Drop one driver's record (used after a successful retry so the
 * failure stops showing in drv failed).  0 dropped, -1 not found. */
int oc_drv_fail_clear(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* OC_CORE_DRV_ISOLATION_H */
