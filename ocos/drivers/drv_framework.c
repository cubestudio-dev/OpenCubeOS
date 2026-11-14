/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS NAK - drivers/drv_framework: boot-time collection of
 * the OC_DRIVER_DECL file-local identities (see drv_framework.h for
 * the rule).
 *
 * The section boundaries are exported by linker.ld
 * ( __oc_drvs_start/_end ).  Walking them once at boot replaces every
 * runtime registration call into global tables.
 */
#include "drv_framework.h"
#include "core_lib_log.h"

/* ---- Linker-provided boundaries ----------------------------------- */
extern const oc_driver_decl_t __oc_drvs_start[], __oc_drvs_end[];

/* ---- Collected view ----------------------------------------------- */
static const oc_driver_decl_t *s_drvs[256];
static int s_ndrvs;

void drv_framework_collect(void)
{
    s_ndrvs = 0;
    for (const oc_driver_decl_t *p = __oc_drvs_start; p < __oc_drvs_end; p++) {
        if (p->family && p->ops && s_ndrvs < (int)(sizeof(s_drvs) / sizeof(s_drvs[0])))
            s_drvs[s_ndrvs++] = p;
    }
    lib_log_info("drivers: file-local OC_DRIVER_DECL collection ready");
}

int drv_framework_count(void) { return s_ndrvs; }

const oc_driver_decl_t *drv_framework_at(int i)
{ return (i >= 0 && i < s_ndrvs) ? s_drvs[i] : 0; }
