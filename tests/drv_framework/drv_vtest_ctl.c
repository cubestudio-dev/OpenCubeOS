/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP08 - VERIFICATION VIRTUAL DRIVER (tests-only).
 *
 *   *** NOT A KERNEL DRIVER.  NOT HARDWARE. ***
 *
 * vtest-ctl is the device-side half: it DECLARES a dependency on
 * vtest-bus (topological order proof), matches the "vtest-ctl"
 * nodes the virtual bus enumerates (auto-bind + multi-instance
 * proof), and carries suspend/resume callbacks with a D0/D3
 * declaration (power-hook proof).  It registers and unregisters
 * NOTHING itself - the owning bus manages the nodes. */
#include "core_drv_meta.h"
#include "core_devtree.h"
#include "core_lib_string.h"
#include "core_lib_log.h"

static int vctl_probe(oc_devnode_t *node)
{
    if (strcmp(node->type, "vtest-ctl") != 0) return -22;
    node->priv = (void *)0xC7;   /* "device online" marker */
    return 0;
}

static void vctl_remove(oc_devnode_t *node)
{
    node->priv = 0;
}

static int vctl_suspend(oc_devnode_t *node, int st)
{
    char line[64];
    char n[12];
    strcpy(line, "vtest-ctl: suspend ");
    strcat(line, node->name);
    strcat(line, " -> D");
    u64_to_str((u64)st, n); strcat(line, n);
    lib_log_info(line);
    return 0;
}

static int vctl_resume(oc_devnode_t *node, int from)
{
    char line[64];
    char n[12];
    strcpy(line, "vtest-ctl: resume ");
    strcat(line, node->name);
    strcat(line, " from D");
    u64_to_str((u64)from, n); strcat(line, n);
    lib_log_info(line);
    return 0;
}

static const char *const g_deps[]  = { "vtest-bus", 0 };
static const char *const g_types[] = { "vtest-ctl", 0 };

OC_DRIVER_INFO_DECL(
    .name    = "vtest-ctl",
    .version = "1.0",
    .author  = "cubestudio-dev",
    .license = "Apache-2.0",
    .depends = g_deps,
    .types   = g_types,
    .dstates = OC_DRV_D_STATE(OC_DRV_D0) | OC_DRV_D_STATE(OC_DRV_D3),
    .probe   = vctl_probe,
    .remove  = vctl_remove,
    .suspend = vctl_suspend,
    .resume  = vctl_resume,
    .desc    = "verification virtual device (tests/drv_framework, not hw)");
