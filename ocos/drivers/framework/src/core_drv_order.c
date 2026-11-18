/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP08 - drivers/framework: deterministic init order.
 *
 * Kahn's algorithm over the collected .oc_drvi records with a SORTED
 * ready set: among all drivers whose dependencies are satisfied, the
 * one with the smallest (prio_hint, name) key runs next.  The result
 * is a pure function of the declarations - the linker section order
 * never participates, so the sequence is identical across rebuilds,
 * reboots and link-order shuffles.
 *
 * Cycles and unknown dependencies are isolated (reported, skipped),
 * never silently looped: every driver starts in the "waiting" set and
 * anything still waiting when the ready set empties is isolated with
 * its reason. */
#include "core_drv_order.h"
#include "core_lib_string.h"

static const oc_driver_info_t *s_order[OC_DRV_ORDER_MAX];
static int s_norder;
static const oc_driver_info_t *s_isolated[OC_DRV_ORDER_MAX];
static int s_nisolated;
static const char *s_reason[OC_DRV_ORDER_MAX];   /* parallel to s_isolated */

/* Does `a` sort before `b` under the deterministic key? */
static int key_less(const oc_driver_info_t *a, const oc_driver_info_t *b)
{
    if (a->prio_hint != b->prio_hint) return a->prio_hint < b->prio_hint;
    return strcmp(a->name, b->name) < 0;
}

void core_drv_order_compute(void)
{
    s_norder = 0;
    s_nisolated = 0;

    int n = core_drv_meta_count();
    if (n > OC_DRV_ORDER_MAX) n = OC_DRV_ORDER_MAX;

    /* done[i]   : already emitted to the order list
     * blocked[i]: isolated (cycle / missing dep) - never emitted
     * pending_deps[i]: how many of deps[i] are still not satisfied */
    static int done[OC_DRV_ORDER_MAX];
    static int blocked[OC_DRV_ORDER_MAX];
    static const char *deps[OC_DRV_ORDER_MAX][8];  /* resolved pointers */
    static int deps_n[OC_DRV_ORDER_MAX];

    for (int i = 0; i < n; i++) {
        done[i] = 0;
        blocked[i] = 0;
        deps_n[i] = 0;
    }

    /* Resolve dependency names to registry indices.  An unknown name
     * isolates the dependent immediately. */
    for (int i = 0; i < n; i++) {
        const oc_driver_info_t *d = core_drv_meta_at(i);
        if (!d->depends) continue;
        for (const char *const *p = d->depends; *p; p++) {
            int found = -1;
            for (int j = 0; j < n; j++)
                if (strcmp(core_drv_meta_at(j)->name, *p) == 0) { found = j; break; }
            if (found < 0) {
                blocked[i] = 1;
                s_isolated[s_nisolated] = d;
                s_reason[s_nisolated] = *p;      /* the missing name */
                s_nisolated++;
                break;                           /* one reason is enough */
            }
            if (deps_n[i] < 8) deps[i][deps_n[i]++] = core_drv_meta_at(found)->name;
        }
    }

    /* Kahn with a sorted ready set. */
    for (;;) {
        int best = -1;
        for (int i = 0; i < n; i++) {
            if (done[i] || blocked[i]) continue;
            int ready = 1;
            for (int k = 0; k < deps_n[i] && ready; k++) {
                const char *dep = deps[i][k];
                int sat = 0;
                for (int j = 0; j < s_norder; j++)
                    if (strcmp(s_order[j]->name, dep) == 0) { sat = 1; break; }
                if (!sat) ready = 0;
            }
            if (!ready) continue;
            if (best < 0 || key_less(core_drv_meta_at(i), core_drv_meta_at(best)))
                best = i;
        }
        if (best < 0) break;                     /* ready set empty */
        done[best] = 1;
        s_order[s_norder++] = core_drv_meta_at(best);
    }

    /* Whatever never became ready is in a cycle. */
    for (int i = 0; i < n; i++) {
        if (done[i] || blocked[i]) continue;
        s_isolated[s_nisolated] = core_drv_meta_at(i);
        s_reason[s_nisolated] = "cycle";        /* literal marker */
        s_nisolated++;
    }
}

const oc_driver_info_t *core_drv_order_at(int i)
{
    return (i >= 0 && i < s_norder) ? s_order[i] : 0;
}

int core_drv_order_count(void) { return s_norder; }

const oc_driver_info_t *core_drv_order_isolated_at(int i)
{
    return (i >= 0 && i < s_nisolated) ? s_isolated[i] : 0;
}

int core_drv_order_isolated_count(void) { return s_nisolated; }

const char *core_drv_order_isolate_reason(const oc_driver_info_t *drv)
{
    for (int i = 0; i < s_nisolated; i++)
        if (s_isolated[i] == drv) return s_reason[i];
    return 0;
}
