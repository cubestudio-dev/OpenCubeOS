/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP08 - drivers/framework: device tree walkers.
 *
 * The iteration faces of the device tree: pre-order DFS over the
 * whole tree (with depth), by-type scan, and direct-children scan.
 * A non-zero callback return stops the walk and propagates out.
 * Lock-free by contract (see core_devtree_node.c: no shape mutation
 * from inside a callback). */
#include "core_devtree.h"
#include "core_lib_string.h"

static int walk_rec(oc_devnode_t *node, int depth, oc_devtree_fn_t fn,
                    void *ctx)
{
    int rc = fn(node, depth, ctx);
    if (rc != 0) return rc;
    for (int i = 0; i < node->nchildren; i++) {
        rc = walk_rec(node->children[i], depth + 1, fn, ctx);
        if (rc != 0) return rc;
    }
    return 0;
}

int oc_devtree_walk(oc_devtree_fn_t fn, void *ctx)
{
    if (!fn) return -1;
    return walk_rec(oc_devtree_root(), 0, fn, ctx);
}

static int type_rec(oc_devnode_t *node, const char *type,
                    oc_devnode_fn_t fn, void *ctx)
{
    if (strcmp(node->type, type) == 0) {
        int rc = fn(node, ctx);
        if (rc != 0) return rc;
    }
    for (int i = 0; i < node->nchildren; i++) {
        int rc = type_rec(node->children[i], type, fn, ctx);
        if (rc != 0) return rc;
    }
    return 0;
}

int oc_devtree_for_each_type(const char *type, oc_devnode_fn_t fn,
                             void *ctx)
{
    if (!type || !type[0] || !fn) return -1;
    return type_rec(oc_devtree_root(), type, fn, ctx);
}

int oc_devtree_for_each_child(oc_devnode_t *parent, oc_devnode_fn_t fn,
                              void *ctx)
{
    if (!parent || !parent->live || !fn) return -1;
    for (int i = 0; i < parent->nchildren; i++) {
        int rc = fn(parent->children[i], ctx);
        if (rc != 0) return rc;
    }
    return 0;
}
