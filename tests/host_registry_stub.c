/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - host link stub for tests that link services/*/
/* *_svc.c files containing OC_SERVICE_DECL probe paths.
 *
 * The v2.0.0 declaration registry (core/core_registry.c) walks linker
 * section boundaries (__oc_*_start/__oc_*_end) that only exist in the
 * kernel image (linker.ld).  A host binary has no such sections, so
 * this stub defines the boundary pairs as EMPTY arrays: the real
 * registry code links and runs unchanged, and its walks simply see
 * zero declarations.  Tests that need real declarations still must
 * run in QEMU.  3.0.0-WP03: the .oc_cmds command section died with
 * the oc> shell (that boundary pair is gone). */
#include "../../core/core_registry.h"

/* lib_log_info shim - same observable behaviour as the kernel path
 * (core/lib/lib_log.c): the message goes to the console capture; the
 * ring/klog mirrors are kernel-only and not under test here. */
extern void screen_console_puts(const char *s);
void lib_log_info(const char *s) {
    screen_console_puts("    ");
    screen_console_puts(s);
    screen_console_puts("\n");
}

const oc_driver_decl_t     __oc_drvs_start[1] = {{0, 0, 0, 0}};
const oc_driver_decl_t     __oc_drvs_end[1]   = {{0, 0, 0, 0}};
const oc_service_decl_t    __oc_svcs_start[1] = {{0, 0, 0, 0}};
const oc_service_decl_t    __oc_svcs_end[1]   = {{0, 0, 0, 0}};
const oc_l1_app_decl_t     __oc_apps_start[1] = {{0, 0, 0, 0, 0}};
const oc_l1_app_decl_t     __oc_apps_end[1]   = {{0, 0, 0, 0, 0}};
