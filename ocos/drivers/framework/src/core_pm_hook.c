/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP08 - drivers/framework: driver power hooks.
 *
 * The tree-walking side of suspend/resume (contracts in
 * interface/core_pm_hook.h).  Suspend order: children before parents
 * (a leaf powers down first); resume order: the exact reverse.
 * Drivers that never declared the target state are skipped; callback
 * failures are logged and counted, the walk always completes.  No
 * S-state machine, no policy - WP-22 calls oc_pm_notify().
 *
 * PM-managed node := bound to a driver that provides BOTH the
 * suspend and the resume callback (a half pair is ignored). */
#include "core_pm_hook.h"
#include "core_drv_meta.h"
#include "core_devtree.h"
#include "core_drv_registry.h"
#include "core_lib_string.h"
#include "core_lib_log.h"

/* Post-order collection of the bound, PM-managed nodes. */
#define OC_PM_WALK_MAX 128

typedef struct {
    oc_devnode_t *nodes[OC_PM_WALK_MAX];
    int n;
    int target;            /* D-state of interest, or -1 = collect mode */
    int count;             /* nodes currently in `target` (query mode) */
} pm_walk_ctx_t;

static void pm_collect_rec(oc_devnode_t *node, pm_walk_ctx_t *ctx)
{
    /* Depth first: the children land in the array BEFORE this node. */
    for (int i = 0; i < node->nchildren; i++)
        pm_collect_rec(node->children[i], ctx);

    if (!node->bound || !node->bound->suspend || !node->bound->resume)
        return;
    if (ctx->target >= 0) {
        if (node->dstate == ctx->target) ctx->count++;
        return;                                  /* query mode stops here */
    }
    if (ctx->n < OC_PM_WALK_MAX)
        ctx->nodes[ctx->n++] = node;
}

static void log_pm(const char *what, const char *node, int st, const char *res)
{
    char line[96];
    char n[12];
    strcpy(line, "pm: ");
    strcat(line, what);
    strcat(line, " ");
    strcat(line, node);
    strcat(line, " -> D");
    u64_to_str((u64)st, n);
    strcat(line, n);
    strcat(line, " ");
    strcat(line, res);
    lib_log_info(line);
}

int oc_pm_notify(int dstate)
{
    if (dstate < OC_DRV_D0 || dstate > OC_DRV_D3) return -1;

    pm_walk_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.target = -1;   /* COLLECT mode (0 would read as query-for-D0
                        * and silently skip every node - live-caught by
                        * the host unit test, fixed here) */
    pm_collect_rec(oc_devtree_root(), &ctx);

    if (dstate == OC_DRV_D0) {
        /* Resume pass: reverse post-order (parents first). */
        int fails = 0;
        for (int i = ctx.n - 1; i >= 0; i--) {
            oc_devnode_t *n = ctx.nodes[i];
            if (n->dstate == OC_DRV_D0) continue;      /* already up */
            int rc = n->bound->resume(n, n->dstate);
            if (rc == 0) {
                n->dstate = OC_DRV_D0;
                if (oc_drvfw_debug_enabled())
                    log_pm("resume", n->name, OC_DRV_D0, "ok");
            } else {
                fails++;
                log_pm("resume", n->name, OC_DRV_D0, "FAILED (state kept)");
            }
        }
        return fails ? -fails : 0;
    }

    /* Suspend pass: post-order (children first). */
    int fails = 0;
    for (int i = 0; i < ctx.n; i++) {
        oc_devnode_t *n = ctx.nodes[i];
        const oc_driver_info_t *d = n->bound;
        if (n->dstate == dstate) continue;             /* already there */
        if (!OC_DRV_D_SUPPORTS(d->dstates, dstate)) continue;
        int rc = d->suspend(n, dstate);
        if (rc == 0) {
            n->dstate = dstate;
            if (oc_drvfw_debug_enabled())
                log_pm("suspend", n->name, dstate, "ok");
        } else {
            fails++;
            log_pm("suspend", n->name, dstate, "FAILED (state kept)");
        }
    }
    return fails ? -fails : 0;
}

int oc_pm_nodes_in_dstate(int dstate)
{
    pm_walk_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.target = dstate;
    pm_collect_rec(oc_devtree_root(), &ctx);
    return ctx.count;
}
