/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP08 - drivers/framework: the driver registry.
 *
 * Composition root of the metadata driver path: runtime state per
 * driver, the deterministic boot orchestration (collect -> order ->
 * init pass -> auto-bind pass -> hotplug drain) and the manual
 * bind/unbind/reload entries.  THE isolation rule lives here: every
 * probe/init result is checked, failures are recorded and skipped -
 * a failing driver can never panic the kernel and never blocks the
 * rest of the walk (see interface/core_drv_registry.h). */
#include "core_drv_registry.h"
#include "core_drv_order.h"
#include "core_drv_isolation.h"
#include "core_hotplug.h"
#include "core_lib_string.h"
#include "core_lib_log.h"

#define OC_DRVFW_MAX 256

static oc_drv_reg_t s_reg[OC_DRVFW_MAX];
static int s_nreg;
static int s_debug;

/* ---- Registry query ---------------------------------------------------- */
int oc_drvfw_count(void) { return s_nreg; }

const oc_drv_reg_t *oc_drvfw_at(int i)
{
    return (i >= 0 && i < s_nreg) ? &s_reg[i] : 0;
}

const oc_drv_reg_t *oc_drvfw_find(const char *name)
{
    if (!name || !name[0]) return 0;
    for (int i = 0; i < s_nreg; i++)
        if (strcmp(s_reg[i].info->name, name) == 0) return &s_reg[i];
    return 0;
}

int oc_drvfw_debug_enabled(void)        { return s_debug; }
void oc_drvfw_set_debug(int on)         { s_debug = on ? 1 : 0; }

/* ---- Helpers ----------------------------------------------------------- */
static int type_matches(const oc_driver_info_t *d, const char *type)
{
    if (!d->types) return 0;
    for (const char *const *t = d->types; *t; t++)
        if (strcmp(*t, type) == 0) return 1;
    return 0;
}

/* Every named dependency must exist and be in state OK. */
static int deps_ok(const oc_driver_info_t *d)
{
    if (!d->depends) return 1;
    for (const char *const *p = d->depends; *p; p++) {
        const oc_drv_reg_t *r = oc_drvfw_find(*p);
        if (!r || r->state != OC_DRVST_OK) return 0;
    }
    return 1;
}

static void log_line(const char *a, const char *b, const char *c)
{
    char line[128];
    strcpy(line, a);
    if (b) { strcat(line, " "); strcat(line, b); }
    if (c) { strcat(line, ": "); strcat(line, c); }
    lib_log_info(line);
}

/* Probe one driver against one node.  Returns 0 = bound; >0 = the
 * probe's error code NEGATED (drivers return negative errno; the
 * positive space is reserved so callers can tell probe results from
 * framework refusals, which stay negative); updates node, registry
 * state and the failure ledger (which stores the original sign). */
static int probe_one(oc_drv_reg_t *reg, oc_devnode_t *node)
{
    const oc_driver_info_t *d = reg->info;
    int rc = d->probe ? d->probe(node) : -61;   /* -61 = ENODATA-ish: no probe */
    if (rc == 0) {
        node->bound = d;
        node->bind_err = 0;
        node->dstate = OC_DRV_D0;
        reg->nbound++;
        reg->state = OC_DRVST_OK;
        if (s_debug) log_line("drvfw: bind", d->name, node->name);
        return 0;
    }
    node->bind_err = rc;
    reg->fail_count++;
    reg->last_err = rc;
    if (reg->nbound == 0) reg->state = OC_DRVST_FAILED;
    oc_drv_fail_note(d->name, rc);
    if (s_debug) log_line("drvfw: probe failed", d->name, node->name);
    return rc < 0 ? -rc : rc;
}

/* ---- Auto-bind pass -----------------------------------------------------
 * Walk the tree; every unbound node tries the ordered drivers whose
 * type list matches, deps OK.  First success wins. */
static int bind_visit(oc_devnode_t *node, int depth, void *ctx)
{
    (void)depth;
    int *bound_total = (int *)ctx;
    if (node->bound || node == oc_devtree_root()) return 0;
    for (int i = 0; i < core_drv_order_count(); i++) {
        const oc_driver_info_t *d = core_drv_order_at(i);
        oc_drv_reg_t *reg = (oc_drv_reg_t *)oc_drvfw_find(d->name);
        if (!reg) continue;
        if (reg->state == OC_DRVST_CYCLE || reg->state == OC_DRVST_MISSING_DEP)
            continue;
        if (reg->state == OC_DRVST_FAILED) continue;
        if (!d->probe) continue;                 /* nothing to probe */
        if (!type_matches(d, node->type)) continue;
        if (!deps_ok(d)) continue;
        if (probe_one(reg, node) == 0) {
            (*bound_total)++;
            break;
        }
    }
    return 0;
}

void oc_drvfw_bind_pass(void)
{
    int bound = 0;
    (void)oc_devtree_walk(bind_visit, &bound);
    /* WP-08: the bind-pass outcome belongs on the boot console -
     * probe failures are isolated, visible, and never fatal. */
    char line[96];
    char n[12];
    strcpy(line, "drvfw: bind pass: bound ");
    u64_to_str((u64)bound, n); strcat(line, n);
    strcat(line, " node(s), probe failures recorded: ");
    u64_to_str((u64)oc_drv_fail_count(), n); strcat(line, n);
    lib_log_info(line);
}

/* ---- Boot orchestration ------------------------------------------------ */
void oc_drvfw_boot(void)
{
    /* 1. Collect the .oc_drvi metadata section into the registry. */
    core_drv_meta_collect();
    s_nreg = core_drv_meta_count();
    if (s_nreg > OC_DRVFW_MAX) s_nreg = OC_DRVFW_MAX;
    for (int i = 0; i < s_nreg; i++) {
        s_reg[i].info = core_drv_meta_at(i);
        s_reg[i].state = OC_DRVST_REGISTERED;
        s_reg[i].last_err = 0;
        s_reg[i].fail_count = 0;
        s_reg[i].nbound = 0;
    }

    /* 2. Deterministic order (topological, cycle/missing detection). */
    core_drv_order_compute();
    for (int i = 0; i < core_drv_order_isolated_count(); i++) {
        const oc_driver_info_t *d = core_drv_order_isolated_at(i);
        oc_drv_reg_t *reg = (oc_drv_reg_t *)oc_drvfw_find(d->name);
        const char *why = core_drv_order_isolate_reason(d);
        if (reg) reg->state = (why && strcmp(why, "cycle") == 0)
                              ? OC_DRVST_CYCLE : OC_DRVST_MISSING_DEP;
        char msg[64];
        strcpy(msg, why ? why : "?");
        strcat(msg, (why && strcmp(why, "cycle") == 0)
                     ? " (isolated)" : " missing (isolated)");
        log_line("drvfw: isolated", d->name, msg);
    }

    /* 3. Init pass in computed order - failure isolation applies. */
    int ok = 0, fail = 0;
    for (int i = 0; i < core_drv_order_count(); i++) {
        const oc_driver_info_t *d = core_drv_order_at(i);
        oc_drv_reg_t *reg = (oc_drv_reg_t *)oc_drvfw_find(d->name);
        if (!reg || !d->init) continue;
        int rc = d->init();
        if (rc == 0) {
            reg->state = OC_DRVST_OK;
            ok++;
            if (s_debug) log_line("drvfw: init ok", d->name, 0);
        } else {
            reg->state = OC_DRVST_FAILED;
            reg->last_err = rc;
            reg->fail_count++;
            oc_drv_fail_note(d->name, rc);
            fail++;
            log_line("drvfw: init FAILED (isolated, system continues)",
                     d->name, 0);
        }
    }

    /* 4. Auto-bind pass over the device tree. */
    oc_drvfw_bind_pass();

    /* 5. Deliver the hotplug events the inits may have posted. */
    (void)oc_hotplug_drain();

    /* 6. Boot-console report: the order itself (queryable invariant). */
    {
        char line[160];
        char n[12];
        strcpy(line, "drvfw: ");
        u64_to_str((u64)core_drv_order_count(), n); strcat(line, n);
        strcat(line, " metadata drivers, order:");
        for (int i = 0; i < core_drv_order_count(); i++) {
            const char *nm = core_drv_order_at(i)->name;
            /* keep the line bounded; `drv order` shows the full list */
            if (strlen(line) + strlen(nm) + 2 >= sizeof(line)) {
                strcat(line, " ...");
                break;
            }
            strcat(line, " ");
            strcat(line, nm);
            if (i + 1 < core_drv_order_count()) strcat(line, ",");
        }
        lib_log_info(line);
    }
    {
        char line[96];
        char n[12];
        strcpy(line, "drvfw: init ok=");
        u64_to_str((u64)ok, n); strcat(line, n);
        strcat(line, " fail="); u64_to_str((u64)fail, n); strcat(line, n);
        strcat(line, " isolated=");
        u64_to_str((u64)core_drv_order_isolated_count(), n); strcat(line, n);
        strcat(line, " (probe failures do not stop the boot)");
        lib_log_info(line);
    }
}

/* ---- Manual operations -------------------------------------------------- */
static int all_digits(const char *s)
{
    if (!s || !s[0]) return 0;
    for (const char *p = s; *p; p++)
        if (*p < '0' || *p > '9') return 0;
    return 1;
}

static oc_devnode_t *find_node_maybe_id(const char *name)
{
    oc_devnode_t *node = oc_devtree_find_name(name);
    if (node) return node;
    if (!all_digits(name)) return 0;
    int id = 0;
    for (const char *p = name; *p; p++) id = id * 10 + (*p - '0');
    return oc_devtree_find((u32)id);
}

int oc_drvfw_bind(const char *drv_name, const char *node_name)
{
    if (!drv_name || !node_name) return -10;
    oc_drv_reg_t *reg = (oc_drv_reg_t *)oc_drvfw_find(drv_name);
    if (!reg) return -11;                       /* unknown driver */
    if (!reg->info->probe) return -15;          /* nothing to probe */
    oc_devnode_t *node = find_node_maybe_id(node_name);
    if (!node) return -12;                      /* unknown node */
    if (node->bound) return -13;                /* already bound */
    int rc = probe_one(reg, node);
    (void)oc_hotplug_drain();
    return rc;
}

int oc_drvfw_unbind(const char *drv_name, const char *node_name)
{
    if (!drv_name || !node_name) return -10;
    oc_drv_reg_t *reg = (oc_drv_reg_t *)oc_drvfw_find(drv_name);
    if (!reg) return -11;
    oc_devnode_t *node = find_node_maybe_id(node_name);
    if (!node) return -12;
    if (node->bound != reg->info) return -14;   /* pair not bound */

    /* Clear BEFORE remove(): the driver may unregister the node it
     * owns from inside its callback, so nothing may touch `node`
     * after the call returns. */
    node->bound = 0;
    node->dstate = OC_DRV_D0;
    if (reg->nbound > 0) reg->nbound--;
    if (reg->info->remove) reg->info->remove(node);
    if (reg->nbound == 0 && reg->fail_count == 0)
        reg->state = OC_DRVST_REGISTERED;
    if (s_debug) log_line("drvfw: unbind", drv_name, node_name);
    (void)oc_hotplug_drain();
    return 0;
}

/* Walk-collect context: the nodes a reload must detach first (a
 * remove() may unregister nodes, so the list is built BEFORE any
 * callback runs - never mutate the tree from inside a walk). */
typedef struct {
    const oc_driver_info_t *drv;
    oc_devnode_t *owned[64];
    int n;
} reload_ctx_t;

static int reload_collect(oc_devnode_t *node, int depth, void *ctx)
{
    (void)depth;
    reload_ctx_t *rc_ = (reload_ctx_t *)ctx;
    if (node->bound == rc_->drv && rc_->n < 64)
        rc_->owned[rc_->n++] = node;
    return 0;
}

int oc_drvfw_reload(const char *drv_name)
{
    if (!drv_name) return -10;
    oc_drv_reg_t *reg = (oc_drv_reg_t *)oc_drvfw_find(drv_name);
    if (!reg) return -11;
    const oc_driver_info_t *d = reg->info;

    /* 1. Detach every node this driver owns. */
    reload_ctx_t rcx;
    memset(&rcx, 0, sizeof(rcx));
    rcx.drv = d;
    (void)oc_devtree_walk(reload_collect, &rcx);
    for (int i = 0; i < rcx.n; i++) {
        oc_devnode_t *node = rcx.owned[i];
        if (node->bound != d) continue;         /* vanished meanwhile */
        node->bound = 0;                        /* pre-clear, then remove */
        node->dstate = OC_DRV_D0;
        if (d->remove) d->remove(node);         /* may free the node */
    }
    reg->nbound = 0;
    reg->last_err = 0;

    /* 2. Re-run the init (failure stays isolated, never fatal). */
    if (d->init) {
        int rc = d->init();
        if (rc != 0) {
            reg->state = OC_DRVST_FAILED;
            reg->last_err = rc;
            reg->fail_count++;
            oc_drv_fail_note(drv_name, rc);
            log_line("drvfw: reload init FAILED (isolated)", drv_name, 0);
            (void)oc_hotplug_drain();
            return rc;
        }
    }
    reg->state = d->init ? OC_DRVST_OK : OC_DRVST_REGISTERED;

    /* 3. Re-bind + deliver whatever the fresh init posted. */
    int fails_before = reg->fail_count;
    oc_drvfw_bind_pass();
    (void)oc_hotplug_drain();

    /* 4. Ledger rule: a driver that came back CLEAN (init ok, no new
     * probe failure in this reload) clears its failure entry; one
     * that failed again keeps it (with the bumped count). */
    if (reg->state == OC_DRVST_OK && reg->fail_count == fails_before)
        (void)oc_drv_fail_clear(drv_name);
    if (s_debug) log_line("drvfw: reload", drv_name, 0);
    return 0;
}

/* ---- Subtree detach -----------------------------------------------------
 * The cascade helper for bus drivers: unbind (children first), then
 * unregister the whole subtree.  A remove() that wants its node to
 * SURVIVE (rebind later) uses oc_drvfw_unbind instead - this one
 * always unregisters. */
int oc_drvfw_detach_subtree(oc_devnode_t *node)
{
    if (!node || !node->live || node == oc_devtree_root()) return -1;

    /* Copy the child list: unregistering mutates it. */
    oc_devnode_t *kids[OC_DEV_CHILD_MAX];
    int nk = node->nchildren;
    for (int i = 0; i < nk && i < OC_DEV_CHILD_MAX; i++)
        kids[i] = node->children[i];
    for (int i = 0; i < nk && i < OC_DEV_CHILD_MAX; i++)
        (void)oc_drvfw_detach_subtree(kids[i]);

    if (node->bound) {
        const oc_driver_info_t *d = node->bound;
        oc_drv_reg_t *reg = (oc_drv_reg_t *)oc_drvfw_find(d->name);
        node->bound = 0;
        node->dstate = OC_DRV_D0;
        if (reg && reg->nbound > 0) reg->nbound--;
        if (d->remove) d->remove(node);
        if (reg && reg->nbound == 0 && reg->fail_count == 0)
            reg->state = OC_DRVST_REGISTERED;
        if (s_debug) log_line("drvfw: detach", d->name, node->name);
    }
    return oc_devtree_unregister(node);
}
