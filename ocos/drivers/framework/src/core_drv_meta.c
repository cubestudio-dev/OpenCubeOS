/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP08 - drivers/framework: driver metadata collect.
 *
 * Boot-time walk of the .oc_drvi linker section (OC_DRIVER_INFO_DECL
 * records).  This is the NEW registration path; it never touches the
 * legacy .oc_drvs walk (drv_framework_collect) - both run, both
 * stay (see interface/core_drv_meta.h for the coexistence contract). */
#include "core_drv_meta.h"
#include "core_lib_string.h"

extern const oc_driver_info_t __oc_drvi_start[], __oc_drvi_end[];

static const oc_driver_info_t *s_info[256];
static int s_ninfo;

void core_drv_meta_collect(void)
{
    s_ninfo = 0;
    for (const oc_driver_info_t *p = __oc_drvi_start; p < __oc_drvi_end; p++) {
        if (p->name && s_ninfo < (int)(sizeof(s_info) / sizeof(s_info[0])))
            s_info[s_ninfo++] = p;
    }
}

int core_drv_meta_count(void) { return s_ninfo; }

const oc_driver_info_t *core_drv_meta_at(int i)
{
    return (i >= 0 && i < s_ninfo) ? s_info[i] : 0;
}

const oc_driver_info_t *core_drv_meta_find(const char *name)
{
    if (!name || !name[0]) return 0;
    for (int i = 0; i < s_ninfo; i++)
        if (strcmp(s_info[i]->name, name) == 0) return s_info[i];
    return 0;
}
