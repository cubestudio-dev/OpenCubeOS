/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-12-watchdog_logging
 * File: kernel/core/core_health.h
 * Purpose: system health monitor - samples the live counters every
 * second, keeps a short history for `health watch`, applies alarm
 * thresholds (spec 5.7: "when a metric crosses a threshold, write a
 * log entry and trigger the watchdog policy") and exports JSON.
 *
 * Metric sources (all pre-existing, no new instrumentation except the
 * two idle-accounting tick counters in core_sched_tick):
 *   CPU     core_sched_idle_counters() deltas over the 1 s window
 *           (idle% -> busy% = 100 - idle%)
 *   memory  mem_pmm_get_stats() (pages total/used/free) +
 *           mem_heap_get_stats() (kmalloc arena)
 *   tasks   core_sched_get_stats() (total switches, active tasks)
 *           + task-state walk for ready/blocked/running split
 *   disk    driver_block_get_stats() summed over devices
 *           (reads/writes, sectors -> bytes at 512)
 *   net     net_get_stats() (tx/rx packets + bytes)
 *
 * Alarms (evaluated once per sample, deduplicated: one ERROR per
 * crossing, re-armed when the metric returns under the threshold):
 *   mem_used > 90%          -> klog ERROR "health alarm: memory >90%"
 *   tasks active > 64       -> klog ERROR
 *   cpu busy == 100% for 10 consecutive samples (a fully locked
 *                              machine) -> klog ERROR + (watchdog
 *                              linkage) arms a system-heartbeat check
 * The watchdog linkage (spec: "trigger the watchdog policy"):
 * alarms feed the soft-slot table's system-heartbeat slot as an
 * EXTRA feed source - i.e. an alarmed-but-alive system keeps feeding
 * the dog, a dead one does not (the WP-11 panic path takes over).
 */
#ifndef OC_CORE_HEALTH_H
#define OC_CORE_HEALTH_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- one sample ----------------------------------------------- */
typedef struct {
    u64  ts_ms;             /* core_timer_now_ms() at sample time   */
    u32  cpu_busy_pct;      /* 0..100 over the last window          */
    u64  mem_total_kb;      /* physical RAM in KiB                  */
    u64  mem_used_kb;
    u64  mem_free_kb;
    u64  heap_used_bytes;   /* kmalloc arena                        */
    u64  heap_free_bytes;
    u32  tasks_total;       /* scheduler slots in use               */
    u32  tasks_running;
    u32  tasks_ready;
    u32  tasks_blocked;
    u64  disk_reads;        /* cumulative since boot                */
    u64  disk_writes;
    u64  disk_read_bytes;
    u64  disk_write_bytes;
    u64  net_tx_packets;
    u64  net_rx_packets;
    u64  net_tx_bytes;
    u64  net_rx_bytes;
    u32  alarms;            /* bitmask of live alarms (see below)   */
} health_sample_t;

/* alarm bits (sample->alarms) */
#define HEALTH_ALARM_MEM    (1u << 0)   /* memory > 90% used          */
#define HEALTH_ALARM_TASKS  (1u << 1)   /* active tasks > 64           */
#define HEALTH_ALARM_CPU    (1u << 2)   /* 100% busy for 10 samples    */

/* ================= L1 extension interface (WP-12) ================== */

/* Take a fresh sample right now (also what the 1 s timer does).
 * Returns 0, or -1 when the subsystem is not initialized. */
int  health_get_cpu(u32 *busy_pct);      /* instantaneous CPU busy %   */
int  health_get_mem(u64 *total_kb, u64 *used_kb, u64 *free_kb);
int  health_get_tasks(u32 *total, u32 *running, u32 *ready, u32 *blocked);
int  health_get_io(u64 *disk_read_bytes, u64 *disk_write_bytes,
                   u64 *net_tx_bytes, u64 *net_rx_bytes);

/* Render the CURRENT sample as one JSON object into out (cap bytes).
 * Returns the length written, or -1 on bad args.  Shape:
 * {"ts_ms":1234,"cpu_busy_pct":7,"mem_total_kb":524160,...} */
int  health_export(char *out, int cap);

/* ===================== kernel-internal surface ====================== */

void health_init(void);                  /* after net+block+sched init */

/* last sample copy (0/-1), plus the raw counter window for watch */
int  health_last(health_sample_t *out);

/* number of samples taken since boot (for the watch header) */
u64  health_sample_count(void);

#ifdef __cplusplus
}
#endif
#endif /* OC_CORE_HEALTH_H */
