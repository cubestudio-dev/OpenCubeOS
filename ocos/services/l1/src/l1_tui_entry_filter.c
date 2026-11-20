/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-tui-input-encoding-hardening 4.1
 * File: ocos/services/l1/src/l1_tui_entry_filter.c
 * Purpose: the hidden-entry set for the registry presentation
 *          surfaces (see interface/svc_l1_entry_filter.h).
 *
 * Registry entries are NOT removed and their registration is NOT
 * touched: execve keeps resolving every name below (the preserved
 * direct-invocation path).  Only the listing surfaces (the L1SVC
 * "l1" registry renderings behind help / help -a / help -w) and the
 * desktop tree entry for the lab skip them.  `tui` is the one visible
 * desktop entry point and is NOT in the set.
 *
 * k_hidden[] below is the single fact source for the audit tooling
 * (tools/docs/enum_apps.sh and the tools/audit scripts parse this
 * table); keep it a flat, singly-quoted, one-name-per-line list.
 */
#include "svc_l1_entry_filter.h"
#include "svc_framework.h"
#include "core_usermode.h"
#include "core_lib_string.h"

static const char *const k_hidden[] = {
    "tui-test",
    "tui-hw",
    "tui-net",
    "tui-sysinfo",
    "tui-time",
    "tui-drv",
    "tui-sys",
    "tui-ext",
};

int l1_entry_hidden(const char *name)
{
    if (!name)
        return 0;
    for (u32 i = 0; i < sizeof(k_hidden) / sizeof(k_hidden[0]); i++)
        if (strcmp(name, k_hidden[i]) == 0)
            return 1;
    return 0;
}

uint32_t l1_registry_visible_count(void)
{
    u64 n = 0;
    for (u64 i = 0; i < OC_USERPROGS_COUNT; i++)
        if (!l1_entry_hidden(OC_USERPROGS[i].name))
            n++;
    return (uint32_t)n;
}

uint32_t l1_registry_hidden_count(void)
{
    u64 n = 0;
    for (u64 i = 0; i < OC_USERPROGS_COUNT; i++)
        if (l1_entry_hidden(OC_USERPROGS[i].name))
            n++;
    return (uint32_t)n;
}
