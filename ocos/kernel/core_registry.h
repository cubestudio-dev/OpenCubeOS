/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS NAK - kernel/core_registry: file-local identity
 * declarations - the kernel-core part.
 *
 * NAK architecture rule ("everything is extensible by adding a file"):
 *   Nothing registers into global tables through runtime calls anymore.
 *   A driver, a service or an L1 app declares its identity IN THE FILE
 *   THAT IMPLEMENTS IT via the OC_*_DECL macros.  Each macro places a
 *   small const descriptor into a dedicated linker section; at boot
 *   the owning framework collects its section (the driver and service
 *   frameworks live in ocos/drivers/ and ocos/services/; this file
 *   keeps the L1-application part + the shared OC_DECL_UID uid macro).
 *
 *   Adding a new driver/service/app = adding one .c file in the right
 *   folder + one declaration line.  No global table edit, no
 *   registration call site, no core file change.
 *
 * Sections (see linker.ld):
 *   .oc_drvs - oc_driver_decl_t      (hardware drivers; collected by
 *              ocos/drivers/drv_framework.c - OC_DRIVER_DECL)
 *   .oc_svcs - oc_service_decl_t     (kernel services; collected by
 *              ocos/services/svc_framework.c - OC_SERVICE_DECL /
 *              OC_SERVICE_DECL_L1)
 *   .oc_apps - oc_l1_app_decl_t      (embedded L1 ring-3 applications;
 *              collected HERE - OC_L1_APP_DECL)
 *
 * The boot orchestration (which framework collects when) lives in the
 * kmain composition root (core_boot_main.c).
 */
#ifndef OC_CORE_REGISTRY_H
#define OC_CORE_REGISTRY_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Two-level expansion so __COUNTER__ is evaluated BEFORE the token
 * paste (## suppresses macro expansion of its operands - the naive
 * single-level paste produced a literal __COUNTER__ symbol and two
 * entries with the same handler collided).  The symbol stays
 * file-local (static): no cross-object collision is possible.
 * (Shared by every declaration framework: drv_framework.h,
 * svc_framework.h and the app declaration below.) */
#define OC_DECL_UID(prefix)    OC_DECL_UID2(prefix, __COUNTER__)
#define OC_DECL_UID2(prefix, c) OC_DECL_UID3(prefix, c)
#define OC_DECL_UID3(prefix, c) prefix##c

/* ---- L1 application identity ----------------------------------------
 * name  : app word used by the launcher (config l1.entry / oc> `l1`)
 * elf/size_ptr : embedded ET_EXEC image (see l1apps/ + userprogs
 * build).  The size arrives as a POINTER to the generated
 * userprog_<name>_size symbol: the bytes live in a different
 * translation unit (core_usermode.o compiles userprogs_data.h once),
 * so the value itself is not a compile-time constant here.
 * is_default : non-zero = this app is the default boot entry
 * (exactly one default is expected; the launcher picks it at boot).
 */
typedef struct {
    const char *name;
    const u8   *elf;
    const u64  *size_ptr;
    int         is_default;
    const char *desc;
} oc_l1_app_decl_t;

#define OC_L1_APP_DECL(nm, elfp, szp, dflt, dsc) \
    static const oc_l1_app_decl_t OC_DECL_UID(__ocap_) \
    __attribute__((used, section(".oc_apps"))) = { (nm), (elfp), (szp), (dflt), (dsc) }

/* ---- Boot-time collection ------------------------------------------
 * Walks the .oc_apps linker section once, early in kmain.  Section
 * boundaries are provided by linker.ld.  Fills the runtime table;
 * reports the count through the getters below (also surfaced via the
 * L1 launcher and tests).
 *
 * NOTE (NAK): the .oc_drvs / .oc_svcs halves of the historical
 * core_registry_collect() moved to their owning frameworks
 * (drv_framework_collect / svc_framework_collect); kmain calls the
 * three collectors in the same boot spot as before.
 */
void core_registry_collect(void);

int  core_registry_apps_count(void);
const oc_l1_app_decl_t *core_registry_app_at(int i);

#ifdef __cplusplus
}
#endif

#endif /* OC_CORE_REGISTRY_H */
