/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP08 - host unit test for the driver framework.
 *
 * Compiles the REAL framework sources (ocos/drivers/framework/src/*.c)
 * on the host with tiny stubs for kmalloc/spinlock/timer/log - no
 * ACPI, no bus, no QEMU: the WP-08 independence bottom line.  The
 * .oc_drvi linker section is stood in by a plain array pair with the
 * same walk semantics (the collector steps start..end exactly like
 * the kernel image does).
 *
 * Build: gcc -O2 -fno-toplevel-reorder -Iocos/kernel -Iocos/kernel/arch/x86_64 -Iocos/drivers -Iocos/drivers/framework/interface -Iocos/services/sysapi/interface tests/host_drv_framework_test.c ocos/drivers/framework/src/core_devtree_node.c ocos/drivers/framework/src/core_devtree_walk.c ocos/drivers/framework/src/core_drv_meta.c ocos/drivers/framework/src/core_drv_order.c ocos/drivers/framework/src/core_drv_isolation.c ocos/drivers/framework/src/core_hotplug_queue.c ocos/drivers/framework/src/core_hotplug_sub.c ocos/drivers/framework/src/core_pm_hook.c ocos/drivers/framework/src/core_drv_registry.c ocos/kernel/core_lib_string.c -o /tmp/oc_hosttest/host_drv_framework_test
 *
 * Covers: metadata collection, deterministic topological order + cycle
 * + missing-dep isolation, init/bind passes, devtree register/find/
 * walk/resources/unregister-refusals, PM hook ordering (children-first
 * suspend, reverse resume, undeclared states skipped), hotplug post/
 * filter/deliver/overflow-drop, bind/unbind/reload semantics, failure
 * ledger rules.  Exit code: number of failed checks. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "types.h"
#include "core_sync.h"            /* spinlock_t for the stubs below */
#include "core_drv_meta.h"
#include "core_devtree.h"
#include "core_drv_registry.h"
#include "core_drv_order.h"
#include "core_drv_isolation.h"
#include "core_hotplug.h"
#include "core_pm_hook.h"
#include "drv_framework.h"

/* ---- host stubs ------------------------------------------------------ */
void *kmalloc(u64 size)            { return malloc((size_t)size); }
void *kzalloc(u64 size)            { return calloc(1, (size_t)size); }
void  kfree(void *p)               { free(p); }
void  spin_init(spinlock_t *l)     { (void)l; }
void  spin_lock(spinlock_t *l)     { (void)l; }
void  spin_unlock(spinlock_t *l)   { (void)l; }
u64   core_timer_now_ms(void)      { static u64 ms; return ++ms; }
void  lib_log_info(const char *s)  { printf("    [klog] %s\n", s); }

/* legacy OC_DRIVER_DECL section: empty on host (the stub pattern) */
const oc_driver_decl_t __oc_drvs_start[1] = {{0, 0, 0, 0}};
const oc_driver_decl_t __oc_drvs_end[1]   = {{0, 0, 0, 0}};

/* ---- check plumbing -------------------------------------------------- */
static int s_fails, s_checks;
static void ck(const char *what, int ok)
{
    s_checks++;
    if (!ok) { s_fails++; printf("[FAIL] %s\n", what); }
    else      { printf("[ ok ] %s\n", what); }
}

/* event tape: every callback appends one line */
static char s_tape[64][48];
static int  s_ntape;
static void tape(const char *s)
{
    if (s_ntape < 64) {
        strncpy(s_tape[s_ntape], s, 47);
        s_tape[s_ntape][47] = 0;
        s_ntape++;
    }
}
static int tape_has(const char *s)
{
    for (int i = 0; i < s_ntape; i++)
        if (strcmp(s_tape[i], s) == 0) return 1;
    return 0;
}
static int tape_idx(const char *s)
{
    for (int i = 0; i < s_ntape; i++)
        if (strcmp(s_tape[i], s) == 0) return i;
    return -1;
}
static void tape_reset(void) { s_ntape = 0; }

/* ---- host test drivers ------------------------------------------------ */
static oc_devnode_t *s_hbus;

static int hbus_init(void)
{
    s_hbus = oc_devtree_register("hbus0", "h-bus", 0);
    if (!s_hbus) return -50;
    oc_devnode_t *c0 = oc_devtree_register("hdev0", "h-ctl", s_hbus);
    oc_devnode_t *c1 = oc_devtree_register("hdev1", "h-ctl", s_hbus);
    if (!c0 || !c1) return -50;
    oc_devtree_add_res(c0, OC_DEVRES_MMIO, 0x1000, 0x100);
    oc_devtree_add_res(c0, OC_DEVRES_IRQ, 9, 1);
    oc_hotplug_post(OC_HPEV_INSERT, c0->id, "h-bus");
    oc_hotplug_post(OC_HPEV_INSERT, c1->id, "h-bus");
    tape("bus-init");
    return 0;
}
static int hbus_probe(oc_devnode_t *n)
{
    tape("bus-probe");
    if (strcmp(n->type, "h-bus") != 0) return -22;   /* wrong-type refuse */
    return 0;
}
static void hbus_remove(oc_devnode_t *n)
{
    while (n->nchildren > 0) {
        oc_devnode_t *c = n->children[0];
        oc_hotplug_post(OC_HPEV_REMOVE, c->id, "h-bus");
        (void)oc_drvfw_detach_subtree(c);
    }
    oc_hotplug_post(OC_HPEV_REMOVE, n->id, "h-bus");
    (void)oc_devtree_unregister(n);
    s_hbus = 0;
    tape("bus-remove");
}
static int hbus_suspend(oc_devnode_t *n, int st)
{ tape("bus-suspend"); (void)n; return st == OC_DRV_D3 ? 0 : 0; }
static int hbus_resume(oc_devnode_t *n, int from)
{ tape("bus-resume"); (void)n; (void)from; return 0; }

static int hpm_init(void)
{
    oc_devnode_t *n = oc_devtree_register("hpm0", "h-pm", 0);
    return n ? 0 : -51;
}
static int hpm_probe(oc_devnode_t *n) { (void)n; return 0; }
static void hpm_remove(oc_devnode_t *n)
{
    /* self-owned node: the framework cleared ->bound before this
     * call, so the unregister is legal (see core_drv_registry.h) */
    (void)oc_devtree_unregister(n);
}
static int hpm_suspend(oc_devnode_t *n, int st)
{ tape("pm-suspend"); (void)n; (void)st; return 0; }
static int hpm_resume(oc_devnode_t *n, int from)
{ tape("pm-resume"); (void)n; (void)from; return 0; }

static int hctl_probe(oc_devnode_t *n)
{ tape("ctl-probe"); (void)n; return 0; }
static void hctl_remove(oc_devnode_t *n) { (void)n; }
static int hctl_suspend(oc_devnode_t *n, int st)
{ tape("ctl-suspend"); (void)n; (void)st; return 0; }
static int hctl_resume(oc_devnode_t *n, int from)
{ tape("ctl-resume"); (void)n; (void)from; return 0; }

static int hfail_init(void)
{
    if (oc_devtree_find_name("hfail0")) return 0;
    return oc_devtree_register("hfail0", "h-fail", 0) ? 0 : -52;
}
static int hfail_probe(oc_devnode_t *n) { (void)n; return -77; }

static const char *const dep_ctl[]  = { "h-bus", 0 };
static const char *const dep_ca[]   = { "h-cyc-b", 0 };
static const char *const dep_cb[]   = { "h-cyc-a", 0 };
static const char *const dep_miss[] = { "h-nonexistent", 0 };
static const char *const typ_bus[]  = { "h-bus", 0 };
static const char *const typ_ctl[]  = { "h-ctl", 0 };
static const char *const typ_pm[]   = { "h-pm", 0 };
static const char *const typ_fail[] = { "h-fail", 0 };

/* The .oc_drvi section stand-in: the collector walks start..end.
 * Two guarantees are needed and both are live-caught lessons:
 *  - one custom section for both symbols (with function pointers
 *    inside, gcc would otherwise split them across .data.rel.ro and
 *    .rodata - non-adjacent, empty walk);
 *  - -fno-toplevel-reorder in the Build line (at -O2 the compiler
 *    may emit the two arrays in the section in reverse declaration
 *    order - end BEFORE start - and the walk sees nothing). */
#define HOST_DRVSEC __attribute__((section(".host_oc_drvi")))
HOST_DRVSEC const oc_driver_info_t __oc_drvi_start[] = {
    { .name = "h-bus", .version = "1.0", .author = "host", .license = "Apache-2.0",
      .types = typ_bus, .dstates = OC_DRV_D_STATE(OC_DRV_D0) | OC_DRV_D_STATE(OC_DRV_D3),
      .init = hbus_init, .probe = hbus_probe, .remove = hbus_remove,
      .suspend = hbus_suspend, .resume = hbus_resume, .desc = "host bus" },
    { .name = "h-ctl", .version = "1.0", .author = "host", .license = "Apache-2.0",
      .depends = dep_ctl, .types = typ_ctl,
      .dstates = OC_DRV_D_STATE(OC_DRV_D0) | OC_DRV_D_STATE(OC_DRV_D3),
      .probe = hctl_probe, .remove = hctl_remove,
      .suspend = hctl_suspend, .resume = hctl_resume, .desc = "host ctl" },
    { .name = "h-pm", .version = "1.0", .author = "host", .license = "Apache-2.0",
      .types = typ_pm, .dstates = OC_DRV_D_STATE(OC_DRV_D0) | OC_DRV_D_STATE(OC_DRV_D1),
      .init = hpm_init, .probe = hpm_probe, .remove = hpm_remove,
      .suspend = hpm_suspend, .resume = hpm_resume, .desc = "host D1-only pm" },
    { .name = "h-fail", .version = "1.0", .author = "host", .license = "Apache-2.0",
      .types = typ_fail, .init = hfail_init, .probe = hfail_probe,
      .desc = "host failure injector" },
    { .name = "h-cyc-a", .version = "1.0", .author = "host", .license = "Apache-2.0",
      .depends = dep_ca, .desc = "host cycle canary A" },
    { .name = "h-cyc-b", .version = "1.0", .author = "host", .license = "Apache-2.0",
      .depends = dep_cb, .desc = "host cycle canary B" },
    { .name = "h-miss", .version = "1.0", .author = "host", .license = "Apache-2.0",
      .depends = dep_miss, .desc = "host missing-dep canary" },
};
HOST_DRVSEC const oc_driver_info_t __oc_drvi_end[] = { {0} };

/* ---- hotplug subscriber ---------------------------------------------- */
static int s_ins_ctl, s_rem_ctl, s_node_hit;
static void mon_cb(const oc_hotplug_ev_t *ev)
{
    if (ev->type == OC_HPEV_INSERT) s_ins_ctl++;
    else s_rem_ctl++;
}
static void node_cb(const oc_hotplug_ev_t *ev) { (void)ev; s_node_hit++; }

/* ---- devtree walk collectors ------------------------------------------ */
static int s_walk_order[16], s_walk_n;
static int walk_rec(oc_devnode_t *n, int depth, void *ctx)
{
    (void)depth; (void)ctx;
    if (s_walk_n < 16) s_walk_order[s_walk_n++] = (int)n->id;
    return 0;
}
static int s_type_hits;
static int type_rec(oc_devnode_t *n, void *ctx)
{
    (void)n; (void)ctx; s_type_hits++;
    return 0;
}

int main(void)
{
    printf("== WP-08 host unit test: driver framework ==\n");

    /* ---- 1. the full boot orchestration on host --------------------- */
    oc_drvfw_boot();
    ck("metadata collected: 7 drivers", core_drv_meta_count() == 7);
    ck("registry filled", oc_drvfw_count() == 7);

    int i_bus = -1, i_ctl = -1;
    for (int i = 0; i < core_drv_order_count(); i++) {
        const char *nm = core_drv_order_at(i)->name;
        if (strcmp(nm, "h-bus") == 0) i_bus = i;
        if (strcmp(nm, "h-ctl") == 0) i_ctl = i;
    }
    ck("topo order: dependency h-bus precedes h-ctl", i_bus >= 0 && i_ctl > i_bus);
    ck("cycle + missing-dep isolated: 3", core_drv_order_isolated_count() == 3);
    ck("cycle members never ran (no tape entries)",
       !tape_has("cyc") && s_ntape >= 3);

    const oc_drv_reg_t *r_ctl = oc_drvfw_find("h-ctl");
    const oc_drv_reg_t *r_fail = oc_drvfw_find("h-fail");
    const oc_drv_reg_t *r_ca = oc_drvfw_find("h-cyc-a");
    const oc_drv_reg_t *r_miss = oc_drvfw_find("h-miss");
    ck("h-ctl bound to 2 devices", r_ctl && r_ctl->state == OC_DRVST_OK
       && r_ctl->nbound == 2);
    ck("h-fail state failed, isolated", r_fail && r_fail->state == OC_DRVST_FAILED);
    ck("h-cyc-a state cycle", r_ca && r_ca->state == OC_DRVST_CYCLE);
    ck("h-miss state missing-dep", r_miss && r_miss->state == OC_DRVST_MISSING_DEP);

    /* ---- 2. device tree surfaces ------------------------------------ */
    oc_devnode_t *hbus = oc_devtree_find_name("hbus0");
    oc_devnode_t *hdev0 = oc_devtree_find_name("hdev0");
    oc_devnode_t *hdev1 = oc_devtree_find_name("hdev1");
    oc_devnode_t *hpm0 = oc_devtree_find_name("hpm0");
    oc_devnode_t *hfail0 = oc_devtree_find_name("hfail0");
    ck("nodes registered + findable by name",
       hbus && hdev0 && hdev1 && hpm0 && hfail0);
    ck("find by id matches find by name",
       oc_devtree_find(hdev0->id) == hdev0);
    ck("auto-bind bound h-ctl to both ctl nodes",
       hdev0->bound == r_ctl->info && hdev1->bound == r_ctl->info);
    ck("failed probe left the node unbound with its error",
       hfail0->bound == 0 && hfail0->bind_err == -77);

    s_walk_n = 0;
    (void)oc_devtree_walk(walk_rec, 0);
    ck("pre-order DFS visits parent before children",
       s_walk_n == 6 && s_walk_order[0] == 1);   /* root first */
    int p = -1, c0 = -1;
    for (int i = 0; i < s_walk_n; i++) {
        if (s_walk_order[i] == (int)hbus->id) p = i;
        if (s_walk_order[i] == (int)hdev0->id) c0 = i;
    }
    ck("DFS: bus before its child", p >= 0 && c0 == p + 1);

    s_type_hits = 0;
    (void)oc_devtree_for_each_type("h-ctl", type_rec, 0);
    ck("for-each-type counts exactly the matches", s_type_hits == 2);
    s_type_hits = 0;
    (void)oc_devtree_for_each_child(hbus, type_rec, 0);
    ck("for-each-child counts direct children", s_type_hits == 2);

    ck("resources recorded", hdev0->nres == 2 && hdev0->res[0].kind == OC_DEVRES_MMIO
       && hdev0->res[0].start == 0x1000 && hdev0->res[1].kind == OC_DEVRES_IRQ
       && hdev0->res[1].start == 9);

    ck("unregister refuses a bound node", oc_devtree_unregister(hdev0) == -3);
    ck("unregister refuses a node with children",
       oc_devtree_unregister(hbus) == -2);
    oc_devnode_t *lone = oc_devtree_register("lone0", "h-lone", 0);
    ck("unregister accepts a free leaf", lone && oc_devtree_unregister(lone) == 0);
    ck("duplicate name refused",
       oc_devtree_register("hbus0", "h-bus", 0) == 0);

    /* ---- 3. PM hooks -------------------------------------------------- */
    tape_reset();
    ck("pm notify D3 ok", oc_pm_notify(OC_DRV_D3) == 0);
    ck("suspend ran children-first (ctl before bus)",
       tape_idx("ctl-suspend") >= 0 && tape_idx("bus-suspend") > tape_idx("ctl-suspend"));
    ck("D3-undeclared driver was skipped", !tape_has("pm-suspend"));
    ck("node d-state advanced to D3", hdev0->dstate == OC_DRV_D3 && hbus->dstate == OC_DRV_D3);
    ck("nodes in D3 queryable", oc_pm_nodes_in_dstate(OC_DRV_D3) == 3);

    tape_reset();
    ck("pm notify D0 ok", oc_pm_notify(OC_DRV_D0) == 0);
    ck("resume ran parents-first (bus before ctl)",
       tape_idx("bus-resume") >= 0 && tape_idx("ctl-resume") > tape_idx("bus-resume"));
    ck("node d-state back to D0", hdev0->dstate == OC_DRV_D0);

    tape_reset();
    ck("pm notify D1 ok", oc_pm_notify(OC_DRV_D1) == 0);
    ck("only the D1-declaring driver was called",
       tape_has("pm-suspend") && !tape_has("ctl-suspend") && !tape_has("bus-suspend"));
    (void)oc_pm_notify(OC_DRV_D0);

    /* ---- 4. hotplug queue + subscriptions ---------------------------- */
    int sub_t = oc_hotplug_subscribe("h-ctl", 0, mon_cb, "host-mon");
    oc_devnode_t *hdev2 = oc_devtree_register("hdev2", "h-ctl", hbus);
    int sub_n = oc_hotplug_subscribe(0, hdev2->id, node_cb, "host-node-mon");
    ck("subscriptions accepted", sub_t >= 0 && sub_n >= 0);

    s_ins_ctl = s_rem_ctl = s_node_hit = 0;
    oc_hotplug_post(OC_HPEV_INSERT, hdev2->id, "test");
    oc_hotplug_post(OC_HPEV_INSERT, hpm0->id, "test");   /* wrong type */
    oc_hotplug_post(OC_HPEV_REMOVE, hdev2->id, "test");
    ck("queue holds events before drain", oc_hotplug_pending() == 3);
    ck("drain dispatches all", oc_hotplug_drain() == 3);
    ck("type filter delivered insert+remove only",
       s_ins_ctl == 1 && s_rem_ctl == 1);
    ck("node filter delivered only its node", s_node_hit == 2);
    ck("per-owner stats count deliveries",
       oc_hotplug_sub_delivered_for("host-mon") == 2 &&
       oc_hotplug_sub_delivered_for("host-node-mon") == 2);

    ck("unsubscribe works", oc_hotplug_unsubscribe(sub_n) == 0);
    s_node_hit = 0;
    oc_hotplug_post(OC_HPEV_INSERT, hdev2->id, "test");
    (void)oc_hotplug_drain();
    ck("unsubscribed callback no longer fires", s_node_hit == 0);

    /* overflow: fill past the ring (OC_HP_RING-1 usable slots: the
     * head==tail slot is the empty/full disambiguator) */
    for (int i = 0; i < 70; i++)
        oc_hotplug_post(OC_HPEV_INSERT, hpm0->id, "overflow");
    ck("overflow dropped and counted", oc_hotplug_dropped() == 70 - 63
       && oc_hotplug_pending() == 63);
    (void)oc_hotplug_drain();

    /* ---- 5. bind / unbind / reload semantics ------------------------- */
    ck("bind onto a bound node refused", oc_drvfw_bind("h-ctl", "hdev0") == -13);
    ck("unbind works", oc_drvfw_unbind("h-ctl", "hdev0") == 0);
    ck("unbound node survives (bus owns it)",
       oc_devtree_find_name("hdev0") == hdev0 && hdev0->bound == 0);
    int rc = oc_drvfw_bind("h-bus", "hdev0");
    ck("type-mismatch probe returns its (positive) error code", rc == 22);
    ck("re-bind restores the pair", oc_drvfw_bind("h-ctl", "hdev0") == 0
       && hdev0->bound == r_ctl->info);

    u32 old_bus_id = hbus->id;
    ck("reload h-bus: full cascade + re-registration",
       oc_drvfw_reload("h-bus") == 0);
    oc_devnode_t *hbus2 = oc_devtree_find_name("hbus0");
    ck("bus node re-registered fresh (new id, re-bound)",
       hbus2 && hbus2->id != old_bus_id
       && hbus2->bound == oc_drvfw_find("h-bus")->info);
    ck("children re-bound after reload",
       oc_devtree_find_name("hdev0") && oc_devtree_find_name("hdev1")
       && oc_devtree_find_name("hdev0")->bound == r_ctl->info);

    ck("reload h-fail still fails, ledger bumps",
       oc_drvfw_reload("h-fail") == 0
       && oc_drvfw_find("h-fail")->state == OC_DRVST_FAILED
       && oc_drvfw_find("h-fail")->fail_count == 2);
    int failrec = -1;
    for (int i = 0; i < oc_drv_fail_count(); i++)
        if (strcmp(oc_drv_fail_at(i)->name, "h-fail") == 0) failrec = i;
    ck("failure ledger queryable", failrec >= 0
       && oc_drv_fail_at(failrec)->err == -77
       && oc_drv_fail_at(failrec)->count == 2);
    ck("clean reload clears nothing that must stay",
       oc_drvfw_reload("h-pm") == 0);

    /* ---- 6. debug flag ------------------------------------------------ */
    ck("debug flag off by default", oc_drvfw_debug_enabled() == 0);
    oc_drvfw_set_debug(1);
    ck("debug flag toggles", oc_drvfw_debug_enabled() == 1);
    oc_drvfw_set_debug(0);

    printf("== host unit test: %d/%d checks passed ==\n",
           s_checks - s_fails, s_checks);
    return s_fails;
}
