/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-12-watchdog_logging
 * File: kernel/core/core_watchdog.h
 * Purpose: the unified watchdog subsystem - hardware (Intel TCO /
 * ACPI WDAT) + software (per-task monitored slots + the WP-11 system
 * heartbeat) behind ONE API.
 *
 * HARDWARE (spec 5.1):
 *   Probe order at boot:
 *     1. ACPI WDAT table (driver_acpi_find_table("WDAT")) - the
 *        entries' GAS registers drive every action.
 *     2. Intel TCO on the LPC bridge (PCI 00:1f.0): PMBASE (config
 *        0x40) + 0x60 -> TCO_RLD/CNT/TMR.  Present on QEMU q35
 *        (ICH9-LPC); absent on the default pc machine (PIIX3), where
 *        the probe honestly reports "none".
 *   Like the WP-11 software heartbeat, the hardware dog is probed
 *   but NOT armed at boot (same documented decision: long synchronous
 *   shell commands own the shell task and cannot kick).  Arming is
 *   explicit: `watchdog hw start <sec>` / watchdog_start() on the
 *   system slot / WDAT arming instructions.
 *
 *   Soft-hard coordination (spec 5.1): when BOTH are armed, the
 *   software check runs FIRST on every tick and takes the WP-11 panic
 *   path (dump + crashlog + policy); the hardware TCO remains the
 *   last-resort hard reset if the panic path itself never completes -
 *   the hardware timeout is set >= 2x the software one whenever the
 *   framework arms both.
 *
 * SOFTWARE (spec 5.2): up to 8 monitored slots.  A task registers with
 *   a name + timeout + policy, feeds periodically; the 1 s tick
 *   checks every ARMED slot and applies the policy:
 *
 *     WD_POLICY_PANIC  - kernel_panic("watchdog timeout: ...") - the
 *                        full WP-11 crash path (dump + log + policy)
 *     WD_POLICY_REBOOT - klog ERROR + core_power_reboot() directly
 *     WD_POLICY_KILL   - klog ERROR + core_sched_task_exited(tid) -
 *                        the monitored task is killed, kernel lives
 *     WD_POLICY_LOG    - klog ERROR once per timeout episode (no
 *                        action; re-arms on the next feed)
 *
 * SYSTEM slot: watchdog_init() registers slot 0 ("system-heartbeat")
 *   wrapping the WP-11 crash_wdt shell heartbeat semantics (feed on
 *   every shell line, OC_WDT_TIMEOUT_MS default 15 s, policy PANIC) -
 *   crashtest wd keeps working unchanged; watchdog_feed(0) is the
 *   L1-visible kick.
 */
#ifndef OC_CORE_WATCHDOG_H
#define OC_CORE_WATCHDOG_H

#include "types.h"

/* ---- policies -------------------------------------------------------- */
#define WD_POLICY_PANIC  0
#define WD_POLICY_REBOOT 1
#define WD_POLICY_KILL   2
#define WD_POLICY_LOG    3

/* ---- limits ---------------------------------------------------------- */
#define WD_MAX_SLOTS      8
#define WD_HW_TIMEOUT_MIN 2      /* TCO/WDAT seconds floor           */
#define WD_HW_TIMEOUT_MAX 63     /* 6-bit TCO timer field            */

#ifdef __cplusplus
extern "C" {
#endif

/* ================= L1 extension interface (WP-12) ==================== */

/* Register a monitored slot. Returns the slot id >= 0, or:
 *   -1 table full, -2 bad args (NULL/empty name, timeout 0,
 *   bad policy).  The slot starts STOPPED; watchdog_start() arms it.
 * Policy see WD_POLICY_*. */
int  watchdog_register(const char *name, u32 timeout_ms, int policy);

/* Arm / feed / stop / re-time a slot. 0 ok, -1 bad id, -2 not
 * registered.  set_timeout also works while running (the next check
 * uses it).  feed() refreshes the deadline - call it from the
 * monitored task's loop. */
int  watchdog_start(int id);
int  watchdog_feed(int id);
int  watchdog_stop(int id);
int  watchdog_set_timeout(int id, u32 timeout_ms);

/* ===================== kernel-internal surface ======================= */

void watchdog_init(void);          /* after PCI + ACPI + timer init   */

/* ---- slot introspection (shell + health) ----------------------------- */
typedef struct {
    char name[16];
    u32  timeout_ms;
    int  policy;
    int  running;                  /* armed                            */
    u64  last_feed_ms;
    u64  timeouts;                 /* episodes seen so far            */
    int  tid;                      /* creator tid (KILL policy)       */
} watchdog_info_t;

int  watchdog_count(void);         /* highest used slot id + 1        */
int  watchdog_get_info(int id, watchdog_info_t *out);  /* 0 / -1      */

/* ---- hardware layer (shell `watchdog hw` + coordination) ------------- */
int  watchdog_hw_present(void);    /* 1 = TCO or WDAT found           */
const char *watchdog_hw_name(void);/* "Intel TCO" / "ACPI WDAT" / ""  */
int  watchdog_hw_start(u32 seconds);   /* arm + set timeout           */
int  watchdog_hw_feed(void);           /* kick (TCO_RLD / WDAT ping)  */
int  watchdog_hw_stop(void);           /* halt (TMR_HLT / WDAT stop)  */
int  watchdog_hw_set_timeout(u32 seconds);
u32  watchdog_hw_timeout(void);        /* current seconds             */
u64  watchdog_hw_base(void);           /* TCOBASE / 0 (diagnostics)   */
int  watchdog_hw_armed(void);
int  watchdog_hw_gcs_noreboot(void);  /* live GCS bit5 readback, -1 n/a */

#ifdef __cplusplus
}
#endif
#endif /* OC_CORE_WATCHDOG_H */
