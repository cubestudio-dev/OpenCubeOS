/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-tui-input-encoding-hardening 4.1
 * WP-runtime-hardening-and-experience-rebase 4.7 (CONFIG_TUI tri-state)
 * File: ocos/services/l1/src/l1_tui_entry_filter.c
 * Purpose: the hidden-entry set for the registry presentation
 *          surfaces (see interface/svc_l1_entry_filter.h).
 *
 * WP-runtime-hardening 4.7 - the table follows the CONFIG_TUI build
 * knob through OC_CONFIG_TUI_HIDDEN (ocos/kernel/generated/
 * config_gen.h, written by the Makefile):
 *
 *   CONFIG_TUI=y : EMPTY set - the desktop AND all 8 sub-launchers
 *                  are listed (help / help -a / help -w) and directly
 *                  runnable.
 *   CONFIG_TUI=h : the FULL set - all 9 tui* entries are unlisted AND
 *                  direct invocation is intercepted at the execve
 *                  resolution funnel (core_userprog_lookup +
 *                  sys_execve, services/sysapi/svc_sysapi_syscall.c)
 *                  with MSG_TUI_ENTRY_BLOCKED.
 *   CONFIG_TUI=n : EMPTY set - nothing to hide: the 9 entries are not
 *                  built at all (tools/gen_userprogs.py skips the
 *                  folders; the L0 bodies are filtered out).
 *
 * k_hidden[] below is the single fact source for the audit tooling
 * (tools/docs/enum_apps.sh and the tools/audit scripts parse this
 * table); keep it a flat, quoted, one-name-per-line list - the #if
 * guard wraps the whole block, never a single name.
 */
#include "svc_l1_entry_filter.h"
#include "svc_framework.h"
#include "core_usermode.h"
#include "core_lib_string.h"
#include "config_gen.h"   /* WP-runtime-hardening 4.7: OC_CONFIG_TUI_HIDDEN */

#if OC_CONFIG_TUI_HIDDEN
/* CONFIG_TUI=h: the full hidden set (the desktop + its 8 launchers).
 * Registered and compiled, unlisted on every presentation surface,
 * and blocked at the direct-invocation funnel. */
static const char *const k_hidden[] = {
    "tui",
    "tui-test",
    "tui-hw",
    "tui-net",
    "tui-sysinfo",
    "tui-time",
    "tui-drv",
    "tui-sys",
    "tui-ext",
};
#endif

int l1_entry_hidden(const char *name)
{
    if (!name)
        return 0;
#if OC_CONFIG_TUI_HIDDEN
    for (u32 i = 0; i < sizeof(k_hidden) / sizeof(k_hidden[0]); i++)
        if (strcmp(name, k_hidden[i]) == 0)
            return 1;
#endif
    /* CONFIG_TUI=y/n: the empty set - every registry entry (y) resp.
     * whatever gen_userprogs.py registered (n) is visible. */
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
