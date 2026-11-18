/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP08 - drivers/framework: hotplug event queue.
 *
 * The ring.  post() is interrupt-context safe: irqsave spinlock, copy
 * the payload in, drop-and-count on overflow.  drain() is the single
 * consumer: it dequeues under the same lock one event at a time and
 * runs the subscriber callbacks (core_hotplug_sub.c) OUTSIDE the lock
 * in process context - exactly the "IRQ produces, process consumes"
 * split the contract asks for. */
#include "core_hotplug.h"
#include "core_devtree.h"
#include "core_lib_string.h"
#include "core_sync.h"
#include "core_timer.h"
#include "core_lib_log.h"

#define OC_HP_RING 64

static oc_hotplug_ev_t s_ring[OC_HP_RING];
static volatile int s_head, s_tail;
static spinlock_t s_lock;
static int s_dropped;
static int s_dispatched;

/* The subscription registry (core_hotplug_sub.c). */
int oc_hotplug_sub_dispatch(const oc_hotplug_ev_t *ev);

int oc_hotplug_post(int ev_type, u32 node_id, const char *source)
{
    if (ev_type != OC_HPEV_INSERT && ev_type != OC_HPEV_REMOVE) return -1;
    if (!source) source = "unknown";

    /* Snapshot the node fields BEFORE taking the lock: the node may
     * be unregistered by the time the event is drained, and finding
     * it here can walk the (lock-free) tree safely. */
    char name[OC_HPEV_NAME_MAX] = "?";
    char type[OC_HPEV_TYPE_MAX] = "?";
    oc_devnode_t *n = oc_devtree_find(node_id);
    if (n) {
        strncpy(name, n->name, sizeof(name) - 1); name[sizeof(name) - 1] = 0;
        strncpy(type, n->type, sizeof(type) - 1); type[sizeof(type) - 1] = 0;
    }

    spin_lock(&s_lock);
    int next = (s_tail + 1) % OC_HP_RING;
    if (next == s_head) {
        s_dropped++;
        spin_unlock(&s_lock);
        lib_log_info("hotplug: queue full, event dropped");
        return -1;
    }
    oc_hotplug_ev_t *e = &s_ring[s_tail];
    e->type = (u8)ev_type;
    e->node_id = node_id;
    strncpy(e->node_name, name, sizeof(e->node_name) - 1);
    e->node_name[sizeof(e->node_name) - 1] = 0;
    strncpy(e->node_type, type, sizeof(e->node_type) - 1);
    e->node_type[sizeof(e->node_type) - 1] = 0;
    e->timestamp = core_timer_now_ms();
    e->source = source;
    s_tail = next;
    spin_unlock(&s_lock);
    return 0;
}

int oc_hotplug_drain(void)
{
    int n = 0;
    for (;;) {
        oc_hotplug_ev_t ev;
        spin_lock(&s_lock);
        if (s_head == s_tail) { spin_unlock(&s_lock); break; }
        ev = s_ring[s_head];
        s_head = (s_head + 1) % OC_HP_RING;
        spin_unlock(&s_lock);

        (void)oc_hotplug_sub_dispatch(&ev);   /* callbacks run unlocked */
        s_dispatched++;
        n++;
    }
    return n;
}

int oc_hotplug_pending(void)
{
    spin_lock(&s_lock);
    int n = (s_tail - s_head + OC_HP_RING) % OC_HP_RING;
    spin_unlock(&s_lock);
    return n;
}

int oc_hotplug_dropped(void)     { return s_dropped; }
int oc_hotplug_dispatched(void)  { return s_dispatched; }
