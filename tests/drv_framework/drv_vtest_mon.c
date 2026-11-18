/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP08 - VERIFICATION VIRTUAL DRIVER (tests-only).
 *
 *   *** NOT A KERNEL DRIVER.  NOT HARDWARE. ***
 *
 * vtest-mon is the subscriber half of the hotplug proof: it
 * subscribes (type filter "vtest-ctl") during init and logs + counts
 * everything the queue delivers.  It binds to no node on purpose -
 * a pure observer; `drv info vtest-mon` shows the delivered-event
 * count through the per-owner subscription stats. */
#include "core_drv_meta.h"
#include "core_hotplug.h"
#include "core_lib_string.h"
#include "core_lib_log.h"

static int s_sub_id = -1;
static int s_inserts, s_removes;

static void vmon_cb(const oc_hotplug_ev_t *ev)
{
    char line[96];
    strcpy(line, "vtest-mon: event ");
    strcat(line, (ev->type == OC_HPEV_INSERT) ? "insert " : "remove ");
    strcat(line, ev->node_name);
    strcat(line, " (type ");
    strcat(line, ev->node_type);
    strcat(line, ") from ");
    strcat(line, ev->source);
    lib_log_info(line);
    if (ev->type == OC_HPEV_INSERT) s_inserts++;
    else                            s_removes++;
}

static int vmon_init(void)
{
    if (s_sub_id >= 0) return 0;            /* reload: already subscribed */
    s_sub_id = oc_hotplug_subscribe("vtest-ctl", 0, vmon_cb, "vtest-mon");
    return (s_sub_id >= 0) ? 0 : -53;
}

OC_DRIVER_INFO_DECL(
    .name    = "vtest-mon",
    .version = "1.0",
    .author  = "cubestudio-dev",
    .license = "Apache-2.0",
    .init    = vmon_init,
    .desc    = "verification hotplug observer (tests/drv_framework, not hw)");
