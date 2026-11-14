/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS NAK - kernel/core_registry: boot-time collection of
 * the L1-application file-local declarations (see core_registry.h for
 * the rule).
 *
 * The section boundaries are exported by linker.ld
 * ( __oc_apps_start/_end ).  The driver (.oc_drvs) and service
 * (.oc_svcs) halves of the historical all-in-one collector moved to
 * their owning NAK frameworks (ocos/drivers/drv_framework.c and
 * ocos/services/svc_framework.c); the kmain composition root calls
 * the three collectors back-to-back at the same boot spot.
 */
#include "core_registry.h"
#include "core_lib_log.h"

/* ---- Linker-provided boundaries ----------------------------------- */
extern const oc_l1_app_decl_t __oc_apps_start[], __oc_apps_end[];

/* ---- Collected view ----------------------------------------------- */
static const oc_l1_app_decl_t *s_apps[64];
static int s_napps;

void core_registry_collect(void)
{
    s_napps = 0;
    for (const oc_l1_app_decl_t *p = __oc_apps_start; p < __oc_apps_end; p++) {
        if (p->name && p->elf && p->size_ptr && *p->size_ptr &&
            s_napps < (int)(sizeof(s_apps) / sizeof(s_apps[0])))
            s_apps[s_napps++] = p;
    }
    lib_log_info("registry: file-local declarations collected");
}

int core_registry_apps_count(void) { return s_napps; }
const oc_l1_app_decl_t *core_registry_app_at(int i)
{ return (i >= 0 && i < s_napps) ? s_apps[i] : 0; }
