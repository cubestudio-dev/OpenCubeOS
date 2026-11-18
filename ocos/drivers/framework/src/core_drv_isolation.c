/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP08 - drivers/framework: driver failure isolation.
 *
 * The failure ledger: one record per failing driver name (merged
 * count), consulted by the diag surface (`drv failed`) and by the
 * retry path (oc_drv_fail_clear on successful reload).  Recording a
 * failure never escalates - the caller just moves on to the next
 * driver. */
#include "core_drv_isolation.h"
#include "core_lib_string.h"
#include "core_timer.h"

static oc_drv_failrec_t s_rec[OC_DRV_FAILREC_MAX];
static int s_nrec;

static void cpy(char *dst, const char *src, int cap)
{
    strncpy(dst, src, (usize)(cap - 1));
    dst[cap - 1] = 0;
}

void oc_drv_fail_note(const char *name, int err)
{
    if (!name || !name[0]) return;

    for (int i = 0; i < s_nrec; i++) {
        if (strcmp(s_rec[i].name, name) == 0) {
            s_rec[i].err = err;
            s_rec[i].count++;
            s_rec[i].last_ts = core_timer_now_ms();
            return;
        }
    }
    if (s_nrec >= OC_DRV_FAILREC_MAX) return;   /* ledger full: drop */
    oc_drv_failrec_t *r = &s_rec[s_nrec++];
    cpy(r->name, name, OC_DRV_FAILNAME_MAX);
    r->err = err;
    r->count = 1;
    r->last_ts = core_timer_now_ms();
}

int oc_drv_fail_count(void) { return s_nrec; }

const oc_drv_failrec_t *oc_drv_fail_at(int i)
{
    return (i >= 0 && i < s_nrec) ? &s_rec[i] : 0;
}

int oc_drv_fail_clear(const char *name)
{
    if (!name) return -1;
    for (int i = 0; i < s_nrec; i++) {
        if (strcmp(s_rec[i].name, name) == 0) {
            s_rec[i] = s_rec[s_nrec - 1];
            s_nrec--;
            return 0;
        }
    }
    return -1;
}
