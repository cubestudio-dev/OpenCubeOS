/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-11-crash_recovery
 * File: kernel/core/core_crash.h
 * Purpose: unified kernel crash-recovery API - panic capture, core
 *          dump, persistent crash log, recovery policy, soft watchdog.
 *
 * Layering (who calls what):
 *
 *   exception entry (arch_exc_dispatch, ring-0 fault)
 *        └─> kernel_panic_frame(reason, frame)      [this module]
 *              ├─ crashlog_record(...)              ring buffer entry
 *              ├─ panic banner + register snapshot  serial + VGA(red)
 *              ├─ stack traceback                   rbp chain + scan
 *              ├─ core_dump_save(...)               memory + /var/crash
 *              ├─ registered panic handlers         L1 observers
 *              ├─ crashlog_flush()                  /var/log/crash.log
 *              └─ policy: HALT (cli;hlt) or REBOOT (8042 + 0xCF9)
 *
 *   watchdog tick (IRQ0) ─ kernel_panic("watchdog timeout ...")
 *   OC_ASSERT(cond)      ─ kernel_panic("assertion failed: ...")
 *   crashtest (shell)    ─ manual triggers of every source above
 */
#ifndef OC_CORE_CRASH_H
#define OC_CORE_CRASH_H

#include "types.h"
#include "arch_idt.h"          /* arch_irq_frame_t */

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ *
 * 1. RECOVERY POLICY (7-extension pair: crash_recovery_set/get_policy)
 *
 * What the kernel does AFTER the panic path finished recording
 * (dump saved, log flushed). Default = HALT.
 * ------------------------------------------------------------------ */
#define CRASH_POLICY_HALT    0     /* cli + hlt safe loop (default)      */
#define CRASH_POLICY_REBOOT  1     /* core_power_reboot(): 8042 + 0xCF9  */

int  crash_recovery_set_policy(int policy);   /* 0 ok, -1 bad policy   */
int  crash_recovery_get_policy(void);         /* CRASH_POLICY_*        */
const char *crash_recovery_policy_name(int policy);

/* ------------------------------------------------------------------ *
 * 2. PANIC CAPTURE (7-extension pair: kernel_panic_register_handler /
 *    kernel_panic)
 *
 * kernel_panic(reason)        - no frame available (assert, watchdog,
 *                               manual crashtest): registers snapshot
 *                               prints live CPU state instead.
 * kernel_panic_frame(...)     - full path from arch_exc_dispatch with
 *                               the exact exception frame.
 *
 * A panic handler sees every panic BEFORE the dump is written; it may
 *   - return 0: not handled, panic continues (dump + halt/reboot)
 *   - return non-zero: the handler claims the situation is recovered
 *     (e.g. it repaired the fault) - the panic path returns and the
 *     caller (exception dispatcher) resumes the faulting frame.
 * Handlers run most-recently-registered first. Max 4.
 * NOTE: a handler that longjmps away (like the TUI guard) must NOT be
 * registered here - the TUI guard is wired directly inside
 * arch_exc_dispatch, BEFORE kernel_panic_frame is reached.
 * ------------------------------------------------------------------ */
#define OC_PANIC_HANDLER_MAX 4

typedef int (*kernel_panic_handler_fn)(const char *reason,
                                       const arch_irq_frame_t *frame);

int  kernel_panic_register_handler(kernel_panic_handler_fn fn);   /* 0 ok */
int  kernel_panic_unregister_handler(kernel_panic_handler_fn fn); /* 0 ok */

/* NOTE: not marked noreturn ON PURPOSE. A registered panic handler
 * that returns non-zero claims recovery - kernel_panic_frame then
 * returns so the exception dispatcher can resume the repaired frame.
 * When no handler recovers (the normal case) the function ends in
 * core_power_halt()/core_power_reboot() and truly never returns. */
void kernel_panic(const char *reason);
void kernel_panic_frame(const char *reason,
                        const arch_irq_frame_t *f);

/* non-fatal variant used by contained faults (TUI guard): records a
 * crashlog entry + serial notice, then RETURNS so the caller can
 * longjmp to its own recovery point. This is how "an app crashed but
 * the kernel survived" is logged without halting the machine. */
void crash_contained(const char *what, const arch_irq_frame_t *f);

/* assertion -> panic source. Keep the condition text in the reason. */
#define OC_ASSERT(cond)                                                \
    do {                                                               \
        if (!(cond))                                                   \
            kernel_panic("assertion failed: " #cond);                 \
    } while (0)

/* ------------------------------------------------------------------ *
 * 3. CORE DUMP (7-extension pair: core_dump_save / core_dump_read)
 *
 * core_dump_save captures, in order, into a boot-time allocated
 * (kmalloc, NOT .bss - the 0x400000 identity window is precious):
 *   header : oc_dump_hdr_t below (magic, version, reason, task, ...)
 *   regs   : full exception frame (22 u64) or live snapshot
 *   task   : tid/pid/name/priority/state + kernel-stack bounds (hdr)
 *   stack  : kernel stack window copy, up to OC_DUMP_STACK_BYTES,
 *            clipped to the valid [stack_base, stack_base+size) range
 *   tables : CR3 + number of mapped PTEs walked (PML4 self-scan)
 * then it tries to persist via VFS to /var/crash/core.dump (works on
 * ramfs rootfs AND on a FAT32 mount - whichever backs /var/crash).
 * If NO filesystem is available the same image is hex-dumped to the
 * serial console (first OC_DUMP_SERIAL_MAX bytes) so the data is not
 * lost. Returns bytes captured, or -1 if even the memory image failed
 * (no dump buffer allocated).
 *
 * core_dump_read copies the last saved image out (up to cap bytes),
 * writes the FULL image length to *out_total. dumpinfo (shell) casts
 * the first bytes to oc_dump_hdr_t and prints the summary fields.
 * ------------------------------------------------------------------ */
#define OC_DUMP_STACK_BYTES   4096    /* kernel-stack window copy     */
#define OC_DUMP_SERIAL_MAX    2048    /* hex-print cap when diskless  */

#define OC_DUMP_REGS_U64      22      /* arch_irq_frame_t, in u64s    */

typedef struct {
    char magic[8];            /* "OCDUMP01"                          */
    u32 hdr_len;              /* sizeof(oc_dump_hdr_t)               */
    u32 image_len;            /* total bytes captured                */
    u64 build;                /* OC_KERNEL_BUILD                     */
    char version[24];         /* OC_RELEASE_VERSION                  */
    char reason[40];          /* short panic reason                  */
    u64 jiffies;
    u64 rtc_epoch;
    u64 cr3;                  /* page-table root at capture time     */
    u64 pte_count;            /* present leaf PTEs walked            */
    u32 tid, pid;
    u32 ring;
    char task[16];
    u64 stack_base, stack_size, stack_rsp;   /* kernel-stack window  */
    u32 stack_copy_len;       /* bytes actually copied               */
    u32 regs_present;         /* 1 = frame captured, 0 = live regs   */
} oc_dump_hdr_t;

int  core_dump_save(const char *reason, const arch_irq_frame_t *f);
int  core_dump_read(void *buf, u32 cap, u32 *out_total);   /* copied   */
u32  core_dump_size(void);                                  /* last len */
int  core_dump_persisted(void);   /* 1 = written to /var/crash/core.dump */

/* ------------------------------------------------------------------ *
 * 4. CRASH LOG (7-extension pair: crashlog_record / crashlog_flush)
 *
 * Ring buffer of the last OC_CRASHLOG_CAP faults, richer than the
 * WP-04 8-entry one: timestamp (jiffies + RTC), vector/source, error
 * code, rip/rsp/cr2, offending task (tid/pid/name/ring). Safe to call
 * from any context: static BSS storage, no allocation, nested-write
 * guard, and the write happens BEFORE any risky printing.
 *
 * Vector field values:
 *   0..31   CPU exception vector (#DE=0, #UD=6, #PF=14, ...)
 *   32      manual panic (kernel_panic from code/crashtest)
 *   33      watchdog timeout
 *   34      assertion failure (OC_ASSERT)
 *   35      contained fault (TUI guard / crash_contained)
 *   36      kernel stack overflow (guard page or canary)
 * ------------------------------------------------------------------ */
#define OC_CRASHLOG_CAP        32
#define OC_CRASH_SRC_PANIC     32
#define OC_CRASH_SRC_WDT       33
#define OC_CRASH_SRC_ASSERT    34
#define OC_CRASH_SRC_CONTAINED 35
#define OC_CRASH_SRC_STACKOVF  36

const char *crash_src_name(u32 vector);

typedef struct {
    u64 jiffies;         /* core_timer_ticks() at record time        */
    u64 rtc_epoch;       /* seconds since 1970 (0 = rtc unavailable) */
    u32 vector;          /* 0..31 exception, 32..36 synthetic source */
    u32 ring;            /* 0 = kernel, 3 = user                     */
    u64 error_code;
    u64 rip;
    u64 rsp;
    u64 cr2;             /* #PF faulting address, 0 otherwise        */
    i32 tid;
    i32 pid;
    char task[16];       /* task name (or "user:<pid>")              */
} crashlog_entry_t;

void crashlog_record(u32 vector, u64 error_code, u64 rip, u64 rsp,
                     u64 cr2, const char *task_name, i32 tid, i32 pid,
                     u32 ring);
int  crashlog_count(void);            /* entries currently stored      */
const crashlog_entry_t *crashlog_get(int index);  /* 0 = oldest, or NULL */
int  crashlog_flush(void);            /* append all to /var/log/crash.log;
                                         returns bytes written, -1 no fs */

/* ------------------------------------------------------------------ *
 * 5. SOFT WATCHDOG (panic source: "watchdog timeout")
 *
 * A periodic core_timer callback checks the heartbeat stamp. The
 * shell (main interactive loop / line editor) kicks it on every key;
 * a kernel that stops servicing input for OC_WDT_TIMEOUT_MS with the
 * watchdog armed is considered lost and panics with the current task
 * context in the banner. crashtest can also blackout the heartbeat on
 * purpose to exercise the path deterministically.
 * ------------------------------------------------------------------ */
#define OC_WDT_TIMEOUT_MS     15000

void crash_wdt_init(void);            /* arm at boot (after timers)    */
void crash_wdt_kick(void);            /* heartbeat: called by shell    */
void crash_wdt_blackout(int on);      /* test hook: stop kicking       */
int  crash_wdt_armed(void);           /* 1 = running                   */
u64  crash_wdt_ms_since_kick(void);   /* diagnostic                    */
void crash_wdt_force_timeout(void);   /* test hook: crashtest wd       */
void core_dump_alloc(void);           /* boot-time buffer reservation  */
void crashlog_boot_alloc(void);       /* boot-time ring reservation    */

#ifdef __cplusplus
}
#endif
#endif /* OC_CORE_CRASH_H */
