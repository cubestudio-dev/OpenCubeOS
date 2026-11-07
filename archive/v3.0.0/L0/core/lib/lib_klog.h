/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-12-watchdog_logging
 * File: kernel/lib/lib_klog.h
 * Purpose: the unified kernel log (klog) - one API, five levels, four
 * output channels, filters and rotation.
 *
 * Levels (each with an INDEPENDENT on/off switch - klog_set_level_enabled):
 *   0 DEBUG   1 INFO   2 WARN   3 ERROR   4 FATAL
 *
 * Channels:
 *   serial   COM1  - verbosity gate: klog_set_level()          (live)
 *   console  VGA   - verbosity gate: klog_set_level()          (live)
 *   ring     RAM   - 1024-entry ring, ALWAYS recorded (dmesg)  (boot+live)
 *   file     VFS   - /var/log/kernel.log after mount, live     append
 *
 * Ring lines are recorded UNFILTERED for every enabled level; the
 * active filter (klog_filter_set) applies to the LIVE console/serial
 * channels and to the REPLAY (dmesg/logcat) - both directions the spec
 * asks for.  The file channel records everything the ring records.
 *
 * Format of one line (identical in ring replay, file and channels):
 *   [   12.345] WARN  net: link up (e1000)
 *   ts = seconds.milliseconds since klog_init (core_timer_now_ms;
 *   0.000 during the earliest boot stages, before the PIT runs).
 *
 * Rotation (klog_rotate / automatic at the threshold):
 *   /var/log/kernel.log   -> kernel.log.1 -> kernel.log.2 -> kernel.log.3
 *   The rename chain runs only after every pending line is appended
 *   (flush-first), the ring stays the source of truth across the whole
 *   window, and the last generation beyond the keep-count is dropped.
 *
 * IRQ context: klog_write is safe from interrupt handlers - the ring
 * slot claim is a short cli/sti critical section, serial is the polled
 * WP-02 COM1 writer (arch_exc_ser_puts) and the VGA console lock is
 * IRQ-safe (BUG-0319).  The FILE channel is skipped when interrupts
 * are disabled at call time; those lines stay in the ring and are
 * appended by the next task-context writer / flush / timer kick.
 */
#ifndef OC_LIB_KLOG_H
#define OC_LIB_KLOG_H

#include "types.h"

/* ---- levels ------------------------------------------------------- */
#define KLOG_DEBUG 0
#define KLOG_INFO  1
#define KLOG_WARN  2
#define KLOG_ERROR 3
#define KLOG_FATAL 4
#define KLOG_LVL_MIN 0
#define KLOG_LVL_MAX 4

/* ---- rotation defaults -------------------------------------------- */
#define KLOG_ROTATE_BYTES  (1024u * 1024u)   /* rotate past 1 MiB      */
#define KLOG_ROTATE_KEEP   3                 /* kernel.log.1 .. .3     */
#define KLOG_PATH          "/var/log/kernel.log"

#ifdef __cplusplus
extern "C" {
#endif

/* ================= L1 extension interface (WP-12) ==================
 * Documented in docs/EXTENSIONS_WP12.md. Every function below is part
 * of the L0 ABI (declared here, exported by the kernel image).
 * =================================================================== */

/* Emit one line. Returns the ring index of the entry, or -1 when the
 * level is disabled by the per-level mask (nothing was recorded).
 * fmt supports %s %d %u %x %c %% (the freestanding set the kernel
 * uses everywhere - tui_printw shares the same grammar). */
int  klog_write(int level, const char *module, const char *fmt, ...);

/* Live-channel (serial+console) verbosity gate: lines with level <
 * the gate are still RECORDED to ring+file but not printed live.
 * -1 mutes both live channels entirely. Returns the previous gate. */
int  klog_set_level(int level);
int  klog_get_level(void);

/* Independent per-level switches (spec 5.3: "each level can be
 * toggled independently").  A disabled level is not recorded at all.
 * Returns the previous state, -1 on a bad level. */
int  klog_set_level_enabled(int level, int on);
int  klog_get_level_enabled(int level);

/* Persist everything the ring holds (and everything pending on the
 * file channel) into KLOG_PATH.  Returns appended bytes, or -1 when
 * no filesystem is mounted / open failed.  Creates /var/log first. */
int  klog_flush(void);

/* Panic-path variant: sync unconditionally (interrupts are already
 * off; same VFS-write depth crashlog_flush uses).  WP-12. */
int  klog_flush_panic(void);

/* Rotate now: append pending lines, then shift .2->.3, .1->.2,
 * kernel.log->.1 and start a fresh file.  Returns 0, or -1 when no
 * filesystem is mounted.  Runs automatically at KLOG_ROTATE_BYTES. */
int  klog_rotate(void);

/* Set the ACTIVE FILTER (spec 5.6).  It applies to the live channels
 * AND to ring replay (dmesg/logcat):
 *   level   -1 = any level, 0..4 = that level AND ABOVE
 *   module  NULL or "" = any module, else exact module-name match
 *   keyword NULL or "" = any text,  else substring of the message
 * Only one filter is active at a time (the last call wins). */
void klog_filter_set(int level, const char *module, const char *keyword);
void klog_filter_clear(void);

/* ===================== kernel-internal surface ===================== */

void klog_init(void);         /* earliest boot: zero state, defaults   */
void klog_grow(void);         /* after mem_heap_init: full 1024 ring   */
/* 3.0.0-WP03 G5: the on-disk channel lives in services/klog
 * (klog_file.c - the fs sink).  It installs this hook after the
 * first filesystem mounts; until then the ring simply holds the
 * entries.  (klog_on_mount itself is declared in services/klog.) */
typedef void (*klog_emit_hook_fn)(void);
void klog_set_emit_hook(klog_emit_hook_fn fn);

/* replay accessors for the sink: walk entries by absolute seq */
u64  klog_total_seq(void);    /* absolute entries ever written       */
u64  klog_oldest_seq(void);   /* first seq still inside the ring     */
int  klog_entry_render(u64 *seq_io, char *line, int cap); /* -1 = end */
int  klog_ring_count(void);   /* entries currently held (<= 1024)      */
int  klog_ring_get(int idx, char *out, int cap); /* formatted replay   */
void klog_clear(void);        /* wipe the ring (logctl clear)          */

/* Record-only entry (no live channels) - lib_log mirrors its boot
 * stage lines through this so dmesg shows the boot log without the
 * console printing them twice. */
void klog_record(int level, const char *module, const char *msg);

/* Filter state for the replay commands. */
int  klog_filter_active(void);
int  klog_filter_level(void);
const char *klog_filter_module(void);
const char *klog_filter_keyword(void);

/* File-channel bookkeeping (dumpinfo-style reporting). */
u64  klog_dropped(void);       /* ring slots overwritten since boot    */

/* 3.0.0-WP03 G5: the file-channel API (klog_file_bytes/open, the
 * rotate setters) moved to services/klog/klog_file.c - the on-disk
 * channel is fs-side policy, not ring mechanism. */

const char *klog_level_name(int level);   /* "DEBUG".."FATAL" or "?"   */

/* 3.0.0-WP03 G4: render the whole ring (oldest -> newest) as
 * formatted lines into out[cap]; NUL-terminated.  Returns the byte
 * count (0 = empty ring / bad args).  The OCIRE log agent's
 * KLOG_SNAPSHOT op consumes this. */
int  klog_snapshot_text(char *out, int cap);

#ifdef __cplusplus
}
#endif
#endif /* OC_LIB_KLOG_H */
