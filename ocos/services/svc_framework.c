/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS NAK - services/svc_framework: boot-time collection of
 * the OC_SERVICE_DECL / OC_SERVICE_DECL_L1 file-local identities (see
 * svc_framework.h for the rule).
 *
 * The section boundaries are exported by linker.ld
 * ( __oc_svcs_start/_end ).  Walking them once at boot fills the
 * identity table + the SYS_L1SVC dispatch surface.
 */
#include "svc_framework.h"
#include "core_lib_log.h"

/* ---- Linker-provided boundaries ----------------------------------- */
extern const oc_service_decl_t __oc_svcs_start[], __oc_svcs_end[];

/* ---- Collected view ----------------------------------------------- */
static const oc_service_decl_t *s_svcs[64];
static int s_nsvcs;

void svc_framework_collect(void)
{
    s_nsvcs = 0;
    for (const oc_service_decl_t *p = __oc_svcs_start; p < __oc_svcs_end; p++) {
        if (p->category && s_nsvcs < (int)(sizeof(s_svcs) / sizeof(s_svcs[0])))
            s_svcs[s_nsvcs++] = p;
    }
    lib_log_info("services: file-local OC_SERVICE_DECL collection ready");
}

int svc_framework_count(void) { return s_nsvcs; }

const oc_service_decl_t *svc_framework_at(int i)
{ return (i >= 0 && i < s_nsvcs) ? s_svcs[i] : 0; }

int svc_framework_dispatch(const char *cat, u32 op,
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
