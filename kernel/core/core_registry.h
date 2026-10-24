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
 *   Adding a new command/driver/service/app = adding one .c file in the
 *   right folder + one declaration line.  No global table edit, no
 *   registration call site, no core file change.
 *
 * Sections (see linker.ld):
 *   .oc_cmds - oc_shell_cmd_decl_t   (oc> recovery shell commands)
 *   .oc_drvs - oc_driver_decl_t      (hardware drivers, per family)
 *   .oc_svcs - oc_service_decl_t     (kernel services, one per category)
 *   .oc_apps - oc_l1_app_decl_t      (embedded L1 ring-3 applications)
 */
#ifndef OC_CORE_REGISTRY_H
#define OC_CORE_REGISTRY_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Shell command identity ---------------------------------------
 * name  : command word typed at the oc> prompt
 * handler : int handler(const char *args) - unchanged shell ABI
 * help  : one-line English description (help -a / default help)
 * group : service-group tag (help -w view); a short lowercase token
 *         that names the owning L0 service category, e.g. "fs",
 *         "net", "klog", "health", "watchdog", "ota", "recovery",
 *         "power", "time", "block", "nic", "snd", "display", "input".
 */
typedef int (*oc_shell_cmd_fn_t)(const char *args);

typedef struct {
    const char        *name;
    oc_shell_cmd_fn_t  handler;
    const char        *help;
    const char        *group;
} oc_shell_cmd_decl_t;

/* __COUNTER__ keeps the file-local symbol unique when one handler backs
 * several command words in the same file (alias-style entries).  The
 * symbol is static: no cross-object collision is possible. */
#define OC_SHELL_CMD(nm, fn, hp, grp) \
    static const oc_shell_cmd_decl_t __ocsc_##fn##_##__COUNTER__ \
    __attribute__((used, section(".oc_cmds"))) = { (nm), (fn), (hp), (grp) }

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
    static const oc_driver_decl_t __ocdv_##nm##_##__COUNTER__ \
    __attribute__((used, section(".oc_drvs"))) = { (fam), (opsp), (nm), (dsc) }

/* ---- Service identity ----------------------------------------------
 * One declaration per L0 service (services/<category>/ folder).  The
 * category string MUST equal the folder name - the services/ tree and
 * the L1 interface surface categories are one-to-one by rule.
 * init/late_init run during boot collection (NULL = no callback).
 */
typedef int (*oc_service_init_fn_t)(void);

typedef struct {
    const char         *category;   /* == services/<category> folder */
    const char         *desc;
    oc_service_init_fn_t init;      /* early bring-up, may be NULL */
    oc_service_init_fn_t late_init; /* after all early inits, may be NULL */
} oc_service_decl_t;

#define OC_SERVICE_DECL(cat, dsc, ini, late) \
    static const oc_service_decl_t __ocsv_##cat##_##__COUNTER__ \
    __attribute__((used, section(".oc_svcs"))) = { (cat), (dsc), (ini), (late) }

/* ---- L1 application identity ----------------------------------------
 * name  : app word used by the launcher (config l1.entry / oc> `l1`)
 * elf/size : embedded ET_EXEC image (see l1apps/ + userprogs build)
 * is_default : non-zero = this app is the default boot entry
 * (exactly one default is expected; the launcher picks it at boot).
 */
typedef struct {
    const char *name;
    const u8   *elf;
    u64         size;
    int         is_default;
    const char *desc;
} oc_l1_app_decl_t;

#define OC_L1_APP_DECL(nm, elfp, szp, dflt, dsc) \
    static const oc_l1_app_decl_t __ocap_##nm##_##__COUNTER__ \
    __attribute__((used, section(".oc_apps"))) = { (nm), (elfp), (szp), (dflt), (dsc) }

/* ---- Boot-time collection ------------------------------------------
 * Walks the linker sections once, early in kmain.  Section boundaries
 * are provided by linker.ld.  Fills the runtime tables; reports the
 * counts through the getters below (also surfaced via `svc` / tests).
 */
void core_registry_collect(void);

int  core_registry_cmds_count(void);
const oc_shell_cmd_decl_t *core_registry_cmd_at(int i);

int  core_registry_drvs_count(void);
const oc_driver_decl_t *core_registry_drv_at(int i);

int  core_registry_svcs_count(void);
const oc_service_decl_t *core_registry_svc_at(int i);

int  core_registry_apps_count(void);
const oc_l1_app_decl_t *core_registry_app_at(int i);

#ifdef __cplusplus
}
#endif

#endif /* OC_CORE_REGISTRY_H */
