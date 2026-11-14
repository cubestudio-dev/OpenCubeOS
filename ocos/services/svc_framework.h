/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS NAK - services/svc_framework: the OC_SERVICE_DECL
 * self-registration framework.
 *
 * NAK rule ("everything is extensible by adding a file"): a root
 * service declares its identity IN THE FILE THAT OWNS ITS RING-3
 * SURFACE via OC_SERVICE_DECL / OC_SERVICE_DECL_L1 below.  The macro
 * places a small const descriptor into the dedicated .oc_svcs linker
 * section; at boot svc_framework_collect() walks the section
 * boundaries (exported by linker.ld) and fills the runtime table.
 *
 *   category : equals the ocos/services/<category>/ folder name -
 *              the services/ tree and the L1 interface surface
 *              categories are one-to-one by rule - with ONE declared
 *              exception: category "ocire" (the L1SVC ABI name,
 *              frozen by the OCIRE log-agent contract) is implemented
 *              by services/ocembed/.
 *   init / late_init : boot callbacks (NULL = none).  The boot ORDER
 *              stays the explicit kmain composition-root sequence
 *              (the dependency chain is hand-curated); the collected
 *              table is the identity / dispatch surface.
 *   l1_dispatch : the SYS_L1SVC entry (see svc_sysapi_l1svc.h).
 *              OC_SERVICE_DECL declares a kernel-only service;
 *              OC_SERVICE_DECL_L1 declares one with a ring-3 surface.
 */
#ifndef OC_SVC_FRAMEWORK_H
#define OC_SVC_FRAMEWORK_H

#include "core_registry.h"   /* OC_DECL_UID (kernel-core uid macro) */

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*oc_service_init_fn_t)(void);
typedef int  (*oc_service_late_fn_t)(void);   /* 0 = probed something */
typedef int  (*oc_service_l1_fn_t)(u32 op, const char *arg, char *out, int cap);

typedef struct {
    const char         *category;   /* == services/<category> folder */
    const char         *desc;
    oc_service_init_fn_t init;      /* early bring-up, may be NULL */
    oc_service_late_fn_t late_init; /* probe pass, may be NULL */
    oc_service_l1_fn_t  l1_dispatch; /* SYS_L1SVC entry, may be NULL */
} oc_service_decl_t;

#define OC_SERVICE_DECL(cat, dsc, ini, late) \
    static const oc_service_decl_t OC_DECL_UID(__ocsv_) \
    __attribute__((used, section(".oc_svcs"))) = { (cat), (dsc), (ini), (late), (oc_service_l1_fn_t)0 }

#define OC_SERVICE_DECL_L1(cat, dsc, ini, late, disp) \
    static const oc_service_decl_t OC_DECL_UID(__ocsv_) \
    __attribute__((used, section(".oc_svcs"))) = { (cat), (dsc), (ini), (late), (disp) }

/* ---- Boot-time collection ------------------------------------------
 * Walkes the .oc_svcs linker section once, early in kmain.  Section
 * boundaries are provided by linker.ld (__oc_svcs_start/_end).
 */
void svc_framework_collect(void);

int  svc_framework_count(void);
const oc_service_decl_t *svc_framework_at(int i);

/* Dispatch one SYS_L1SVC call to the owning service.
 * Returns the service's return code, or -1 when the category is
 * unknown / has no ring-3 surface. */
int svc_framework_dispatch(const char *cat, u32 op,
                            const char *arg, char *out, int cap);

#ifdef __cplusplus
}
#endif

#endif /* OC_SVC_FRAMEWORK_H */
