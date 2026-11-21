/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-tui-input-encoding-hardening 4.1
 * WP-runtime-hardening-and-experience-rebase 4.7 (CONFIG_TUI tri-state)
 * File: ocos/services/l1/interface/svc_l1_entry_filter.h
 * Purpose: the hidden-entry contract for the registry presentation
 *          surfaces (help / help -a / help -w listings and the TUI
 *          desktop tree).
 *
 * WP-runtime-hardening 4.7: the hidden set follows the CONFIG_TUI
 * build knob (the table is compiled under OC_CONFIG_TUI_HIDDEN):
 *   CONFIG_TUI=y - EMPTY set: every registered entry (incl. all 9
 *                  tui* commands) is listed and directly runnable.
 *   CONFIG_TUI=h - the FULL set (tui + its 8 sub-launchers): the tui*
 *                  entries stay REGISTERED but are unlisted on every
 *                  presentation surface AND blocked at the direct-
 *                  invocation funnel (core_userprog_lookup/sys_execve
 *                  print MSG_TUI_ENTRY_BLOCKED and fail not-found).
 *   CONFIG_TUI=n - EMPTY set: the 9 tui* entries are not built at
 *                  all (discovery skips the folders), nothing to hide.
 *
 * The k_hidden[] table in the implementation
 * (ocos/services/l1/src/l1_tui_entry_filter.c) is the single fact
 * source for the audit tooling - keep it a flat quoted C string list.
 */
#ifndef SVC_L1_ENTRY_FILTER_H
#define SVC_L1_ENTRY_FILTER_H

#include <stdint.h>

/* 1 when `name` is a hidden (registered-but-not-listed) command. */
int l1_entry_hidden(const char *name);

/* OC_USERPROGS_COUNT minus the hidden members actually present in the
 * registry: the number every registry LISTING surface must agree on. */
uint32_t l1_registry_visible_count(void);

/* The hidden-member count itself (audit reporting). */
uint32_t l1_registry_hidden_count(void);

#endif /* SVC_L1_ENTRY_FILTER_H */
