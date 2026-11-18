/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP08 - drivers/framework: device tree nodes.
 *
 * Registration, unregistration, lookup and resources for the device
 * node model (see interface/core_devtree.h for the data layout and
 * the contracts).  Nodes are kzalloc'd; the root is a static in-image
 * record.  A single spinlock guards the shape mutations (register /
 * unregister); the walkers deliberately run lock-free under the
 * standing rule: never register or unregister from inside a walk
 * callback.  The framework itself obeys it - a bus driver enumerates
 * from init(), removal paths run outside any walk. */
#include "core_devtree.h"
#include "core_drv_meta.h"     /* OC_DRV_D0 for the initial node state */
#include "core_mm_heap.h"
#include "core_lib_string.h"
#include "core_sync.h"

/* ---- The root --------------------------------------------------------- */
static oc_devnode_t g_root = {
    .id = 1, .name = "root", .type = "root", .live = 1,
};

static spinlock_t g_tree_lock;   /* zero-initialized = unlocked */
static u32 g_next_id = 2;        /* 1 = root, 0 = invalid */
static int g_count = 1;          /* live nodes incl. root */

/* ---- Helpers ---------------------------------------------------------- */
static void cpy_name(char *dst, const char *src, int cap)
{
    strncpy(dst, src, (usize)(cap - 1));
    dst[cap - 1] = 0;
}

static int scan_dup(oc_devnode_t *node, const char *name)
{
    if (strcmp(node->name, name) == 0) return 1;
    for (int i = 0; i < node->nchildren; i++)
        if (scan_dup(node->children[i], name)) return 1;
    return 0;
}

/* ---- Register / unregister -------------------------------------------- */
oc_devnode_t *oc_devtree_register(const char *name, const char *type,
                                  oc_devnode_t *parent)
{
    if (!name || !type || !name[0] || !type[0]) return 0;
    if (!parent) parent = &g_root;
    if (!parent->live) return 0;

    spin_lock(&g_tree_lock);

    /* Names are globally unique: one scan from the root covers the
     * parent and every existing node (the tree is single-rooted). */
    if (parent->nchildren >= OC_DEV_CHILD_MAX || scan_dup(&g_root, name)) {
        spin_unlock(&g_tree_lock);
        return 0;
    }

    oc_devnode_t *n = kzalloc(sizeof(oc_devnode_t));
    if (!n) { spin_unlock(&g_tree_lock); return 0; }

    n->id = g_next_id++;
    cpy_name(n->name, name, OC_DEV_NAME_MAX);
    cpy_name(n->type, type, OC_DEV_TYPE_MAX);
    n->parent = parent;
    n->live = 1;
    n->dstate = OC_DRV_D0;
    parent->children[parent->nchildren++] = n;
    g_count++;

    spin_unlock(&g_tree_lock);
    return n;
}

int oc_devtree_unregister(oc_devnode_t *node)
{
    if (!node || !node->live || node == &g_root) return -1;

    spin_lock(&g_tree_lock);

    if (node->nchildren > 0) { spin_unlock(&g_tree_lock); return -2; }
    if (node->bound)         { spin_unlock(&g_tree_lock); return -3; }

    oc_devnode_t *p = node->parent;
    if (p) {
        for (int i = 0; i < p->nchildren; i++) {
            if (p->children[i] == node) {
                p->children[i] = p->children[p->nchildren - 1];
                p->children[p->nchildren - 1] = 0;
                p->nchildren--;
                break;
            }
        }
    }
    node->live = 0;
    g_count--;

    spin_unlock(&g_tree_lock);
    kfree(node);
    return 0;
}

/* ---- Lookup ----------------------------------------------------------- */
static oc_devnode_t *find_id_rec(oc_devnode_t *node, u32 id)
{
    if (node->id == id) return node;
    for (int i = 0; i < node->nchildren; i++) {
        oc_devnode_t *r = find_id_rec(node->children[i], id);
        if (r) return r;
    }
    return 0;
}

oc_devnode_t *oc_devtree_find(u32 id)
{
    if (id == 0) return 0;
    if (id == 1) return &g_root;
    return find_id_rec(&g_root, id);
}

static oc_devnode_t *find_name_rec(oc_devnode_t *node, const char *name)
{
    if (strcmp(node->name, name) == 0) return node;
    for (int i = 0; i < node->nchildren; i++) {
        oc_devnode_t *r = find_name_rec(node->children[i], name);
        if (r) return r;
    }
    return 0;
}

oc_devnode_t *oc_devtree_find_name(const char *name)
{
    if (!name || !name[0]) return 0;
    return find_name_rec(&g_root, name);
}

oc_devnode_t *oc_devtree_root(void) { return &g_root; }
int oc_devtree_count(void) { return g_count; }

/* ---- Resources -------------------------------------------------------- */
int oc_devtree_add_res(oc_devnode_t *node, int kind, u64 start, u64 len)
{
    if (!node || !node->live || node->nres >= OC_DEV_RES_MAX) return -1;
    if (kind < OC_DEVRES_MMIO || kind > OC_DEVRES_DMA) return -1;
    oc_devres_t *r = &node->res[node->nres++];
    r->kind = (u8)kind;
    r->start = start;
    r->len = len;
    return 0;
}
