/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP08 - drivers/framework: device tree abstraction.
 *
 * The device node data model + register/unregister + lookup + walk
 * interfaces.  Deliberately free of ACPI and of every concrete bus:
 * nodes are pushed in by their owners (a bus driver, a platform
 * shim, a test harness); WP-09a/WP-09b will fill the tree from
 * PCI/USB enumeration through the SAME register call.
 *
 * NAK placement: this header is the interface half of
 * ocos/drivers/framework (interface/ vs src/ isolation rule). */
#ifndef OC_CORE_DEVTREE_H
#define OC_CORE_DEVTREE_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Limits -----------------------------------------------------------
 * Fixed-size inline arrays keep nodes kmalloc-simple and the tree
 * walkable without any allocator dependency of its own.  They are
 * framework limits, not bus limits: raise them when a real bus needs
 * more (WP-09a/b will tell). */
#define OC_DEV_NAME_MAX 24   /* NUL-terminated node name                */
#define OC_DEV_TYPE_MAX 16   /* NUL-terminated device type              */
#define OC_DEV_CHILD_MAX 8   /* children per node                       */
#define OC_DEV_RES_MAX   6   /* resources per node                      */

/* ---- Device resources ------------------------------------------------- */
enum oc_devres_kind {
    OC_DEVRES_MMIO   = 1,    /* memory-mapped I/O window  (start,len)   */
    OC_DEVRES_IOPORT = 2,    /* I/O port range            (start,len)   */
    OC_DEVRES_IRQ    = 3,    /* interrupt line            (start = irq) */
    OC_DEVRES_DMA    = 4,    /* DMA channel               (start = ch)  */
};

typedef struct {
    u8  kind;                /* enum oc_devres_kind                     */
    u8  _pad[7];
    u64 start;
    u64 len;
} oc_devres_t;

struct oc_driver_info;       /* core_drv_meta.h (avoid a header cycle)  */

/* ---- Device node ------------------------------------------------------ */
typedef struct oc_devnode {
    u32 id;                  /* unique id, assigned at registration     */
    u32 _pad;
    char name[OC_DEV_NAME_MAX];
    char type[OC_DEV_TYPE_MAX];
    struct oc_devnode *parent;          /* 0 for the root              */
    struct oc_devnode *children[OC_DEV_CHILD_MAX];
    int nchildren;
    oc_devres_t res[OC_DEV_RES_MAX];
    int nres;
    const struct oc_driver_info *bound; /* 0 = not bound                */
    int bind_err;                        /* last probe rc (0 = ok/none)  */
    int dstate;                          /* current D-state (core_pm_hook)*/
    void *priv;                          /* owner private data           */
    int live;                            /* registration flag            */
} oc_devnode_t;

/* ---- Register / unregister -------------------------------------------
 * register: name+type must be non-empty, parent must be live (0 = the
 * framework root), names are globally unique.  Returns the node or 0
 * (invalid argument / duplicate name / child slot full / out of heap).
 * unregister: refuses while children are attached or a driver is bound
 * (unbind first); detaches from the parent and frees the node. */
oc_devnode_t *oc_devtree_register(const char *name, const char *type,
                                  oc_devnode_t *parent);
int  oc_devtree_unregister(oc_devnode_t *node);

/* ---- Lookup ----------------------------------------------------------- */
oc_devnode_t *oc_devtree_find(u32 id);
oc_devnode_t *oc_devtree_find_name(const char *name);
oc_devnode_t *oc_devtree_root(void);
int           oc_devtree_count(void);

/* ---- Resources -------------------------------------------------------- */
int oc_devtree_add_res(oc_devnode_t *node, int kind, u64 start, u64 len);

/* ---- Iteration --------------------------------------------------------
 * Every walker visits nodes in tree order (pre-order DFS, children in
 * registration order).  A callback returning non-zero stops the walk
 * and the value is returned to the caller.  ctx is passed through
 * untouched.  The walker skips nothing: live nodes only, by
 * construction. */
typedef int (*oc_devtree_fn_t)(oc_devnode_t *node, int depth, void *ctx);
int oc_devtree_walk(oc_devtree_fn_t fn, void *ctx);          /* whole tree */

typedef int (*oc_devnode_fn_t)(oc_devnode_t *node, void *ctx);
int oc_devtree_for_each_type(const char *type, oc_devnode_fn_t fn,
                             void *ctx);                      /* by type  */
int oc_devtree_for_each_child(oc_devnode_t *parent, oc_devnode_fn_t fn,
                              void *ctx);                     /* children */

#ifdef __cplusplus
}
#endif

#endif /* OC_CORE_DEVTREE_H */
