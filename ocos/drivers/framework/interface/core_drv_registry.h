/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP08 - drivers/framework: the driver registry.
 *
 * Runtime state for every collected metadata driver + the boot
 * orchestration (collect -> order -> init pass -> auto-bind pass ->
 * hotplug drain) + the manual bind/unbind/reload entries the diag
 * surface drives.  Failure isolation is the rule here: a probe that
 * returns non-zero is recorded (core_drv_isolation.c), the node is
 * marked "not bound", and the walk carries on - no panic, no hang.
 *
 * Auto-bind rule: after the init pass the tree is walked; for every
 * unbound node, metadata drivers are tried in computed order against
 * the node's type; a driver whose dependencies are not all OK is
 * skipped.  First successful probe wins the node. */
#ifndef OC_CORE_DRV_REGISTRY_H
#define OC_CORE_DRV_REGISTRY_H

#include "types.h"
#include "core_drv_meta.h"
#include "core_devtree.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Driver states ---------------------------------------------------- */
enum oc_drv_state {
    OC_DRVST_REGISTERED = 0,   /* collected, init not run yet          */
    OC_DRVST_OK,               /* init succeeded (may bind nodes)      */
    OC_DRVST_FAILED,           /* init or every probe failed (retryable)*/
    OC_DRVST_CYCLE,            /* dependency cycle member (isolated)   */
    OC_DRVST_MISSING_DEP,      /* depends on an unknown driver         */
};

typedef struct {
    const oc_driver_info_t *info;
    int state;                 /* enum oc_drv_state                    */
    int last_err;              /* last error code (0 = none)           */
    int fail_count;            /* probe/init failures so far           */
    int nbound;                /* nodes currently bound to it          */
} oc_drv_reg_t;

/* ---- Registry query ----------------------------------------------------
 * Covers the metadata path only; the diag render merges the legacy
 * OC_DRIVER_DECL table (drv_framework_at()) on top for `drv list`. */
int oc_drvfw_count(void);
const oc_drv_reg_t *oc_drvfw_at(int i);            /* 0 out of range */
const oc_drv_reg_t *oc_drvfw_find(const char *name);

/* ---- Boot orchestration ------------------------------------------------
 * kmain calls this once, right after the declaration-registry
 * collectors: collect .oc_drvi, compute the deterministic order,
 * run init in that order (isolating failures), run the auto-bind
 * pass over the device tree, drain the hotplug queue, and log the
 * order + the isolation summary on the boot console. */
void oc_drvfw_boot(void);

/* ---- Manual operations (diag surface; debug use) -----------------------
 * bind: probe drv against an unbound node (type match is ADVISED but
 *   not forced - manual bind is an explicit operator decision).
 *   0 = bound now; >0 = the probe error code; <0 = framework refusal
 *   (unknown driver / unknown node / node already bound / deps not OK).
 * unbind: run remove() on a bound pair and drop the binding.
 *   0 ok; <0 = refusal (unknown pair / not bound).
 *   LIFECYCLE: the framework clears node->bound BEFORE remove() runs,
 *   and touches NOTHING on the node afterwards - a driver that owns
 *   its node may unregister it from inside remove().
 * reload: unbind every node of the driver, then re-run its init and
 *   re-bind pass (a retry after a failure clears the record on
 *   success).  0 ok; >0 = the new init/probe error; <0 = refusal.
 * detach_subtree: unbind a node (and, recursively, its children) and
 *   unregister them - the cascade helper a bus driver calls from its
 *   own remove() for the child devices it owns. */
int oc_drvfw_bind(const char *drv_name, const char *node_name);
int oc_drvfw_unbind(const char *drv_name, const char *node_name);
int oc_drvfw_reload(const char *drv_name);
int oc_drvfw_detach_subtree(oc_devnode_t *node);

/* Re-run the auto-bind pass (WP-09a will call this after enumerating
 * new nodes; harmless when nothing is pending). */
void oc_drvfw_bind_pass(void);

/* ---- Framework debug logging ------------------------------------------
 * Off by default; `drv debug on` / `dev debug on` flip it.  When on,
 * every framework operation logs a line (probe results, bind/unbind,
 * PM transitions, hotplug dispatch). */
int  oc_drvfw_debug_enabled(void);
void oc_drvfw_set_debug(int on);

#ifdef __cplusplus
}
#endif

#endif /* OC_CORE_DRV_REGISTRY_H */
