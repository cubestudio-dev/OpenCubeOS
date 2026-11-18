/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP08 - drivers/framework: hotplug subscriptions.
 *
 * The subscriber table: a driver registers one callback with an
 * optional device-type filter and/or node-id filter; the drain pass
 * (core_hotplug_queue.c) offers every event to every matching
 * subscriber.  subscribe/unsubscribe run in process context (the
 * table lock is a plain spinlock - drain takes it only for the
 * matching scan, never around a callback). */
#include "core_hotplug.h"
#include "core_lib_string.h"
#include "core_sync.h"

#define OC_HP_SUB_MAX 16

typedef struct {
    int used;
    char type_filter[OC_HPEV_TYPE_MAX];   /* empty = every type */
    u32  node_filter;                     /* 0 = every node */
    oc_hotplug_cb_t cb;
    char owner[OC_HPEV_NAME_MAX];
    int delivered;                        /* events matched so far */
} oc_hp_sub_t;

static oc_hp_sub_t s_sub[OC_HP_SUB_MAX];
static spinlock_t s_lock;

int oc_hotplug_subscribe(const char *type_filter, u32 node_filter,
                         oc_hotplug_cb_t cb, const char *owner)
{
    if (!cb) return -1;

    spin_lock(&s_lock);
    int id = -1;
    for (int i = 0; i < OC_HP_SUB_MAX; i++) {
        if (!s_sub[i].used) { id = i; break; }
    }
    if (id >= 0) {
        oc_hp_sub_t *s = &s_sub[id];
        memset(s, 0, sizeof(*s));
        s->used = 1;
        if (type_filter && type_filter[0]) {
            strncpy(s->type_filter, type_filter, sizeof(s->type_filter) - 1);
        }
        s->node_filter = node_filter;
        s->cb = cb;
        if (owner)
            strncpy(s->owner, owner ? owner : "?", sizeof(s->owner) - 1);
    }
    spin_unlock(&s_lock);
    return id;
}

int oc_hotplug_unsubscribe(int sub_id)
{
    if (sub_id < 0 || sub_id >= OC_HP_SUB_MAX) return -1;
    spin_lock(&s_lock);
    int rc = s_sub[sub_id].used ? 0 : -1;
    if (rc == 0) s_sub[sub_id].used = 0;
    spin_unlock(&s_lock);
    return rc;
}

/* Offer one event to every matching subscriber.  Called only from
 * oc_hotplug_drain() (process context, no queue lock held).  Returns
 * the number of subscribers that received it. */
int oc_hotplug_sub_dispatch(const oc_hotplug_ev_t *ev)
{
    int hits = 0;
    for (int i = 0; i < OC_HP_SUB_MAX; i++) {
        if (!s_sub[i].used) continue;
        if (s_sub[i].type_filter[0] &&
            strcmp(s_sub[i].type_filter, ev->node_type) != 0)
            continue;
        if (s_sub[i].node_filter && s_sub[i].node_filter != ev->node_id)
            continue;
        s_sub[i].cb(ev);           /* outside every lock, by contract */
        s_sub[i].delivered++;
        hits++;
    }
    return hits;
}

int oc_hotplug_sub_delivered_for(const char *owner)
{
    if (!owner || !owner[0]) return 0;
    int n = 0;
    for (int i = 0; i < OC_HP_SUB_MAX; i++)
        if (s_sub[i].used && strcmp(s_sub[i].owner, owner) == 0)
            n += s_sub[i].delivered;
    return n;
}
