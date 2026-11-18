/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP08 - services/diag: the drv/dev surface.
 *
 * Kernel-side bodies of the L1SVC_DIAG_DRV_* / L1SVC_DIAG_DEV_* ops
 * (numbering: svc_sysapi_l1svc.h ops 40..51).  Every handler renders
 * plain ASCII into the L1SVC out buffer; the ring-3 side formats
 * nothing - this is the ONE shared implementation both the `drv`/
 * `dev` umbrella commands and their standalone subcommand twins
 * call, so `drv list` and `drv_list` are byte-identical by
 * construction. */
#include "svc_sysapi_l1svc.h"
#include "core_drv_registry.h"
#include "core_drv_order.h"
#include "core_drv_isolation.h"
#include "core_drv_meta.h"
#include "core_devtree.h"
#include "core_hotplug.h"
#include "drv_framework.h"        /* legacy OC_DRIVER_DECL registry */
#include "core_lib_string.h"

/* ---- Output plumbing (the svc_diag_l1.c idiom, local copy) ---------- */
static char *s_out;
static int   s_left;

static void put(const char *s)
{
    while (*s && s_left > 1) { *s_out++ = *s++; s_left--; }
    *s_out = 0;
}

static void put_u64(u64 v)
{
    char b[24];
    u64_to_str(v, b);
    put(b);
}

static void put_hex(u64 v)
{
    char b[20];
    static const char hexd[] = "0123456789ABCDEF";
    int i = 0;
    if (v == 0) { put("0x0"); return; }
    while (v && i < 16) { b[i++] = hexd[v & 0xF]; v >>= 4; }
    put("0x");
    while (i--) { char c[2] = { b[i], 0 }; put(c); }
}

static void put_i(int v)
{
    if (v < 0) { put("-"); put_u64((u64)(-(long)v)); }
    else put_u64((u64)v);
}

static void put_col(const char *s, int width)
{
    put(s);
    for (int n = (int)strlen(s); n < width; n++) put(" ");
}

/* ---- Small helpers ---------------------------------------------------- */
static int split_args(const char *a, char tok[][24], int maxtok)
{
    int n = 0;
    while (a && *a && n < maxtok) {
        while (*a == ' ') a++;
        if (!*a) break;
        int i = 0;
        while (*a && *a != ' ' && i < 23) tok[n][i++] = *a++;
        tok[n][i] = 0;
        while (*a && *a != ' ') a++;   /* swallow overlong tails */
        n++;
    }
    return n;
}

static const char *drv_state_name(int st)
{
    switch (st) {
    case OC_DRVST_REGISTERED:  return "registered";
    case OC_DRVST_OK:          return "ok";
    case OC_DRVST_FAILED:      return "failed";
    case OC_DRVST_CYCLE:       return "cycle";
    case OC_DRVST_MISSING_DEP: return "missing-dep";
    default:                   return "?";
    }
}

static void put_dstates(u32 mask)
{
    int first = 1;
    if (!mask) { put("none"); return; }
    for (int d = OC_DRV_D0; d <= OC_DRV_D3; d++) {
        if (!OC_DRV_D_SUPPORTS(mask, d)) continue;
        if (!first) put(" ");
        put("D");
        put_u64((u64)d);
        first = 0;
    }
}

static void put_kv(const char *k, const char *v)
{
    put("  ");
    put_col(k, 10);
    put(v);
    put("\n");
}

/* ===================================================================== */
/* drv list                                                              */
/* ===================================================================== */
int svc_diag_drvfw_drv_list(void)
{
    put("driver registry: ");
    put_u64((u64)(oc_drvfw_count() + drv_framework_count()));
    put(" drivers (");
    put_u64((u64)oc_drvfw_count());
    put(" metadata + ");
    put_u64((u64)drv_framework_count());
    put(" legacy)\n");
    put("name            version  state      devs  path    desc\n");

    for (int i = 0; i < oc_drvfw_count(); i++) {
        const oc_drv_reg_t *r = oc_drvfw_at(i);
        char devs[8];
        devs[0] = 0;
        u64_to_str((u64)r->nbound, devs);
        put_col(r->info->name, 16);
        put_col(r->info->version ? r->info->version : "-", 9);
        put_col(drv_state_name(r->state), 11);
        put_col(devs, 6);
        put_col("meta", 8);
        put(r->info->desc ? r->info->desc : "");
        put("\n");
    }
    for (int i = 0; i < drv_framework_count(); i++) {
        const oc_driver_decl_t *d = drv_framework_at(i);
        put_col(d->name, 16);
        put_col("-", 9);
        put_col("legacy", 11);
        put_col("-", 6);
        put_col("legacy", 8);
        put(d->desc ? d->desc : "");
        put("\n");
    }
    return 0;
}

/* ===================================================================== */
/* drv info <name>                                                       */
/* ===================================================================== */
typedef struct {
    const oc_driver_info_t *d;
    int n;
} bound_ctx_t;

static int bound_visit(oc_devnode_t *node, int depth, void *ctx)
{
    (void)depth;
    bound_ctx_t *c = (bound_ctx_t *)ctx;
    if (node->bound == c->d) {
        if (c->n > 0) put(", ");
        put(node->name);
        put(" (id ");
        put_u64((u64)node->id);
        put(")");
        c->n++;
    }
    return 0;
}

int svc_diag_drvfw_drv_info(const char *arg)
{
    char tok[1][24];
    if (split_args(arg, tok, 1) < 1) {
        put("drv info: missing driver name\n");
        put("usage: drv info <name>\n");
        return 1;
    }
    const oc_drv_reg_t *r = oc_drvfw_find(tok[0]);
    if (r) {
        const oc_driver_info_t *d = r->info;
        char b[16];
        put("driver: ");
        put(d->name);
        put("\n");
        put_kv("version", d->version ? d->version : "-");
        put_kv("author", d->author ? d->author : "-");
        put_kv("license", d->license ? d->license : "-");
        put_kv("desc", d->desc ? d->desc : "-");
        put_kv("path", "meta (OC_DRIVER_INFO_DECL)");
        put_kv("state", drv_state_name(r->state));

        b[0] = 0;
        u64_to_str((u64)d->prio_hint, b);
        put("  "); put_col("priority", 10); put(b); put("\n");

        put("  "); put_col("depends", 10);
        if (d->depends && d->depends[0]) {
            int first = 1;
            for (const char *const *p = d->depends; *p; p++) {
                if (!first) put(", ");
                put(*p);
                first = 0;
            }
        } else {
            put("(none)");
        }
        put("\n");

        put("  "); put_col("d-states", 10);
        put_dstates(d->dstates);
        put("\n");

        put("  "); put_col("failures", 10);
        put_i(r->fail_count);
        if (r->last_err) { put(" (last error "); put_i(r->last_err); put(")"); }
        put("\n");

        put("  "); put_col("hotplug", 10);
        put_u64((u64)oc_hotplug_sub_delivered_for(d->name));
        put(" events received\n");

        put("  "); put_col("devices", 10);
        bound_ctx_t c;
        c.d = d;
        c.n = 0;
        (void)oc_devtree_walk(bound_visit, &c);
        if (c.n == 0) put("(none)");
        put("\n");
        return 0;
    }
    for (int i = 0; i < drv_framework_count(); i++) {
        const oc_driver_decl_t *ld = drv_framework_at(i);
        if (strcmp(ld->name, tok[0]) == 0) {
            put("driver: ");
            put(ld->name);
            put("\n");
            put_kv("path", "legacy (OC_DRIVER_DECL)");
            put_kv("family", ld->family ? ld->family : "-");
            put_kv("desc", ld->desc ? ld->desc : "-");
            put("  note:      legacy registration path - no metadata,\n"
                "             managed by its family manager\n");
            return 0;
        }
    }
    put("drv info: unknown driver: ");
    put(tok[0]);
    put("\nusage: drv info <name>  (see `drv list`)\n");
    return 1;
}

/* ===================================================================== */
/* drv failed                                                            */
/* ===================================================================== */
int svc_diag_drvfw_drv_failed(void)
{
    int n = oc_drv_fail_count();
    if (n == 0) {
        put("no failed drivers (probe/init failures are isolated and\n"
            "recorded here; `drv reload <name>` retries)\n");
        return 0;
    }
    put("failed drivers: ");
    put_u64((u64)n);
    put(" (isolated - the boot was never blocked)\n");
    put("name            err   count  last seen\n");
    for (int i = 0; i < n; i++) {
        const oc_drv_failrec_t *f = oc_drv_fail_at(i);
        put_col(f->name, 16);
        put_i(f->err);
        put("  ");
        put_col("", 0);
        put_u64((u64)f->count);
        put("     ");
        put_u64(f->last_ts);
        put(" ms\n");
    }
    return 0;
}

/* ===================================================================== */
/* drv bind / unbind / reload / debug  (debug use)                       */
/* ===================================================================== */
int svc_diag_drvfw_drv_bind(const char *arg)
{
    char tok[2][24];
    if (split_args(arg, tok, 2) < 2) {
        put("drv bind: need <driver> and <device>\n");
        put("usage: drv bind <drv> <node>   (debug: manual binding)\n");
        return 1;
    }
    int rc = oc_drvfw_bind(tok[0], tok[1]);
    if (rc == 0) {
        put("bound ");
        put(tok[0]);
        put(" to ");
        put(tok[1]);
        put("\n");
        return 0;
    }
    if (rc > 0) {
        put("probe failed with error -");
        put_u64((u64)rc);
        put(" (recorded, system unaffected)\n");
        return 1;
    }
    switch (rc) {
    case -11: put("unknown driver: "); put(tok[0]); put("\n"); break;
    case -12: put("unknown device node: "); put(tok[1]); put("\n"); break;
    case -13: put("node already bound (unbind first)\n"); break;
    case -15: put("driver has no probe callback\n"); break;
    default:  put("bind refused (error "); put_i(rc); put(")\n"); break;
    }
    put("usage: drv bind <drv> <node>   (debug: manual binding)\n");
    return 1;
}

int svc_diag_drvfw_drv_unbind(const char *arg)
{
    char tok[2][24];
    if (split_args(arg, tok, 2) < 2) {
        put("drv unbind: need <driver> and <device>\n");
        put("usage: drv unbind <drv> <node> (debug: manual unbinding)\n");
        return 1;
    }
    int rc = oc_drvfw_unbind(tok[0], tok[1]);
    if (rc == 0) {
        put("unbound ");
        put(tok[0]);
        put(" from ");
        put(tok[1]);
        put("\n");
        return 0;
    }
    switch (rc) {
    case -11: put("unknown driver: "); put(tok[0]); put("\n"); break;
    case -12: put("unknown device node: "); put(tok[1]); put("\n"); break;
    case -14: put("that driver is not bound to that node\n"); break;
    default:  put("unbind refused (error "); put_i(rc); put(")\n"); break;
    }
    put("usage: drv unbind <drv> <node> (debug: manual unbinding)\n");
    return 1;
}

int svc_diag_drvfw_drv_reload(const char *arg)
{
    char tok[1][24];
    if (split_args(arg, tok, 1) < 1) {
        put("drv reload: missing driver name\n");
        put("usage: drv reload <drv>        (debug: re-init + re-probe)\n");
        return 1;
    }
    int rc = oc_drvfw_reload(tok[0]);
    if (rc == 0) {
        const oc_drv_reg_t *r = oc_drvfw_find(tok[0]);
        put("reloaded ");
        put(tok[0]);
        put(": state ");
        put(r ? drv_state_name(r->state) : "?");
        put(", ");
        put_u64((u64)(r ? r->nbound : 0));
        put(" node(s) bound\n");
        return 0;
    }
    if (rc != 0 && rc != -10 && rc != -11) {
        put("reload: init/probe failed with error ");
        put_i(rc);
        put(" (isolated, recorded)\n");
        return 1;
    }
    if (rc == -11) {
        put("unknown driver: ");
        put(tok[0]);
        put("\n");
    } else {
        put("reload refused (error ");
        put_i(rc);
        put(")\n");
    }
    put("usage: drv reload <drv>        (debug: re-init + re-probe)\n");
    return 1;
}

int svc_diag_drvfw_drv_debug(const char *arg)
{
    char tok[1][24];
    if (split_args(arg, tok, 1) < 1) {
        put("drv debug: need on or off\n");
        put("usage: drv debug <on|off>     (debug: framework log switch)\n");
        put("note: dev debug toggles the same framework flag\n");
        return 1;
    }
    if (strcmp(tok[0], "on") == 0) {
        oc_drvfw_set_debug(1);
        put("driver framework debug logging: on\n");
        return 0;
    }
    if (strcmp(tok[0], "off") == 0) {
        oc_drvfw_set_debug(0);
        put("driver framework debug logging: off\n");
        return 0;
    }
    put("drv debug: unknown argument: ");
    put(tok[0]);
    put(" (expected on or off)\n");
    put("usage: drv debug <on|off>     (debug: framework log switch)\n");
    return 1;
}

/* ===================================================================== */
/* drv order                                                             */
/* ===================================================================== */
int svc_diag_drvfw_drv_order(void)
{
    put("metadata driver init order (topological, deterministic:\n"
        "dependencies first, then priority hint, then name - never link\n"
        "order):\n");
    for (int i = 0; i < core_drv_order_count(); i++) {
        const oc_driver_info_t *d = core_drv_order_at(i);
        put("  ");
        put_u64((u64)(i + 1));
        put(". ");
        put(d->name);
        if (d->depends && d->depends[0]) {
            put("  <- ");
            int first = 1;
            for (const char *const *p = d->depends; *p; p++) {
                if (!first) put(", ");
                put(*p);
                first = 0;
            }
        }
        put("\n");
    }
    int iso = core_drv_order_isolated_count();
    put("cycle detection: ");
    if (iso == 0) {
        put("no dependency cycles, no missing dependencies\n");
    } else {
        put_u64((u64)iso);
        put(" driver(s) isolated:\n");
        for (int i = 0; i < iso; i++) {
            const oc_driver_info_t *d = core_drv_order_isolated_at(i);
            const char *why = core_drv_order_isolate_reason(d);
            put("  ");
            put(d->name);
            put(": ");
            if (why && strcmp(why, "cycle") == 0)
                put("dependency cycle (reported, never looped)\n");
            else {
                put("missing dependency: ");
                put(why ? why : "?");
                put("\n");
            }
        }
    }
    put("legacy OC_DRIVER_DECL drivers: ");
    put_u64((u64)drv_framework_count());
    put(" (initialized by the kmain family-manager sequence - outside\n"
        "this computation by design)\n");
    return 0;
}

/* ===================================================================== */
/* dev tree                                                              */
/* ===================================================================== */
static const char *res_kind_name(int k)
{
    switch (k) {
    case OC_DEVRES_MMIO:   return "mmio";
    case OC_DEVRES_IOPORT: return "io";
    case OC_DEVRES_IRQ:    return "irq";
    case OC_DEVRES_DMA:    return "dma";
    default:               return "?";
    }
}

static int tree_visit(oc_devnode_t *node, int depth, void *ctx)
{
    (void)ctx;
    for (int i = 0; i < depth; i++) put("  ");
    put("[");
    put_u64((u64)node->id);
    put("] ");
    put(node->name);
    put("  type=");
    put(node->type);
    put("  drv=");
    if (node->bound) {
        put(node->bound->name);
        put("  D");
        put_u64((u64)node->dstate);
    } else {
        put("-");
        if (node->bind_err) {
            put("  unbound (last probe error ");
            put_i(node->bind_err);
            put(")");
        }
    }
    for (int i = 0; i < node->nres; i++) {
        put("  ");
        put(res_kind_name(node->res[i].kind));
        put(" ");
        if (node->res[i].kind == OC_DEVRES_IRQ ||
            node->res[i].kind == OC_DEVRES_DMA)
            put_u64(node->res[i].start);
        else {
            put_hex(node->res[i].start);
            put("+");
            put_hex(node->res[i].len);
        }
    }
    put("\n");
    return 0;
}

int svc_diag_drvfw_dev_tree(void)
{
    put("device tree: ");
    put_u64((u64)oc_devtree_count());
    put(" node(s)\n");
    (void)oc_devtree_walk(tree_visit, 0);
    return 0;
}

/* ===================================================================== */
/* dev list [type]                                                       */
/* ===================================================================== */
typedef struct {
    const char *type;
    int n;
} list_ctx_t;

static int list_visit(oc_devnode_t *node, int depth, void *ctx)
{
    (void)depth;
    list_ctx_t *c = (list_ctx_t *)ctx;
    if (c->type && strcmp(c->type, node->type) != 0) return 0;
    put("[");
    put_u64((u64)node->id);
    put("] ");
    put_col(node->name, 14);
    put_col(node->type, 12);
    put(node->bound ? node->bound->name : "-");
    put("\n");
    c->n++;
    return 0;
}

int svc_diag_drvfw_dev_list(const char *arg)
{
    char tok[1][24];
    int n = split_args(arg, tok, 1);
    list_ctx_t c;
    c.type = (n >= 1) ? tok[0] : 0;
    c.n = 0;
    put("device nodes");
    if (c.type) {
        put(" of type ");
        put(c.type);
    }
    put(":\n");
    put("id   name           type         driver\n");
    (void)oc_devtree_walk(list_visit, &c);
    if (c.n == 0) {
        put(c.type ? "(no nodes of that type)\n" : "(no device nodes)\n");
    }
    return 0;
}

/* ===================================================================== */
/* dev info <id|name>                                                    */
/* ===================================================================== */
static oc_devnode_t *find_node_arg(const char *s)
{
    oc_devnode_t *node = oc_devtree_find_name(s);
    if (node) return node;
    int id = 0, any = 0;
    for (const char *p = s; *p; p++) {
        if (*p < '0' || *p > '9') return 0;
        id = id * 10 + (*p - '0');
        any = 1;
    }
    if (!any) return 0;
    return oc_devtree_find((u32)id);
}

int svc_diag_drvfw_dev_info(const char *arg)
{
    char tok[1][24];
    if (split_args(arg, tok, 1) < 1) {
        put("dev info: missing node id or name\n");
        put("usage: dev info <id|name>\n");
        return 1;
    }
    oc_devnode_t *n = find_node_arg(tok[0]);
    if (!n) {
        put("dev info: unknown device node: ");
        put(tok[0]);
        put("\nusage: dev info <id|name>  (see `dev tree`)\n");
        return 1;
    }
    put("node: ");
    put(n->name);
    put("\n");
    put("  "); put_col("id", 10); put_u64((u64)n->id); put("\n");
    put("  "); put_col("type", 10); put(n->type); put("\n");
    put("  "); put_col("parent", 10);
    if (n->parent) {
        put(n->parent->name);
        put(" (id ");
        put_u64((u64)n->parent->id);
        put(")");
    } else {
        put("(none)");
    }
    put("\n");
    put("  "); put_col("children", 10); put_u64((u64)n->nchildren);
    for (int i = 0; i < n->nchildren; i++) {
        put(i ? ", " : " ");
        put(n->children[i]->name);
    }
    put("\n");
    put("  "); put_col("driver", 10);
    if (n->bound) {
        put(n->bound->name);
        put(" (meta path)");
    } else {
        put("(unbound)");
        if (n->bind_err) {
            put("  last probe error: ");
            put_i(n->bind_err);
            put(" (retryable)");
        }
    }
    put("\n");
    put("  "); put_col("d-state", 10); put("D");
    put_u64((u64)n->dstate);
    if (n->bound && !OC_DRV_D_SUPPORTS(n->bound->dstates, n->dstate))
        put(" (driver declares: none - not PM-managed)");
    put("\n");
    put("  "); put_col("resources", 10);
    if (n->nres == 0) {
        put("(none)");
    }
    put("\n");
    for (int i = 0; i < n->nres; i++) {
        put("    ");
        put_col(res_kind_name(n->res[i].kind), 6);
        if (n->res[i].kind == OC_DEVRES_IRQ ||
            n->res[i].kind == OC_DEVRES_DMA) {
            put_u64(n->res[i].start);
        } else {
            put_hex(n->res[i].start);
            put(" + ");
            put_hex(n->res[i].len);
        }
        put("\n");
    }
    put("  "); put_col("priv", 10); put_hex((u64)(usize)n->priv); put("\n");
    return 0;
}

/* ===================================================================== */
/* dev debug <on|off>  (same framework flag as drv debug)                */
/* ===================================================================== */
int svc_diag_drvfw_dev_debug(const char *arg)
{
    char tok[1][24];
    if (split_args(arg, tok, 1) < 1) {
        put("dev debug: need on or off\n");
        put("usage: dev debug <on|off>     (debug: framework log switch)\n");
        put("note: drv debug toggles the same framework flag\n");
        return 1;
    }
    if (strcmp(tok[0], "on") == 0) {
        oc_drvfw_set_debug(1);
        put("driver framework debug logging: on\n");
        return 0;
    }
    if (strcmp(tok[0], "off") == 0) {
        oc_drvfw_set_debug(0);
        put("driver framework debug logging: off\n");
        return 0;
    }
    put("dev debug: unknown argument: ");
    put(tok[0]);
    put(" (expected on or off)\n");
    put("usage: dev debug <on|off>     (debug: framework log switch)\n");
    return 1;
}

/* ---- The shared dispatch used by svc_diag_l1.c ----------------------- */
int svc_diag_drvfw_dispatch(u32 op, const char *arg, char *out, int cap)
{
    if (!out || cap <= 0) return -1;
    s_out = out;
    s_left = cap;
    out[0] = 0;
    const char *a = arg ? arg : "";
    switch (op) {
    case L1SVC_DIAG_DRV_LIST:   return svc_diag_drvfw_drv_list();
    case L1SVC_DIAG_DRV_INFO:   return svc_diag_drvfw_drv_info(a);
    case L1SVC_DIAG_DRV_FAILED: return svc_diag_drvfw_drv_failed();
    case L1SVC_DIAG_DRV_BIND:   return svc_diag_drvfw_drv_bind(a);
    case L1SVC_DIAG_DRV_UNBIND: return svc_diag_drvfw_drv_unbind(a);
    case L1SVC_DIAG_DRV_RELOAD: return svc_diag_drvfw_drv_reload(a);
    case L1SVC_DIAG_DRV_DEBUG:  return svc_diag_drvfw_drv_debug(a);
    case L1SVC_DIAG_DRV_ORDER:  return svc_diag_drvfw_drv_order();
    case L1SVC_DIAG_DEV_TREE:   return svc_diag_drvfw_dev_tree();
    case L1SVC_DIAG_DEV_LIST:   return svc_diag_drvfw_dev_list(a);
    case L1SVC_DIAG_DEV_INFO:   return svc_diag_drvfw_dev_info(a);
    case L1SVC_DIAG_DEV_DEBUG:  return svc_diag_drvfw_dev_debug(a);
    default: return -1;
    }
}
