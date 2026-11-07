/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 2.0.0 - core/registry: file-local identity declarations.
 *
 * v2.0.0 architecture rule ("everything is extensible by adding a file"):
 *   Nothing registers into global tables through runtime calls anymore.
 *   A shell command, a driver, a service or an L1 app declares its
 *   identity IN THE FILE THAT IMPLEMENTS IT via the OC_*_DECL macros
 *   below.  Each macro places a small const descriptor into a dedicated
 *   linker section; at boot core_registry_collect() walks the section
 *   boundaries (exported by linker.ld) and fills the runtime tables.
 *
 *   Adding a new driver/service/app = adding one .c file in the
 *   right folder + one declaration line.  No global table edit, no
 *   registration call site, no core file change.
 *
 * Sections (see linker.ld):
 *   .oc_drvs - oc_driver_decl_t      (hardware drivers, per family)
 *   .oc_svcs - oc_service_decl_t     (kernel services, one per category)
 *   .oc_apps - oc_l1_app_decl_t      (embedded L1 ring-3 applications)
 *
 * 3.0.0-WP03: the .oc_cmds section (oc> recovery shell commands) is
 * DELETED together with the L0 command surface - the whole shell
 * identity block below (oc_shell_cmd_decl_t, OC_SHELL_CMD) is gone.
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
 * (Shared by every identity block below.) */
#define OC_DECL_UID(prefix)    OC_DECL_UID2(prefix, __COUNTER__)
#define OC_DECL_UID2(prefix, c) OC_DECL_UID3(prefix, c)
#define OC_DECL_UID3(prefix, c) prefix##c

/* ---- Driver identity -----------------------------------------------
 * family : "block", "nic", "snd", "display", "input", ... - selects the
 *          driver manager that owns the declaration (one manager per
 *          family; the managers live in the matching services/ folder).
 * ops    : family-specific ops table (driver_block_ops_t, ...).  The
 *          manager stores the pointer; probe/init callbacks inside the
 *          ops table do the hardware bring-up when the manager walks
 *          the section.
 * name   : stable driver identity string ("ata", "nvme", "e1000", ...).
 *
 * This REPLACES the v1.x register-class L1 interfaces
 * (driver_block_register / driver_nic_register / driver_snd_register):
 * those interfaces are deleted in 2.0.0; operation-class interfaces
 * (read/write/send/recv/play/...) stay.
 */
typedef struct {
    const char *family;
    const void *ops;        /* const driver_*_ops_t* (typed per family) */
    const char *name;
    const char *desc;
} oc_driver_decl_t;

#define OC_DRIVER_DECL(fam, nm, opsp, dsc) \
    static const oc_driver_decl_t OC_DECL_UID(__ocdv_) \
    __attribute__((used, section(".oc_drvs"))) = { (fam), (opsp), (nm), (dsc) }

/* ---- Service identity ----------------------------------------------
 * One declaration per L0 service (services/<category>/ folder).  The
 * category string equals the folder name (the services/ tree and the
 * L1 interface surface categories are one-to-one by rule) - with ONE
 * declared exception: category "ocire" (the L1SVC ABI name, frozen by
 * the OCIRE log-agent contract) is implemented by services/ocembed/
 * (the 3.0.0-WP03 spec's folder name for the embed service).
 * init/late_init run during boot collection (NULL = no callback);
 * both return void (failures are logged by the service itself).
 *
 * v2.0.0 L1 rewrite: l1_dispatch exposes the service's operation
 * surface to ring-3 applications through SYS_L1SVC (see
 * services/l1/l1svc_defs.h).  NULL = the service has no ring-3
 * surface (kernel-only service).  OC_SERVICE_DECL declares one
 * without a ring-3 surface; OC_SERVICE_DECL_L1 declares one with it.
 */
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
 * Walks the linker sections once, early in kmain.  Section boundaries
 * are provided by linker.ld.  Fills the runtime tables; reports the
 * counts through the getters below (also surfaced via `svc` / tests).
 */
void core_registry_collect(void);

int  core_registry_drvs_count(void);
const oc_driver_decl_t *core_registry_drv_at(int i);

int  core_registry_svcs_count(void);
const oc_service_decl_t *core_registry_svc_at(int i);

/* v2.0.0: dispatch one SYS_L1SVC call to the owning service.
 * Returns the service's return code, or -1 when the category is
 * unknown / has no ring-3 surface. */
int core_registry_svc_dispatch(const char *cat, u32 op,
                                const char *arg, char *out, int cap);

int  core_registry_apps_count(void);
const oc_l1_app_decl_t *core_registry_app_at(int i);

#ifdef __cplusplus
}
#endif

#endif /* OC_CORE_REGISTRY_H */
