/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-12-watchdog_logging
 * File: kernel/core/core_health.c
 * Purpose: health monitor implementation - see core_health.h.
 *
 * The sampler is a 1 s periodic soft timer (core_timer_register_
 * periodic, IRQ0 context): it only reads counters and writes the
 * static sample ring (one u64-pair window guarded by a cli/sti
 * publish) - no allocation, no VFS, safe at IRQ depth.  Alarms are
 * klog ERROR lines (the IRQ-safe ring path; the file channel catches
 * up from task context).  The watchdog linkage feeds the system
 * heartbeat slot (watchdog_feed(0)) while the machine is alive -
 * callers that armed slot 0 get an automatic heartbeat from the
 * health sampler, and a locked-up kernel stops feeding (the WP-11
 * panic path then fires from the real tick).
 */
#include "core_health.h"
#include "lib_string.h"
#include "lib_klog.h"
#include "core_timer.h"
#include "core_sched.h"
#include "core_watchdog.h"
#include "../mem/mem_pmm.h"
#include "../mem/mem_heap.h"
#include "../drivers/block/driver_block_blk.h"
#include "../net/net_core.h"

/* ---- state ---------------------------------------------------------- */
static health_sample_t g_last;
static u64 g_sample_count = 0;

/* CPU window: previous counter pair */
static u64 g_prev_idle_ticks = 0;
static u64 g_prev_total_ticks = 0;

/* alarm dedup: 1 while the alarm condition holds */
static u32 g_alarm_live = 0;
static u32 g_cpu_100_run = 0;     /* consecutive 100%-busy samples */

/* task states: walk the scheduler table through the stats API plus a
 * light state split (core_sched exposes the table via a helper) */
extern int core_sched_task_states(u32 *running, u32 *ready,
                                  u32 *blocked, u32 *total);

/* ---- one sample ------------------------------------------------------ */
static void health_take(health_sample_t *s) {
    memset(s, 0, sizeof(*s));
    s->ts_ms = core_timer_now_ms();

    /* CPU: window over the idle/total tick counters */
    u64 idle_now = 0, total_now = 0;
    core_sched_idle_counters(&idle_now, &total_now);
    u64 dt = total_now - g_prev_total_ticks;
    u64 di = idle_now - g_prev_idle_ticks;
    g_prev_idle_ticks = idle_now;
    g_prev_total_ticks = total_now;
    if (dt == 0) s->cpu_busy_pct = 0;
    else {
        u64 idle_pct = (di * 100ULL) / dt;
        s->cpu_busy_pct = (u32)(idle_pct >= 100 ? 0 : 100 - idle_pct);
    }

    /* memory */
    mem_pmm_stats_t pm;
    mem_pmm_get_stats(&pm);
    s->mem_total_kb = pm.total_pages * 4;          /* x 4 KiB per page */
    s->mem_free_kb  = pm.free_pages * 4;
    s->mem_used_kb  = (pm.total_pages > pm.free_pages)
                      ? (pm.total_pages - pm.free_pages) / 4 : 0;
    mem_heap_stats_t hp;
    mem_heap_get_stats(&hp);
    s->heap_used_bytes = hp.allocated;
    s->heap_free_bytes = hp.free;

    /* tasks */
    {
        u32 run = 0, ready = 0, blocked = 0, total = 0;
        core_sched_task_states(&run, &ready, &blocked, &total);
        s->tasks_running = run;
        s->tasks_ready = ready;
        s->tasks_blocked = blocked;
        s->tasks_total = total;
    }

    /* disk: sum over registered devices */
    {
        int n = driver_block_num_devices();
        for (int i = 0; i < n; i++) {
            driver_block_stats_t bs;
            driver_block_get_stats(i, &bs);
            s->disk_reads += bs.reads;
            s->disk_writes += bs.writes;
            s->disk_read_bytes += bs.read_sectors * 512ULL;
            s->disk_write_bytes += bs.write_sectors * 512ULL;
        }
    }

    /* net */
    {
        net_stats_t ns;
        net_get_stats(&ns);
        s->net_tx_packets = ns.tx_packets;
        s->net_rx_packets = ns.rx_packets;
        s->net_tx_bytes = ns.tx_bytes;
        s->net_rx_bytes = ns.rx_bytes;
    }
}

/* ---- alarms (deduplicated) ------------------------------------------- */
static void health_alarms(health_sample_t *s) {
    u32 live = 0;

    if (s->mem_total_kb > 0) {
        u64 pct = (s->mem_used_kb * 100ULL) / s->mem_total_kb;
        if (pct > 90) live |= HEALTH_ALARM_MEM;
    }
    if (s->tasks_total > 64) live |= HEALTH_ALARM_TASKS;
    if (s->cpu_busy_pct >= 90) {
        if (g_cpu_100_run < 0xFFFFFFFF) g_cpu_100_run++;
        if (g_cpu_100_run >= 10) live |= HEALTH_ALARM_CPU;
    } else g_cpu_100_run = 0;

    /* rising edges only (one ERROR per crossing) */
    u32 rising = live & ~g_alarm_live;
    if (rising & HEALTH_ALARM_MEM)
        klog_write(KLOG_ERROR, "health",
                   "alarm: memory >90%% used (%u/%u KiB)",
                   (u32)s->mem_used_kb, (u32)s->mem_total_kb);
    if (rising & HEALTH_ALARM_TASKS)
        klog_write(KLOG_ERROR, "health",
                   "alarm: %u active tasks (>64)", s->tasks_total);
    if (rising & HEALTH_ALARM_CPU)
        klog_write(KLOG_ERROR, "health",
                   "alarm: CPU >=90%% busy for 10 consecutive seconds");

    s->alarms = live;
    g_alarm_live = live;
}

/* ---- the 1 s sampler -------------------------------------------------- */
static void health_tick(void *ctx) {
    (void)ctx;
    health_sample_t s;
    health_take(&s);
    health_alarms(&s);

    /* publish (short cli window - the sample is ~150 bytes) */
    u64 f;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(f) :: "memory");
    g_last = s;
    g_sample_count++;
    __asm__ volatile("pushfq; popq %0" : "=r"(f) :: "memory");
    if (f & 0x200) __asm__ volatile("sti" ::: "memory");

    /* watchdog linkage (spec 5.7): an ALIVE machine keeps feeding the
     * system heartbeat slot; if the kernel locks up hard enough that
     * this timer stops, the feed stops and the WP-11 panic path (or
     * the armed hardware dog) takes over. */
    watchdog_feed(0);
}

/* ---- init --------------------------------------------------------------- */
void health_init(void) {
    memset(&g_last, 0, sizeof(g_last));
    g_sample_count = 0;
    g_prev_idle_ticks = 0;
    g_prev_total_ticks = 0;
    g_alarm_live = 0;
    g_cpu_100_run = 0;
    /* prime the CPU window so the first sample is meaningful */
    core_sched_idle_counters(&g_prev_idle_ticks, &g_prev_total_ticks);
    core_timer_register_periodic(health_tick, NULL, 1000);
    klog_write(KLOG_INFO, "health", "monitor started (1 s samples, alarms: mem>90%% tasks>64 cpu>=90%%x10s)");
}

/* ===================== L1 extension interface ========================== */
int health_get_cpu(u32 *busy_pct) {
    if (!busy_pct) return -1;
    health_sample_t s;
    health_take(&s);
    *busy_pct = s.cpu_busy_pct;
    return 0;
}

int health_get_mem(u64 *total_kb, u64 *used_kb, u64 *free_kb) {
    if (!total_kb || !used_kb || !free_kb) return -1;
    mem_pmm_stats_t pm;
    mem_pmm_get_stats(&pm);
    *total_kb = pm.total_pages * 4;
    *free_kb  = pm.free_pages * 4;
    *used_kb  = (pm.total_pages > pm.free_pages)
                ? (pm.total_pages - pm.free_pages) * 4 : 0;
    return 0;
}

int health_get_tasks(u32 *total, u32 *running, u32 *ready, u32 *blocked) {
    if (!total || !running || !ready || !blocked) return -1;
    return core_sched_task_states(running, ready, blocked, total);
}

int health_get_io(u64 *disk_read_bytes, u64 *disk_write_bytes,
                  u64 *net_tx_bytes, u64 *net_rx_bytes) {
    if (!disk_read_bytes || !disk_write_bytes || !net_tx_bytes || !net_rx_bytes)
        return -1;
    *disk_read_bytes = *disk_write_bytes = 0;
    int n = driver_block_num_devices();
    for (int i = 0; i < n; i++) {
        driver_block_stats_t bs;
        driver_block_get_stats(i, &bs);
        *disk_read_bytes += bs.read_sectors * 512ULL;
        *disk_write_bytes += bs.write_sectors * 512ULL;
    }
    net_stats_t ns;
    net_get_stats(&ns);
    *net_tx_bytes = ns.tx_bytes;
    *net_rx_bytes = ns.rx_bytes;
    return 0;
}

/* u64 -> decimal into a JSON buffer */
static int hj_num(char *out, int cap, int n, const char *key, u64 v, int comma) {
    char num[24];
    u64_to_str(v, num);
    int w = 0;
    if (comma && n < cap) out[n + w++] = ',';
    for (const char *k = key; *k && n + w < cap - 1; k++) out[n + w++] = *k;
    if (n + w < cap - 1) out[n + w++] = '"';
    if (n + w < cap - 1) out[n + w++] = ':';
    for (char *q = num; *q && n + w < cap - 1; q++) out[n + w++] = *q;
    out[n + w] = 0;
    return n + w;
}
static int hj_str(char *out, int cap, int n, const char *key, const char *val, int comma) {
    int w = 0;
    if (comma && n < cap) out[n + w++] = ',';
    for (const char *k = key; *k && n + w < cap - 1; k++) out[n + w++] = *k;
    if (n + w < cap - 1) out[n + w++] = '"';
    if (n + w < cap - 1) out[n + w++] = ':';
    if (n + w < cap - 1) out[n + w++] = '"';
    for (const char *q = val; *q && n + w < cap - 1; q++) out[n + w++] = *q;
    if (n + w < cap - 1) out[n + w++] = '"';
    out[n + w] = 0;
    return n + w;
}

int health_export(char *out, int cap) {
    if (!out || cap < 256) return -1;
    health_sample_t s;
    health_take(&s);
    health_alarms(&s);

    int n = 0;
    out[n++] = '{';
    n = hj_num(out, cap, n, "\"ts_ms", s.ts_ms, 0);
    n = hj_num(out, cap, n, "\"cpu_busy_pct", s.cpu_busy_pct, 1);
    n = hj_num(out, cap, n, "\"mem_total_kb", s.mem_total_kb, 1);
    n = hj_num(out, cap, n, "\"mem_used_kb", s.mem_used_kb, 1);
    n = hj_num(out, cap, n, "\"mem_free_kb", s.mem_free_kb, 1);
    n = hj_num(out, cap, n, "\"heap_used_bytes", s.heap_used_bytes, 1);
    n = hj_num(out, cap, n, "\"heap_free_bytes", s.heap_free_bytes, 1);
    n = hj_num(out, cap, n, "\"tasks_total", s.tasks_total, 1);
    n = hj_num(out, cap, n, "\"tasks_running", s.tasks_running, 1);
    n = hj_num(out, cap, n, "\"tasks_ready", s.tasks_ready, 1);
    n = hj_num(out, cap, n, "\"tasks_blocked", s.tasks_blocked, 1);
    n = hj_num(out, cap, n, "\"disk_reads", s.disk_reads, 1);
    n = hj_num(out, cap, n, "\"disk_writes", s.disk_writes, 1);
    n = hj_num(out, cap, n, "\"disk_read_bytes", s.disk_read_bytes, 1);
    n = hj_num(out, cap, n, "\"disk_write_bytes", s.disk_write_bytes, 1);
    n = hj_num(out, cap, n, "\"net_tx_packets", s.net_tx_packets, 1);
    n = hj_num(out, cap, n, "\"net_rx_packets", s.net_rx_packets, 1);
    n = hj_num(out, cap, n, "\"net_tx_bytes", s.net_tx_bytes, 1);
    n = hj_num(out, cap, n, "\"net_rx_bytes", s.net_rx_bytes, 1);
    n = hj_num(out, cap, n, "\"alarms", s.alarms, 1);
    n = hj_str(out, cap, n, "\"alarms_text",
               (s.alarms & HEALTH_ALARM_MEM)  ? "mem" :
               (s.alarms & HEALTH_ALARM_TASKS) ? "tasks" :
               (s.alarms & HEALTH_ALARM_CPU)  ? "cpu" : "none", 1);
    if (n < cap - 1) out[n++] = '}';
    if (n < cap - 1) out[n++] = '\n';
    out[n] = 0;
    return n;
}

int  health_last(health_sample_t *out) {
    if (!out) return -1;
    *out = g_last;
    return 0;
}
u64  health_sample_count(void) { return g_sample_count; }
