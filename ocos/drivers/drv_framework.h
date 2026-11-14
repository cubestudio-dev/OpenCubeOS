/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS NAK - drivers/drv_framework: the OC_DRIVER_DECL
 * self-registration framework.
 *
 * NAK rule ("everything is extensible by adding a file"): a hardware
 * driver declares its identity IN THE FILE THAT IMPLEMENTS IT via
 * OC_DRIVER_DECL below.  The macro places a small const descriptor
 * into the dedicated .oc_drvs linker section; at boot
 * drv_framework_collect() walks the section boundaries (exported by
 * linker.ld) and fills the runtime table.
 *
 *   Adding a new driver = adding one .c file in the right
 *   ocos/drivers/<class>/ folder + one OC_DRIVER_DECL line.  No global
 *   table edit, no registration call site, no core file change.
 *
 * The owning service-side family managers (block / nic / snd / usb /
 * wireless / sdio) walk the collected table through
 * drv_framework_at() during their probe pass.
 */
#ifndef OC_DRV_FRAMEWORK_H
#define OC_DRV_FRAMEWORK_H

#include "core_registry.h"   /* OC_DECL_UID (kernel-core uid macro) */

#ifdef __cplusplus
extern "C" {
#endif

/* Driver identity.
 * family : "block", "nic", "snd", "usb", "wireless", "sdio", ... -
 *          selects the driver manager that owns the declaration (one
 *          manager per family; the managers live in the matching
 *          services/ folder).
 * ops    : family-specific ops table (drv_storage_ops_t, ...).  The
 *          manager stores the pointer; probe/init callbacks inside the
 *          ops table do the hardware bring-up when the manager walks
 *          the section.
 * name   : stable driver identity string ("ata", "nvme", "e1000", ...).
 */
typedef struct {
    const char *family;
    const void *ops;        /* const drv_*_ops_t* (typed per family) */
    const char *name;
    const char *desc;
} oc_driver_decl_t;

/* Two-level expansion so __COUNTER__ is evaluated BEFORE the token
 * paste - defined once in core_registry.h (kernel core) and shared
 * by every declaration framework. */

#define OC_DRIVER_DECL(fam, nm, opsp, dsc) \
    static const oc_driver_decl_t OC_DECL_UID(__ocdv_) \
    __attribute__((used, section(".oc_drvs"))) = { (fam), (opsp), (nm), (dsc) }

/* ---- Boot-time collection ------------------------------------------
 * Walkes the .oc_drvs linker section once, early in kmain.  Section
 * boundaries are provided by linker.ld (__oc_drvs_start/_end).
 * Reports the count through drv_framework_count() and logs the
 * collection notice on the boot console.
 */
void drv_framework_collect(void);

int  drv_framework_count(void);
const oc_driver_decl_t *drv_framework_at(int i);

#ifdef __cplusplus
}
#endif

#endif /* OC_DRV_FRAMEWORK_H */
