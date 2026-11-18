/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP08 - VERIFICATION VIRTUAL DRIVER (tests-only).
 *
 *   *** NOT A KERNEL DRIVER.  NOT HARDWARE. ***
 *
 * vtest-fail is the FAILURE-INJECTION canary: its init succeeds (so
 * the node exists and shows in the tree), but every probe returns
 * -55.  The boot must CONTINUE past it (isolation rule), the node
 * must stay visible as "not bound, last error -55", and `drv
 * reload vtest-fail` must retry, fail again and bump the failure
 * count - proving the ledger and the retry path without any risk
 * to the running system. */
#include "core_drv_meta.h"
#include "core_devtree.h"

static int vfail_init(void)
{
    /* Idempotent by design: a `drv reload` re-runs init while the
     * never-bound node from the previous round is still registered
     * (nothing unbinds it - the probe always fails), so reusing the
     * existing node is the correct owner behavior. */
    if (oc_devtree_find_name("vtest-fail0"))
        return 0;
    oc_devnode_t *n = oc_devtree_register("vtest-fail0", "vtest-fail", 0);
    return n ? 0 : -54;
}

static int vfail_probe(oc_devnode_t *node)
{
    (void)node;
    return -55;      /* deliberate failure injection - do not "fix" */
}

static const char *const g_types[] = { "vtest-fail", 0 };

OC_DRIVER_INFO_DECL(
    .name    = "vtest-fail",
    .version = "1.0",
    .author  = "cubestudio-dev",
    .license = "Apache-2.0",
    .types   = g_types,
    .init    = vfail_init,
    .probe   = vfail_probe,
    .desc    = "verification failure injector (tests/drv_framework, not hw)");
