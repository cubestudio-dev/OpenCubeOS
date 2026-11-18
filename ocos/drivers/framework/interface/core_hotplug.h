/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS 3.0.0-WP08 - drivers/framework: generic hotplug events.
 *
 * A single framework-wide event queue + subscription registry.
 * Concrete buses (PCI WP-09a, USB WP-14a) will call oc_hotplug_post()
 * from their interrupt / enumeration code; drivers subscribe with a
 * type or node filter and consume through the drain pass.
 *
 * Concurrency contract: post() is interrupt-context safe (irqsave
 * spinlock around the ring); drain() runs in process context and is
 * the ONLY place callbacks fire - never from the poster's context.
 * The queue is fixed-depth; an overflow drops the event, counts it
 * and logs (drop-and-count beats blocking an IRQ).
 *
 * Ownership: the framework delivers; the driver that registered the
 * node and executes remove() owns resource recovery. */
#ifndef OC_CORE_HOTPLUG_H
#define OC_CORE_HOTPLUG_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OC_HPEV_NAME_MAX 24
#define OC_HPEV_TYPE_MAX 16

enum oc_hpev_type {
    OC_HPEV_INSERT = 1,        /* a device node appeared */
    OC_HPEV_REMOVE = 2,        /* a device node went away */
};

/* The event snapshot.  Self-contained on purpose: a REMOVE event is
 * often drained AFTER the node was unregistered, so the payload
 * copies the node's name/type instead of holding a pointer into
 * freed memory.  source is a static string owned by the poster
 * (driver name / "framework"). */
typedef struct {
    u8  type;                          /* enum oc_hpev_type */
    u8  _pad[7];
    u32 node_id;                       /* device node id (reference)  */
    char node_name[OC_HPEV_NAME_MAX];
    char node_type[OC_HPEV_TYPE_MAX];
    u64 timestamp;                     /* ms since boot              */
    const char *source;                /* origin identifier          */
} oc_hotplug_ev_t;

/* ---- Post (IRQ-safe) ---------------------------------------------------
 * Enqueues one event.  0 = queued; -1 = queue full (dropped, counted). */
int oc_hotplug_post(int ev_type, u32 node_id, const char *source);

/* ---- Subscribe / unsubscribe ------------------------------------------
 * filter: type_filter = 0 -> every type; node_filter = 0 -> every node.
 * cb runs from oc_hotplug_drain() in process context.  Returns a
 * subscription id >= 0, or -1 (table full / null cb). */
typedef void (*oc_hotplug_cb_t)(const oc_hotplug_ev_t *ev);

int oc_hotplug_subscribe(const char *type_filter, u32 node_filter,
                         oc_hotplug_cb_t cb, const char *owner);
int oc_hotplug_unsubscribe(int sub_id);

/* ---- Consume -----------------------------------------------------------
 * Drains the queue: every pending event goes to every matching
 * subscriber callback, in FIFO order.  Returns the number of events
 * dispatched (0 when idle).  Safe to call with the queue locked by
 * an in-flight post on another CPU (the drain takes the lock per
 * dequeue step). */
int oc_hotplug_drain(void);

/* ---- Stats (diagnostics) ---------------------------------------------- */
int oc_hotplug_pending(void);
int oc_hotplug_dropped(void);
int oc_hotplug_dispatched(void);

/* Events actually DELIVERED to the subscriptions of one owner (the
 * `drv info` surface prints this for subscribing drivers).  Counts
 * only events that passed that subscription's filter. */
int oc_hotplug_sub_delivered_for(const char *owner);

#ifdef __cplusplus
}
#endif

#endif /* OC_CORE_HOTPLUG_H */
