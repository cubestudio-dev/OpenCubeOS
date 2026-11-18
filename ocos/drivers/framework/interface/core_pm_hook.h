/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP08 - drivers/framework: driver power hooks.
 *
 * Driver-level suspend/resume with D-state declarations.  The
 * framework side ONLY: it walks the device tree and invokes the
 * callbacks of bound drivers that declared the target state.
 * There is NO system-level S-state machine and NO policy here -
 * the "when/why" of a transition belongs to WP-22, which will call
 * oc_pm_notify() from the power service.
 *
 * Ordering: suspend runs children-first (deepest nodes power down
 * before their parents), resume runs the exact reverse (parents
 * come back first).  Callback errors are logged and collected -
 * the walk continues; the caller gets a failure count, the policy
 * decision (what to do about a failing device) stays with WP-22. */
#ifndef OC_CORE_PM_HOOK_H
#define OC_CORE_PM_HOOK_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Request a device-level power transition.
 *
 *   dstate == OC_DRV_D0  : resume pass - every bound node not already
 *                          in D0 gets resume(node, from = its current
 *                          state), parents first, then lands in D0.
 *   dstate == D1/D2/D3   : suspend pass - every bound node whose
 *                          driver DECLARED that state gets
 *                          suspend(node, target), children first, then
 *                          records the node's new state.
 *
 * Return: 0 = every invoked callback succeeded (nodes without a
 * matching declaration are skipped, not counted as failures);
 * negative = -(number of failed callbacks).  A node keeps the
 * target state only when its callback returned 0. */
int oc_pm_notify(int dstate);

/* Number of bound nodes currently holding each D-state (diagnostics). */
int oc_pm_nodes_in_dstate(int dstate);

#ifdef __cplusplus
}
#endif

#endif /* OC_CORE_PM_HOOK_H */
