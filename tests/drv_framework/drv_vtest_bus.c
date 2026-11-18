/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP08 - VERIFICATION VIRTUAL DRIVER (tests-only).
 *
 *   *** NOT A KERNEL DRIVER.  NOT HARDWARE. ***
 *
 * vtest-bus is the framework-chain stand-in for the WP-09a bus-driver
 * ROLE: it enumerates (registers a bus node + two child devices with
 * firmware-style resources), posts hotplug INSERT events, and on
 * removal cascades the children (notify -> detach -> unregister) -
 * exercising every framework interface end to end with zero real
 * hardware and zero QEMU dependency in the framework itself.
 * It lives in tests/drv_framework/ by the WP-08 rule and ships in
 * every image as the live canary of the chain it verifies. */
#include "core_drv_meta.h"
#include "core_devtree.h"
#include "core_drv_registry.h"
#include "core_hotplug.h"
#include "core_lib_string.h"
#include "core_lib_log.h"

static oc_devnode_t *s_bus_node;

static int vbus_init(void)
{
    s_bus_node = oc_devtree_register("vtest-bus0", "vtest-bus", 0);
    if (!s_bus_node) return -52;
    oc_devnode_t *c0 = oc_devtree_register("vtest-ctl0", "vtest-ctl", s_bus_node);
    oc_devnode_t *c1 = oc_devtree_register("vtest-ctl1", "vtest-ctl", s_bus_node);
    if (!c0 || !c1) return -52;
    oc_devtree_add_res(c0, OC_DEVRES_MMIO, 0xFEE00000ull, 0x1000);
    oc_devtree_add_res(c0, OC_DEVRES_IRQ, 11, 1);
    oc_devtree_add_res(c1, OC_DEVRES_MMIO, 0xFEE01000ull, 0x1000);
    oc_devtree_add_res(c1, OC_DEVRES_IRQ, 11, 1);
    /* Bus enumeration: the children become hotplug-visible. */
    oc_hotplug_post(OC_HPEV_INSERT, c0->id, "vtest-bus");
    oc_hotplug_post(OC_HPEV_INSERT, c1->id, "vtest-bus");
    return 0;
}

static int vbus_probe(oc_devnode_t *node)
{
    if (strcmp(node->type, "vtest-bus") != 0) return -22;
    node->priv = (void *)1;      /* "bus powered on" marker */
    return 0;
}

static void vbus_remove(oc_devnode_t *node)
{
    /* Children first: notify (the event snapshot reads the live
     * node), then cascade unbind + unregister.  detach_subtree()
     * compacts the child list, so slot 0 is always the next one. */
    while (node->nchildren > 0) {
        oc_devnode_t *c = node->children[0];
        oc_hotplug_post(OC_HPEV_REMOVE, c->id, "vtest-bus");
        (void)oc_drvfw_detach_subtree(c);
    }
    oc_hotplug_post(OC_HPEV_REMOVE, node->id, "vtest-bus");
    /* node->bound was cleared by the framework BEFORE this call, so
     * unregister() accepts the node; the bus owns it entirely. */
    (void)oc_devtree_unregister(node);
    s_bus_node = 0;
}

static int vbus_suspend(oc_devnode_t *node, int st)
{
    char line[64];
    char n[12];
    strcpy(line, "vtest-bus: suspend ");
    strcat(line, node->name);
    strcat(line, " -> D");
    u64_to_str((u64)st, n); strcat(line, n);
    lib_log_info(line);
    return 0;
}

static int vbus_resume(oc_devnode_t *node, int from)
{
    char line[64];
    char n[12];
    strcpy(line, "vtest-bus: resume ");
    strcat(line, node->name);
    strcat(line, " from D");
    u64_to_str((u64)from, n); strcat(line, n);
    lib_log_info(line);
    return 0;
}

static const char *const g_types[] = { "vtest-bus", 0 };

OC_DRIVER_INFO_DECL(
    .name    = "vtest-bus",
    .version = "1.0",
    .author  = "cubestudio-dev",
    .license = "Apache-2.0",
    .types   = g_types,
    .dstates = OC_DRV_D_STATE(OC_DRV_D0) | OC_DRV_D_STATE(OC_DRV_D1)
             | OC_DRV_D_STATE(OC_DRV_D3),
    .init    = vbus_init,
    .probe   = vbus_probe,
    .remove  = vbus_remove,
    .suspend = vbus_suspend,
    .resume  = vbus_resume,
    .desc    = "verification virtual bus (tests/drv_framework, not hw)");
