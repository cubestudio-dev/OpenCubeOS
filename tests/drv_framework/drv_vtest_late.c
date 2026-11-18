/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP08 - VERIFICATION VIRTUAL DRIVER (tests-only).
 *
 *   *** NOT A KERNEL DRIVER.  NOT HARDWARE. ***
 *
 * vtest-late is the ZERO-GLOBAL-TABLE-EDIT proof: it was added AFTER
 * the framework, the Makefile wildcard and every other vtest driver
 * had already landed - one new file, nothing else touched - and it
 * registers, orders (after its declared dependency vtest-ctl),
 * probes and binds through the pure file-addition path that every
 * future driver will use. */
#include "core_drv_meta.h"
#include "core_devtree.h"
#include "core_lib_string.h"

static int vlate_init(void)
{
    oc_devnode_t *n = oc_devtree_register("vtest-late0", "vtest-late", 0);
    return n ? 0 : -56;
}

static int vlate_probe(oc_devnode_t *node)
{
    if (strcmp(node->type, "vtest-late") != 0) return -22;
    node->priv = (void *)0x1A;
    return 0;
}

static void vlate_remove(oc_devnode_t *node)
{
    node->priv = 0;
}

static const char *const g_deps[]  = { "vtest-ctl", 0 };
static const char *const g_types[] = { "vtest-late", 0 };

OC_DRIVER_INFO_DECL(
    .name    = "vtest-late",
    .version = "1.0",
    .author  = "cubestudio-dev",
    .license = "Apache-2.0",
    .depends = g_deps,
    .types   = g_types,
    .init    = vlate_init,
    .probe   = vlate_probe,
    .remove  = vlate_remove,
    .desc    = "verification late-addition (tests/drv_framework, not hw)");
