/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP08 - drivers/framework: deterministic init order.
 *
 * The metadata drivers are initialized in an order COMPUTED from
 * their dependency declarations (topological sort, Kahn's algorithm
 * with a sorted ready set).  Hard rules this module guarantees:
 *
 *   1. A driver runs strictly after every driver it depends on.
 *   2. A dependency cycle is DETECTED and REPORTED (both members are
 *      isolated and skipped) - never a silent livelock.
 *   3. A dependency naming an unknown driver is DETECTED and
 *      REPORTED (the dependent is isolated and skipped).
 *   4. Among dependency-free drivers the tie-break is
 *      (prio_hint, name) - NEVER the section order, so the sequence
 *      is byte-identical across rebuilds and reboots whatever the
 *      linker did.  Legacy OC_DRIVER_DECL drivers are outside this
 *      ordering (their sequence is the kmain family-manager call
 *      order, unchanged). */
#ifndef OC_CORE_DRV_ORDER_H
#define OC_CORE_DRV_ORDER_H

#include "types.h"
#include "core_drv_meta.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OC_DRV_ORDER_MAX 128        /* metadata drivers supported       */

/* Compute the order over the collected .oc_drvi registry.
 * Call once after core_drv_meta_collect().  Results:
 *   order[i]                - the i-th driver to initialize
 *   order_n                 - how many (the rest were isolated)
 *   isolated[i]/isolated_n  - drivers left out (cycle / missing dep)
 *   The arrays live in this module (single boot-time computation). */
void core_drv_order_compute(void);

const oc_driver_info_t *core_drv_order_at(int i);   /* 0 out of range */
int  core_drv_order_count(void);
const oc_driver_info_t *core_drv_order_isolated_at(int i);
int  core_drv_order_isolated_count(void);

/* Why an isolated driver was left out (for the report/diag render). */
const char *core_drv_order_isolate_reason(const oc_driver_info_t *drv);

#ifdef __cplusplus
}
#endif

#endif /* OC_CORE_DRV_ORDER_H */
