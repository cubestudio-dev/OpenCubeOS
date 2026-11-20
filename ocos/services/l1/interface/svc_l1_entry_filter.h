/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-tui-input-encoding-hardening 4.1
 * File: ocos/services/l1/interface/svc_l1_entry_filter.h
 * Purpose: the hidden-entry contract for the registry presentation
 *          surfaces (help / help -a / help -w listings and the TUI
 *          desktop tree).
 *
 * The tui-* sub-launchers (tui-test / tui-hw / tui-net / tui-sysinfo /
 * tui-time / tui-drv / tui-sys / tui-ext) stay REGISTERED and directly
 * executable (execve keeps resolving every name); only the LISTING
 * surfaces skip them.  `tui` itself is THE single desktop entry and
 * stays visible.
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
