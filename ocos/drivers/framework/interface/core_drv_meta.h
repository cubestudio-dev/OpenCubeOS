/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP08 - drivers/framework: driver metadata.
 *
 * The NEW driver description structure - name/version/author/license,
 * dependency list, init priority hint, supported D-states and the
 * probe/remove/suspend/resume callbacks - plus OC_DRIVER_INFO_DECL,
 * the registration macro that places the descriptor into the
 * .oc_drvi linker section.
 *
 * COEXISTENCE CONTRACT with the legacy OC_DRIVER_DECL
 * (ocos/drivers/drv_framework.h): the old macro keeps its exact
 * signature and behavior; existing drivers never change.  The old
 * path carries family ops tables and is walked by the family
 * managers (block/nic/snd/usb/...); the new path carries full
 * metadata and is walked by THIS framework (deterministic order,
 * failure isolation, device-tree binding, PM hooks).  Both sections
 * are collected at boot - two registration paths, one system.
 *
 * Adding a metadata driver = adding one .c file with one
 * OC_DRIVER_INFO_DECL line.  No global table edit, no registration
 * call site (the Linker Section collection rule). */
#ifndef OC_CORE_DRV_META_H
#define OC_CORE_DRV_META_H

#include "types.h"
#include "core_registry.h"      /* OC_DECL_UID */
#include "core_devtree.h"       /* oc_devnode_t */

#ifdef __cplusplus
extern "C" {
#endif

struct oc_devnode;

/* ---- D-states (device power states, ACPI-style) ----------------------
 * A driver declares the states it can suspend to / resume from via a
 * bitmask of OC_DRV_D_STATE(x).  D0 = fully operational; D3 = off.
 * The framework (core_pm_hook.c) only invokes suspend(node, Dk) when
 * the driver declared Dk.  System-level S-states and the power POLICY
 * are NOT here (WP-22 owns those). */
#define OC_DRV_D0 0
#define OC_DRV_D1 1
#define OC_DRV_D2 2
#define OC_DRV_D3 3
#define OC_DRV_D_STATE(x) (1u << (x))
#define OC_DRV_D_SUPPORTS(mask, x) (((mask) & OC_DRV_D_STATE(x)) != 0)

/* ---- Driver description structure ------------------------------------ */
typedef struct oc_driver_info {
    const char *name;           /* stable driver identity ("vtest-bus") */
    const char *version;        /* free-form version string             */
    const char *author;         /* free-form author string              */
    const char *license;        /* "Apache-2.0", "GPL-2.0", ...         */
    const char *const *depends; /* NULL-terminated driver-name list; a
                                 * dependency must itself be a metadata
                                 * driver (enforced at order time)      */
    const char *const *types;   /* NULL-terminated compatible device
                                 * types; the auto-bind pass probes a
                                 * driver against every unbound node of
                                 * a matching type                     */
    int  prio_hint;             /* tie-break hint; lower runs earlier
                                 * among dependency-free drivers (the
                                 * name sort stays the final tie-break,
                                 * so the order NEVER depends on link
                                 * order)                               */
    u32  dstates;               /* OC_DRV_D_STATE(x) bitmask            */
    int  (*init)(void);         /* driver bring-up, no node yet; may
                                 * register device nodes               */
    int  (*probe)(struct oc_devnode *node);   /* bind to a node; 0 ok,
                                 * negative errno on failure            */
    void (*remove)(struct oc_devnode *node);  /* unbind; the driver owns
                                 * its node resource release            */
    int  (*suspend)(struct oc_devnode *node, int target_dstate);
    int  (*resume)(struct oc_devnode *node, int from_dstate);
    const char *desc;
} oc_driver_info_t;

/* ---- Registration macro ----------------------------------------------
 * Usage (designated initializers keep it auditable):
 *   static const char *const g_dep[] = { "vtest-bus", 0 };
 *   static const char *const g_typ[] = { "vtest-ctl", 0 };
 *   OC_DRIVER_INFO_DECL(
 *       .name = "vtest-ctl", .version = "1.0",
 *       .author = "cubestudio-dev", .license = "Apache-2.0",
 *       .depends = g_dep, .types = g_typ,
 *       .dstates = OC_DRV_D_STATE(OC_DRV_D0) | OC_DRV_D_STATE(OC_DRV_D3),
 *       .init = ctl_init, .probe = ctl_probe, .remove = ctl_remove,
 *       .suspend = ctl_suspend, .resume = ctl_resume,
 *       .desc = "virtual test device");
 * Every field may be 0/NULL except name (the collector skips a
 * nameless record; probe needs types to ever be called).
 *
 * aligned(8) is LOAD-BEARING: gcc gives a second declaration in the
 * same file a 32-byte alignment hint, which pads the section (a
 * 104-byte struct followed by 24 filler bytes) and desyncs the
 * registry walk (live-caught on vtest-cyc-a/b: 6 decls collected
 * as 2).  Forcing 8-byte alignment keeps every entry back-to-back
 * inside one object, matching the SUBALIGN(8) the linker script
 * already applies BETWEEN objects. */
#define OC_DRIVER_INFO_DECL(...) \
    static const oc_driver_info_t OC_DECL_UID(__ocdi_) \
    __attribute__((used, section(".oc_drvi"), aligned(8))) = { __VA_ARGS__ }

/* ---- Boot-time collection --------------------------------------------
 * Walks the .oc_drvi section once (boundaries exported by linker.ld)
 * into the framework registry (core_drv_registry.c keeps the runtime
 * state).  Called by oc_drvfw_boot(); not by kmain directly. */
void core_drv_meta_collect(void);

int core_drv_meta_count(void);
const oc_driver_info_t *core_drv_meta_at(int i);   /* 0 when out of range */
const oc_driver_info_t *core_drv_meta_find(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* OC_CORE_DRV_META_H */
