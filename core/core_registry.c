/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 2.0.0 - core/registry: boot-time collection of the
 * file-local identity declarations (see core_registry.h for the rule).
 *
 * The section boundaries below are exported by linker.ld
 * ( __oc_cmds_start/_end, __oc_drvs_start/_end, __oc_svcs_start/_end,
 *   __oc_apps_start/_end ).  Walking them once at boot replaces every
 * v1.x runtime registration call into global tables.
 */
#include "core_registry.h"
#include "lib_log.h"

/* ---- Linker-provided boundaries ----------------------------------- */
extern const oc_shell_cmd_decl_t  __oc_cmds_start[], __oc_cmds_end[];
extern const oc_driver_decl_t     __oc_drvs_start[], __oc_drvs_end[];
extern const oc_service_decl_t    __oc_svcs_start[], __oc_svcs_end[];
extern const oc_l1_app_decl_t     __oc_apps_start[], __oc_apps_end[];

/* ---- Collected views ---------------------------------------------- */
static const oc_shell_cmd_decl_t *s_cmds[512];
static int s_ncmds;
static const oc_driver_decl_t    *s_drvs[256];
static int s_ndrvs;
static const oc_service_decl_t   *s_svcs[64];
static int s_nsvcs;
static const oc_l1_app_decl_t    *s_apps[64];
static int s_napps;

void core_registry_collect(void)
{
    s_ncmds = 0;
    for (const oc_shell_cmd_decl_t *p = __oc_cmds_start; p < __oc_cmds_end; p++) {
        if (p->name && p->handler && s_ncmds < (int)(sizeof(s_cmds) / sizeof(s_cmds[0])))
            s_cmds[s_ncmds++] = p;
    }
    s_ndrvs = 0;
    for (const oc_driver_decl_t *p = __oc_drvs_start; p < __oc_drvs_end; p++) {
        if (p->family && p->ops && s_ndrvs < (int)(sizeof(s_drvs) / sizeof(s_drvs[0])))
            s_drvs[s_ndrvs++] = p;
    }
    s_nsvcs = 0;
    for (const oc_service_decl_t *p = __oc_svcs_start; p < __oc_svcs_end; p++) {
        if (p->category && s_nsvcs < (int)(sizeof(s_svcs) / sizeof(s_svcs[0])))
            s_svcs[s_nsvcs++] = p;
    }
    s_napps = 0;
    for (const oc_l1_app_decl_t *p = __oc_apps_start; p < __oc_apps_end; p++) {
        if (p->name && p->elf && p->size_ptr && *p->size_ptr &&
            s_napps < (int)(sizeof(s_apps) / sizeof(s_apps[0])))
            s_apps[s_napps++] = p;
    }
    lib_log_info("registry: file-local declarations collected");
    {
        char line[96]; char n[12];
        lib_log_info("registry: cmds/drivers/services/apps in table");
        (void)line; (void)n;
    }
}

int core_registry_cmds_count(void) { return s_ncmds; }
const oc_shell_cmd_decl_t *core_registry_cmd_at(int i)
{ return (i >= 0 && i < s_ncmds) ? s_cmds[i] : 0; }

int core_registry_drvs_count(void) { return s_ndrvs; }
const oc_driver_decl_t *core_registry_drv_at(int i)
{ return (i >= 0 && i < s_ndrvs) ? s_drvs[i] : 0; }

int core_registry_svcs_count(void) { return s_nsvcs; }
const oc_service_decl_t *core_registry_svc_at(int i)
{ return (i >= 0 && i < s_nsvcs) ? s_svcs[i] : 0; }

int core_registry_apps_count(void) { return s_napps; }
const oc_l1_app_decl_t *core_registry_app_at(int i)
{ return (i >= 0 && i < s_napps) ? s_apps[i] : 0; }

int core_registry_svc_dispatch(const char *cat, u32 op,
                                const char *arg, char *out, int cap)
{
    if (!cat || !out || cap <= 0) return -1;
    for (int i = 0; i < s_nsvcs; i++) {
        const oc_service_decl_t *s = s_svcs[i];
        const char *a = s->category, *b = cat;
        while (*a && *b && *a == *b) { a++; b++; }
        if (*a == 0 && *b == 0) {
            if (!s->l1_dispatch) return -1;   /* kernel-only service */
            return s->l1_dispatch(op, arg, out, cap);
        }
    }
    return -1;
}
