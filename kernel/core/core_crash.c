/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-11-crash_recovery
 * File: kernel/core/core_crash.c
 * Purpose: unified crash recovery - kernel_panic, core dump, persistent
 *          crash log, recovery policy, soft watchdog. See core_crash.h
 *          for the layering diagram and the L1 contract.
 *
 * Robustness rules for everything in this file:
 *   - the panic path may run with a corrupted heap, a broken
 *     framebuffer or a wedged disk: every subsystem is used
 *     best-effort, serial output first, static storage only;
 *   - a panic inside the panic (nested) stops recursing: one serial
 *     line then cli;hlt (same policy as the P0fix2 BUG-0041 guard);
 *   - nothing here allocates at crash time: the dump buffer is
 *     allocated ONCE at boot (kmalloc before anything can break).
 */
#include "core_crash.h"

#include "lib_string.h"        /* strcpy/strlen/memcpy/memset        */
#include "screen_console.h"    /* VGA best-effort output             */
#include "screen_fb.h"         /* screen_fb_rgb                      */
#include "core_power.h"        /* core_power_halt / core_power_reboot*/
#include "core_sched.h"        /* core_kthread_current / task_t      */
#include "core_timer.h"        /* core_timer_ticks / register_periodic*/
#include "core_rtc.h"          /* core_rtc_unix_now                  */
#include "core_usermode.h"     /* user_process_current               */
#include "mem_heap.h"          /* kmalloc / kfree                    */
#include "mem_pmm.h"
#include "mem_vmm.h"
#include "fs_vfs.h"            /* crashlog_flush / core dump persist */

/* Baked at compile time (Makefile -D). */
#ifndef OC_RELEASE_VERSION
#define OC_RELEASE_VERSION "dev"
#endif
#ifndef OC_KERNEL_BUILD
#define OC_KERNEL_BUILD 0
#endif

/* WP-11 size decision: the whole crash path is perf-irrelevant (it runs
 * once, at the worst possible moment) but its -O2 inline expansion
 * pushed the kernel image past the 0x400000 identity-window boundary.
 * Optimize THIS translation unit for size. */
#pragma GCC optimize("Os")

/* Capture the LIVE CPU registers (no fault frame available) into the
 * arch_irq_frame_t u64 layout, so the dump image and the banner print
 * have ONE layout. Shared by core_dump_save and panic_print_regs:
 * TWO asm blocks of 8 outputs - 16 "=r" outputs in one block exceeds
 * the allocatable register set. */
static void crash_live_regs(u64 r[22]) {
    u64 rax, rbx, rcx, rdx, rsi, rdi, rbp, rsp;
    u64 r8, r9, r10, r11, r12, r13, r14, r15;
    __asm__ volatile(
        "mov %%rax, %0\n  mov %%rbx, %1\n  mov %%rcx, %2\n"
        "mov %%rdx, %3\n  mov %%rsi, %4\n  mov %%rdi, %5\n"
        "mov %%rbp, %6\n  mov %%rsp, %7\n"
        : "=r"(rax), "=r"(rbx), "=r"(rcx), "=r"(rdx),
          "=r"(rsi), "=r"(rdi), "=r"(rbp), "=r"(rsp):: "memory");
    __asm__ volatile(
        "mov %%r8,  %0\n  mov %%r9,  %1\n  mov %%r10, %2\n"
        "mov %%r11, %3\n  mov %%r12, %4\n  mov %%r13, %5\n"
        "mov %%r14, %6\n  mov %%r15, %7\n"
        : "=r"(r8), "=r"(r9), "=r"(r10), "=r"(r11),
          "=r"(r12), "=r"(r13), "=r"(r14), "=r"(r15));
    r[0] = r15; r[1] = r14; r[2] = r13; r[3] = r12;
    r[4] = r11; r[5] = r10; r[6] = r9;  r[7] = r8;
    r[8] = rdi; r[9] = rsi; r[10] = rbp;
    r[11] = rdx; r[12] = rcx; r[13] = rbx; r[14] = rax;
    r[15] = 0;   /* int_no (synthetic) */
    r[16] = 0;   /* error code        */
    r[17] = (u64)(uintptr_t)&crash_live_regs;  /* ~caller rip marker */
    r[18] = 8;   /* cs   */
    r[19] = 0x202; /* rflags guess */
    r[20] = rsp;
    r[21] = 16;  /* ss   */
}

/* WP-11: plausibility filter for user_proc_t pointers found in a
 * panic context. A burned stack can corrupt anything the task was
 * touching (and a #DF arrives after the corruption already happened),
 * so the panic path only trusts a proc whose pid is sane and whose
 * task link matches - anything else is reported as (corrupt?). */
static user_proc_t *crash_valid_proc(user_proc_t *up) {
    if (!up) return NULL;
    if (!up->alive) return NULL;
    if (up->pid <= 0 || up->pid > 1000000) return NULL;
    task_t *t = core_kthread_current();
    if (t && up->tid != t->tid) return NULL;
    return up;
}

/* ================================================================== *
 * serial console (self-contained COM1 polling - the panic path must
 * not depend on any subsystem that may itself be crashed)
 * ================================================================== */
static void cr_outb(u16 p, u8 v) { __asm__ volatile("outb %0, %1" :: "a"(v), "Nd"(p)); }
static u8   cr_inb(u16 p)        { u8 v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(p)); return v; }
#define CR_COM1 0x3F8
static void cr_putc(char c) {
    int spins = 0;
    while ((cr_inb(CR_COM1 + 5) & 0x20) == 0) {
        if (++spins > 200000) return;   /* dead UART: give up, no hang */
    }
    cr_outb(CR_COM1, (u8)c);
}
static void cr_puts(const char *s) { while (*s) cr_putc(*s++); }
static void cr_hex64(u64 v) {
    static const char hex[] = "0123456789ABCDEF";
    char buf[17];
    for (int i = 15; i >= 0; i--) { buf[i] = hex[v & 0xF]; v >>= 4; }
    buf[16] = 0;
    cr_puts(buf);
}
static void cr_hex8(u64 v) {
    static const char hex[] = "0123456789ABCDEF";
    char buf[3];
    buf[0] = hex[(v >> 4) & 0xF];
    buf[1] = hex[v & 0xF];
    buf[2] = 0;
    cr_puts(buf);
}
static void cr_dec(u64 v) {
    char buf[21];
    int i = 20;
    if (v == 0) { cr_putc('0'); return; }
    buf[i--] = 0;
    while (v) { buf[i--] = '0' + (v % 10); v /= 10; }
    cr_puts(&buf[i + 1]);
}

/* ================================================================== *
 * 1. recovery policy
 * ================================================================== */
static int g_policy = CRASH_POLICY_HALT;

int crash_recovery_set_policy(int policy) {
    if (policy != CRASH_POLICY_HALT && policy != CRASH_POLICY_REBOOT)
        return -1;
    g_policy = policy;
    return 0;
}

int crash_recovery_get_policy(void) { return g_policy; }

const char *crash_recovery_policy_name(int policy) {
    if (policy == CRASH_POLICY_REBOOT) return "REBOOT";
    return "HALT";
}

/* ================================================================== *
 * 2. panic handler chain
 * ================================================================== */
static kernel_panic_handler_fn g_panic_handlers[OC_PANIC_HANDLER_MAX];

int kernel_panic_register_handler(kernel_panic_handler_fn fn) {
    if (!fn) return -1;
    for (int i = 0; i < OC_PANIC_HANDLER_MAX; i++) {
        if (g_panic_handlers[i] == NULL) {
            g_panic_handlers[i] = fn;
            return 0;
        }
    }
    return -2;   /* chain full */
}

int kernel_panic_unregister_handler(kernel_panic_handler_fn fn) {
    if (!fn) return -1;
    for (int i = 0; i < OC_PANIC_HANDLER_MAX; i++) {
        if (g_panic_handlers[i] == fn) {
            g_panic_handlers[i] = NULL;
            return 0;
        }
    }
    return -3;
}

/* ================================================================== *
 * 3. crash log ring buffer (supersedes the WP-04 8-entry buffer)
 *
 * Storage note: the ring lives in a boot-time kmalloc block (NOT
 * .bss - the 0x400000 identity window is full). It is allocated ONCE
 * in crashlog_boot_alloc() before anything can crash and never freed,
 * so a later heap corruption cannot un-reserve it; if the allocation
 * fails at boot the ring is simply disabled (records are dropped). */
static crashlog_entry_t *g_log = NULL;
static int g_log_count = 0;          /* entries stored (<= CAP)       */
static int g_log_total = 0;          /* total ever recorded (overflow */
                                     /* indicator for the listing)    */
static volatile int g_log_busy = 0;  /* nested-write guard            */

void crashlog_boot_alloc(void) {
    if (!g_log)
        g_log = kmalloc(sizeof(crashlog_entry_t) * OC_CRASHLOG_CAP);
}

const char *crash_src_name(u32 vector) {
    switch (vector) {
    case 0:  return "#DE Divide Error";
    case 1:  return "#DB Debug";
    case 2:  return "NMI";
    case 3:  return "#BP Breakpoint";
    case 4:  return "#OF Overflow";
    case 5:  return "#BR Bound Range";
    case 6:  return "#UD Invalid Opcode";
    case 7:  return "#NM Device NA";
    case 8:  return "#DF Double Fault";
    case 10: return "#TS Invalid TSS";
    case 11: return "#NP Segment NP";
    case 12: return "#SS Stack-Segment";
    case 13: return "#GP General Protection";
    case 14: return "#PF Page Fault";
    case 16: return "#MF x87 FPU";
    case 17: return "#AC Alignment Check";
    case 18: return "#MC Machine Check";
    case 19: return "#XM SIMD FP";
    case 31: return "#SX Security";
    case OC_CRASH_SRC_PANIC:     return "manual/panic";
    case OC_CRASH_SRC_WDT:       return "watchdog-timeout";
    case OC_CRASH_SRC_ASSERT:    return "assertion-failed";
    case OC_CRASH_SRC_CONTAINED: return "contained(TUI)";
    case OC_CRASH_SRC_STACKOVF:  return "kstack-overflow";
    default: return "exception";
    }
}

void crashlog_record(u32 vector, u64 error_code, u64 rip, u64 rsp,
                     u64 cr2, const char *task_name, i32 tid, i32 pid,
                     u32 ring) {
    if (!g_log) return;              /* boot alloc failed: disabled  */
    /* nested calls (a fault while recording) are dropped, not nested */
    if (g_log_busy) return;
    g_log_busy = 1;

    crashlog_entry_t *e;
    if (g_log_count < OC_CRASHLOG_CAP) {
        e = &g_log[g_log_count++];
    } else {
        /* ring: shift down, append at the end (same scheme as WP-04,
         * 32 entries are cheap to move even inside a fault) */
        for (int i = 0; i < OC_CRASHLOG_CAP - 1; i++) g_log[i] = g_log[i + 1];
        e = &g_log[OC_CRASHLOG_CAP - 1];
    }
    g_log_total++;

    e->jiffies     = core_timer_ticks();
    e->rtc_epoch   = core_rtc_unix_now();   /* 0 when RTC unreadable */
    e->vector      = vector;
    e->ring        = ring;
    e->error_code  = error_code;
    e->rip         = rip;
    e->rsp         = rsp;
    e->cr2         = cr2;
    e->tid         = tid;
    e->pid         = pid;
    e->task[0]     = 0;
    if (task_name) {
        for (int i = 0; i < (int)sizeof(e->task) - 1 && task_name[i]; i++)
            e->task[i] = task_name[i];
    }
    g_log_busy = 0;
}

int crashlog_count(void) { return g_log_count; }

const crashlog_entry_t *crashlog_get(int index) {
    if (index < 0 || index >= g_log_count) return NULL;
    return &g_log[index];
}

/* append the whole ring to /var/log/crash.log (VFS: ramfs rootfs or a
 * mounted FAT32 both satisfy "a filesystem is mounted"). Returns the
 * bytes appended, or -1 when no fs / open failed. Safe during panic:
 * static line buffer, O_CREAT|O_APPEND|O_WRONLY. */
int crashlog_flush(void) {
    static char line[160];
    if (!g_log || g_log_count <= 0) return 0;

    /* ensure /var/log exists (mkdir best-effort, EEXIST is fine) */
    fs_vfs_mkdir("/var");
    fs_vfs_mkdir("/var/log");

    int fd = fs_vfs_open("/var/log/crash.log",
                         VFS_O_WRONLY | VFS_O_CREAT | VFS_O_APPEND);
    if (fd < 0) return -1;

    int total = 0;
    for (int i = 0; i < g_log_count; i++) {
        const crashlog_entry_t *e = &g_log[i];
        char num[24];

        strcpy(line, "[");
        u64_to_str(e->rtc_epoch ? e->rtc_epoch : e->jiffies, num);
        strcat(line, num);
        strcat(line, e->rtc_epoch ? "s] " : "t] ");

        strcat(line, crash_src_name(e->vector));
        strcat(line, " vec=");
        u64_to_str(e->vector, num); strcat(line, num);
        strcat(line, " ring=");
        u64_to_str(e->ring, num); strcat(line, num);
        strcat(line, " err=0x");
        u64_to_hex(e->error_code, num, 16); strcat(line, num);
        strcat(line, " rip=0x");
        u64_to_hex(e->rip, num, 16); strcat(line, num);
        strcat(line, " rsp=0x");
        u64_to_hex(e->rsp, num, 16); strcat(line, num);
        if (e->cr2) {
            strcat(line, " cr2=0x");
            u64_to_hex(e->cr2, num, 16); strcat(line, num);
        }
        if (e->task[0]) {
            strcat(line, " task=");
            strcat(line, e->task);
        }
        if (e->pid > 0) {
            strcat(line, " pid=");
            u64_to_str((u64)e->pid, num); strcat(line, num);
        }
        strcat(line, "\n");

        int n = fs_vfs_write(fd, line, (int)strlen(line));
        if (n < 0) { fs_vfs_close(fd); return -1; }
        total += n;
    }
    fs_vfs_close(fd);
    return total;
}

/* ================================================================== *
 * 4. core dump
 * ================================================================== */
#define OC_DUMP_MAGIC   "OCDUMP01"

static u8  *g_dump_buf = NULL;      /* kmalloc'd at boot              */
static u32  g_dump_len = 0;         /* last image length              */
static int  g_dump_persisted = 0;   /* 1 = /var/crash/core.dump written */
static int  g_dump_serial_dumped = 0; /* 1 = no-fs fallback printed   */

void core_dump_alloc(void) {
    /* called once from kmain, BEFORE anything can crash: the buffer
     * is the header + frame + 4 KiB stack window */
    if (!g_dump_buf)
        g_dump_buf = kmalloc(sizeof(oc_dump_hdr_t) + OC_DUMP_REGS_U64 * 8
                             + OC_DUMP_STACK_BYTES);
}

/* count present leaf PTEs by walking the CURRENT CR3 (only 2 MiB of
 * low PML4 entries are scanned to stay bounded in panic context) */
static u64 core_dump_walk_ptes(void) {
    u64 cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    u64 pml4 = cr3 & ~0xFFFULL;
    u64 count = 0;
    /* self-map trick unavailable: PML4 is identity-mapped, walk it */
    for (int i = 0; i < 64; i++) {                 /* first 128 GiB */
        u64 pdpe = *(volatile u64*)(pml4 + (u64)i * 8);
        if (!(pdpe & 1)) continue;
        u64 pdp = pdpe & ~0xFFFULL;
        for (int j = 0; j < 512; j++) {
            u64 pde = *(volatile u64*)(pdp + (u64)j * 8);
            if (!(pde & 1)) continue;
            if (pde & 0x80) { count += 512; continue; }   /* 2 MiB page */
            u64 pt = pde & ~0xFFFULL;
            for (int k = 0; k < 512; k++) {
                u64 pte = *(volatile u64*)(pt + (u64)k * 8);
                if (pte & 1) count++;
            }
        }
    }
    return count;
}

int core_dump_save(const char *reason, const arch_irq_frame_t *f) {
    if (!g_dump_buf) return -1;
    g_dump_persisted = 0;
    g_dump_serial_dumped = 0;

    task_t *t = core_kthread_current();
    user_proc_t *up = crash_valid_proc(user_process_current());

    oc_dump_hdr_t *h = (oc_dump_hdr_t *)g_dump_buf;
    memset(g_dump_buf, 0, sizeof(oc_dump_hdr_t) + OC_DUMP_REGS_U64 * 8);

    memcpy(h->magic, OC_DUMP_MAGIC, 8);
    h->hdr_len   = (u32)sizeof(oc_dump_hdr_t);
    h->build     = (u64)OC_KERNEL_BUILD;
    h->jiffies   = core_timer_ticks();
    h->rtc_epoch = core_rtc_unix_now();
    {
        const char *v = OC_RELEASE_VERSION;
        for (int i = 0; i < 23 && v[i]; i++) h->version[i] = v[i];
    }
    if (reason) {
        for (int i = 0; i < 39 && reason[i]; i++) h->reason[i] = reason[i];
    }
    h->tid  = t ? (u32)t->tid : 0;
    h->pid  = up ? (u32)up->pid : 0;
    h->ring = f ? (u32)(f->cs & 3) : 0;
    if (t) {
        for (int i = 0; i < 15 && t->name[i]; i++) h->task[i] = t->name[i];
    } else if (up) {
        h->task[0] = 'u'; h->task[1] = ':'; h->task[2] = 0;
    }

    /* registers: the exact exception frame when we have one */
    u64 *regs = (u64 *)(g_dump_buf + sizeof(oc_dump_hdr_t));
    if (f) {
        const u64 *src = (const u64 *)f;
        for (int i = 0; i < 22; i++) regs[i] = src[i];
        h->regs_present = 1;
    } else {
        /* live snapshot in the SAME layout as the exception frame */
        crash_live_regs(regs);
        h->regs_present = 0;
    }
    h->cr3 = 0;
    __asm__ volatile("mov %%cr3, %0" : "=r"(h->cr3));
    h->pte_count = core_dump_walk_ptes();

    /* kernel stack window: clip to the owning task's bounds */
    u8 *stack_dst = g_dump_buf + sizeof(oc_dump_hdr_t) + OC_DUMP_REGS_U64 * 8;
    {
        u64 rsp = f ? f->rsp : regs[20];
        if (t && t->stack_base && t->stack_size) {
            u64 base = (u64)(uintptr_t)t->stack_base;
            u64 size = t->stack_size;
            h->stack_base = base;
            h->stack_size = size;
            h->stack_rsp  = rsp;
            u64 lo = (rsp > OC_DUMP_STACK_BYTES) ? rsp - OC_DUMP_STACK_BYTES : 0;
            if (lo < base) lo = base;
            u64 hi = rsp;
            if (hi > base + size) hi = base + size;
            if (hi > lo) {
                u32 n = (u32)(hi - lo);
                if (n > OC_DUMP_STACK_BYTES) n = OC_DUMP_STACK_BYTES;
                memcpy(stack_dst, (const void *)(uintptr_t)lo, n);
                h->stack_copy_len = n;
            } else {
                h->stack_copy_len = 0;
            }
        } else {
            h->stack_rsp = rsp;
            h->stack_copy_len = 0;
        }
        /* WP-11 fix: the scheduler-recorded window can MISS the real
         * stack - the boot-context task (kmain/idle) runs on the boot
         * stack, while t->stack_base points at a PMM page that was
         * never used as a stack. When the window produced nothing,
         * fall back to a raw 4 KiB window below the live rsp - but
         * ONLY for a plausible kernel rsp (a #DF frame carries garbage
         * like 0xFFFFFFFFFFFFF040) and only over MAPPED pages (kthread
         * guard pages are deliberately not-present). */
        if (h->stack_copy_len == 0 && rsp > 0x1000 &&
            rsp < 0x40000000000ULL) {
            /* window = [page(rsp), rsp) - only if that page is mapped:
             * a guard-page overflow panic arrives with rsp INSIDE the
             * unmapped guard page, and a burned stack has nothing left
             * worth capturing; copying there would re-fault inside the
             * panic. Record "stack destroyed" (copy_len 0) instead. */
            u64 lo = rsp & ~0xFFFULL;
            u64 ptmp;
            if (mem_vmm_is_mapped(mem_vmm_kernel_as(), lo, &ptmp)) {
                u32 n = (u32)(rsp - lo);
                memcpy(stack_dst, (const void *)(uintptr_t)lo, n);
                h->stack_copy_len = n;
                h->stack_base = lo;
                h->stack_size = n;
            }
        }
    }

    g_dump_len = (u32)(sizeof(oc_dump_hdr_t) + OC_DUMP_REGS_U64 * 8
                       + h->stack_copy_len);

    /* persist: /var/crash/core.dump on whichever fs backs /var/crash */
    fs_vfs_mkdir("/var");
    fs_vfs_mkdir("/var/crash");
    int fd = fs_vfs_open("/var/crash/core.dump",
                         VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC);
    if (fd >= 0) {
        int n = fs_vfs_write(fd, g_dump_buf, (int)g_dump_len);
        fs_vfs_close(fd);
        if (n == (int)g_dump_len) {
            g_dump_persisted = 1;
            return (int)g_dump_len;
        }
    }

    /* no filesystem (or write failed): full image out the serial line
     * so the data survives - hex dump, capped at OC_DUMP_SERIAL_MAX */
    cr_puts("\r\n[core-dump] no filesystem - dumping ");
    cr_dec(g_dump_len);
    cr_puts(" bytes over serial (hex, cap ");
    cr_dec(OC_DUMP_SERIAL_MAX);
    cr_puts(")\r\n");
    u32 cap = g_dump_len < OC_DUMP_SERIAL_MAX ? g_dump_len
                                              : (u32)OC_DUMP_SERIAL_MAX;
    for (u32 off = 0; off < cap; off += 16) {
        cr_puts("  ");
        cr_hex64((u64)off);
        cr_puts(": ");
        for (u32 k = 0; k < 16 && off + k < cap; k++)
            cr_hex8(g_dump_buf[off + k]);
        cr_puts("\r\n");
    }
    if (g_dump_len > cap) {
        cr_puts("  ... truncated (");
        cr_dec(g_dump_len - cap);
        cr_puts(" more bytes)\r\n");
    }
    g_dump_serial_dumped = 1;
    return (int)g_dump_len;
}

int core_dump_read(void *buf, u32 cap, u32 *out_total) {
    if (out_total) *out_total = g_dump_len;
    if (!buf || cap == 0 || g_dump_len == 0) return 0;
    u32 n = cap < g_dump_len ? cap : g_dump_len;
    memcpy(buf, g_dump_buf, n);
    return (int)n;
}

u32  core_dump_size(void)      { return g_dump_len; }
int  core_dump_persisted(void) { return g_dump_persisted; }

/* ================================================================== *
 * 5. soft watchdog
 * ================================================================== */
static volatile u64 g_wdt_last_kick_ms = 0;
static volatile int g_wdt_armed = 0;
static volatile int g_wdt_blackout = 0;

void crash_wdt_kick(void) {
    if (g_wdt_armed && !g_wdt_blackout)
        g_wdt_last_kick_ms = core_timer_now_ms();
}

void crash_wdt_blackout(int on) { g_wdt_blackout = on ? 1 : 0; }
int  crash_wdt_armed(void)      { return g_wdt_armed; }

u64  crash_wdt_ms_since_kick(void) {
    if (!g_wdt_armed) return 0;
    return core_timer_now_ms() - g_wdt_last_kick_ms;
}

static void crash_wdt_tick(void *ctx) {
    (void)ctx;
    if (!g_wdt_armed) return;
    u64 since = core_timer_now_ms() - g_wdt_last_kick_ms;
    if (since > (u64)OC_WDT_TIMEOUT_MS) {
        char reason[72];
        task_t *t = core_kthread_current();
        strcpy(reason, "watchdog timeout: no shell heartbeat for ");
        u64_to_str(since / 1000ULL, reason + strlen(reason));
        strcat(reason, "s (task=");
        if (t) strcat(reason, t->name); else strcat(reason, "?");
        strcat(reason, ")");
        kernel_panic(reason);
    }
}

void crash_wdt_init(void) {
    g_wdt_last_kick_ms = core_timer_now_ms();
    if (core_timer_register_periodic(crash_wdt_tick, NULL, 1000) == 0)
        g_wdt_armed = 1;
    /* WP-11 decision (documented honestly): the watchdog tick is
     * REGISTERED but stays DISARMED after boot. A long synchronous
     * shell command (cryptotest, tcptest, an E2E suite) owns the shell
     * task and cannot kick - arming by default would fire a false
     * panic under exactly the workloads the regression gates run.
     * Arming is explicit: crashtest wd arms + forces the timeout so
     * the FULL path (tick -> check -> panic -> dump -> policy) is
     * exercised for real. */
    g_wdt_armed = 0;
}

/* WP-11: test hook for `crashtest wd` - arm the watchdog and rewind
 * the heartbeat far past the deadline, so the NEXT real 1 s tick
 * (crash_wdt_tick, the production check path) raises the panic. */
void crash_wdt_force_timeout(void) {
    g_wdt_last_kick_ms = 0;              /* epoch of the timer clock  */
    if (core_timer_now_ms() < (u64)OC_WDT_TIMEOUT_MS)
        g_wdt_last_kick_ms = (u64)-OC_WDT_TIMEOUT_MS - 1; /* underflow-safe */
    g_wdt_armed = 1;
}

/* ================================================================== *
 * 6. panic banner + register snapshot + traceback
 * ================================================================== */
static volatile int g_in_panic = 0;

/* kernel image bounds for the "looks like a code address" filter.
 * Provided by linker.ld (__end_of_kernel); the low bound is the 1 MiB
 * load address. */
extern char __end_of_kernel[];

static int looks_like_code(u64 v) {
    return v >= 0x100000ULL && v < (u64)(uintptr_t)__end_of_kernel;
}

/* VGA best-effort: red text if the console is alive. Every call is
 * wrapped by the caller's g_in_panic guard so a #PF in here falls
 * into the nested-panic halt instead of recursing. */
static void vga_line(const char *s) {
    u32 saved = screen_console_get()->fg_pixel;
    screen_console_get()->fg_pixel = screen_fb_rgb(0xFF, 0x40, 0x40);
    screen_console_puts(s);
    screen_console_putc('\n');
    screen_console_get()->fg_pixel = saved;
}

static void panic_print_task(void) {
    task_t *t = core_kthread_current();
    user_proc_t *up = crash_valid_proc(user_process_current());
    cr_puts("  task  : ");
    if (t) {
        cr_puts("tid=");
        cr_dec((u64)t->tid);
        cr_puts(" name=");
        cr_puts(t->name);
    } else {
        cr_puts("(none)");
    }
    if (up) {
        cr_puts(" user-pid=");
        cr_dec((u64)up->pid);
    }
    cr_puts("\r\n");
}

static void panic_print_regs(const arch_irq_frame_t *f, const u64 live[22]) {
    u64 r[22];
    int have_frame = 0;
    if (f) {
        const u64 *src = (const u64 *)f;
        for (int i = 0; i < 22; i++) r[i] = src[i];
        have_frame = 1;
    } else if (live) {
        for (int i = 0; i < 22; i++) r[i] = live[i];
    } else {
        /* no frame, no pre-captured snapshot: take one now */
        crash_live_regs(r);
    }
    /* layout (both cases): 0..7 = r15..r8, 8=rdi, 9=rsi, 10=rbp,
     * 11=rdx, 12=rcx, 13=rbx, 14=rax, 15=int_no, 16=err, 17=rip,
     * 18=cs, 19=rflags, 20=rsp, 21=ss */

    static const char *nm[15] = {
        "r15", "r14", "r13", "r12", "r11", "r10", "r9 ", "r8 ",
        "rdi", "rsi", "rbp", "rdx", "rcx", "rbx", "rax"
    };
    cr_puts(have_frame
        ? "  Registers (exception frame):\r\n"
        : "  Registers (live snapshot):\r\n");
    for (int row = 0; row < 4; row++) {
        cr_puts("    ");
        for (int col = 0; col < 4; col++) {
            int i = row * 4 + col;
            if (i >= 15) break;
            cr_puts(nm[i]); cr_puts("=");
            cr_hex64(r[i]);
            cr_puts(" ");
        }
        cr_puts("\r\n");
    }
    if (have_frame) {
        cr_puts("    rip=");  cr_hex64(r[17]);
        cr_puts("  cs=0x");   cr_hex64(r[18] & 0xFFFF);
        cr_puts("  ss=0x");   cr_hex64(r[21] & 0xFFFF);
        cr_puts("  rflags=0x"); cr_hex64(r[19]);
        cr_puts("\r\n    rsp="); cr_hex64(r[20]);
        cr_puts("  int=");    cr_dec(r[15]);
        cr_puts("  err=0x");  cr_hex64(r[16]);
        cr_puts("\r\n");
        if (r[15] == 14) {
            u64 cr2;
            __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
            cr_puts("    cr2="); cr_hex64(cr2);
            cr_puts("  (faulting address)\r\n");
        }
    } else {
        cr_puts("    rbp="); cr_hex64(r[10]);
        cr_puts("  rsp=");  cr_hex64(r[20]);
        cr_puts("  rip~0x"); cr_hex64(r[17]);
        cr_puts("  (caller approx)\r\n");
    }
    {
        u64 cr3;
        __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
        cr_puts("    cr3="); cr_hex64(cr3);
        cr_puts("\r\n");
    }
}

/* Traceback. -O2 kernels omit frame pointers, so we do BOTH:
 *   (a) if the frame's rbp chain looks valid (each link inside the
 *       current kernel stack window), follow it - exact;
 *   (b) a stack scan from rsp upward printing values that look like
 *       code addresses - approximate, clearly labelled. */
static void panic_print_trace(const arch_irq_frame_t *f, const u64 live[22]) {
    task_t *t = core_kthread_current();
    /* WP-11 fix: without an exception frame the traceback used rsp=0
     * and printed "(rsp outside the kernel stack window)". Use the
     * pre-captured live snapshot (f==NULL path) so the scan works for
     * manual panics / watchdog / assertions too. Layout: r[10]=rbp,
     * r[20]=rsp. */
    u64 rsp = f ? f->rsp : (live ? live[20] : 0);
    u64 rbp = f ? f->rbp : (live ? live[10] : 0);
    /* WP-11: sanity gate - a #DF frame carries a garbage rsp (e.g.
     * 0xFFFFFFFFFFFFF040); scanning "the stack" there would fault and
     * end the panic half-printed. Only scan plausible kernel memory. */
    if (rsp < 0x1000ULL || rsp >= 0x40000000000ULL) rsp = 0;
    u64 stack_lo = 0, stack_hi = 0;
    if (t && t->stack_base && t->stack_size) {
        stack_lo = (u64)(uintptr_t)t->stack_base;
        stack_hi = stack_lo + t->stack_size;
    }
    /* same fix as the dump: the scheduler window can miss the REAL
     * stack (boot-context task). If rsp is outside [lo,hi) or the
     * window is unknown, scan a raw 4 KiB window around rsp (safe:
     * the whole low 4 GiB is identity-mapped). */
    if (rsp && (stack_hi == 0 || rsp < stack_lo || rsp >= stack_hi)) {
        stack_lo = rsp;
        stack_hi = rsp + 4096;
    }

    cr_puts("  Call trace:\r\n");

    /* (a) rbp chain */
    int n = 0;
    if (rbp && stack_lo && rbp >= stack_lo && rbp < stack_hi) {
        u64 fp = rbp;
        while (n < 8 && fp >= stack_lo && fp + 16 <= stack_hi) {
            u64 ret = *(volatile u64 *)(fp + 8);
            u64 next = *(volatile u64 *)(fp);
            cr_puts("     "); cr_dec((u64)n); cr_puts(": 0x");
            cr_hex64(ret);
            cr_puts(looks_like_code(ret) ? "" : " (?)");
            cr_puts("\r\n");
            n++;
            if (next <= fp || next >= stack_hi) break;
            fp = next;
        }
    }
    if (n == 0)
        cr_puts("     (frame-pointer chain unavailable - O2 kernel)\r\n");

    /* (b) stack scan */
    cr_puts("    stack scan (approximate return addrs, innermost first):\r\n");
    if (rsp && stack_hi > rsp) {
        int shown = 0;
        for (u64 p = (rsp + 7) & ~7ULL;
             p + 8 <= stack_hi && shown < 12; p += 8) {
            /* WP-11 4.4: skip unmapped pages (kthread guard pages) so
             * the scan itself cannot #PF - we are inside the panic
             * path, a nested fault would end everything. */
            u64 ptmp;
            if (!mem_vmm_is_mapped(mem_vmm_kernel_as(), p & ~0xFFFULL, &ptmp))
                { p |= 0xFFFULL; continue; }   /* jump to next page */
            u64 v = *(volatile u64 *)p;
            if (looks_like_code(v)) {
                cr_puts("     0x"); cr_hex64(p);
                cr_puts(" -> 0x"); cr_hex64(v);
                cr_puts("\r\n");
                shown++;
            }
        }
        if (shown == 0) cr_puts("     (none found)\r\n");
    } else {
        cr_puts("     (rsp outside the kernel stack window)\r\n");
    }
}

/* WP-11: #DF continuation. Runs on the FRESH IST1 panic stack (see
 * the switch below): by the time a Double Fault fires, the faulting
 * task's stack has a not-present guard page below and its return
 * addresses were overwritten by whatever burned it - continuing the
 * panic on that stack crashed mid-banner twice (observed #GP in the
 * decimal printer, then a jump to 0x17). This function re-does the
 * whole panic sequence with ~8 KiB of guaranteed-good stack. */
/* ---- the panic path itself --------------------------------------- */
void kernel_panic_frame(const char *reason, const arch_irq_frame_t *f) {
    /* nested panic: one line, then the safe loop. Never recurse. */
    if (g_in_panic) {
        cr_puts("\r\n*** NESTED PANIC INSIDE PANIC - HALTING ***\r\n");
        __asm__ volatile("cli");
        for (;;) __asm__ volatile("hlt");
    }
    g_in_panic = 1;
    __asm__ volatile("cli");

    /* WP-11 FINAL: a #DF is handled ENTIRELY at dispatcher depth, with
     * the PROVEN printer. Bring-up rounds showed: (a) any continuation
     * beyond the dispatcher banner (fresh-stack switch, register dump,
     * traceback, dump/log writes) re-faults in the hostile #DF context
     * (destroyed stack + garbage frame fields, observed
     * rsp=0xFFFFFFFFFFFFF040); (b) the -Os cr_puts printer locked up
     * mid-word there. What IS empirically 100% reliable: this file's
     * lib_string calls (the dispatcher's attribution block) and the
     * ser_puts banner printed by arch_exceptions.c. So the #DF panic
     * assembles ONE static line buffer with strcpy/strcat and emits it
     * through arch_exc_ser_puts - the same primitives, same depth. No
     * record, no dump, no flush for #DF (stated in KNOWN_ISSUES; every
     * other panic source exercises those paths fully). */
    if (f && f->int_no == 8) {
        extern void arch_exc_ser_puts(const char *s);
        static char b[192];
        char n[24];
        task_t *t8 = core_kthread_current();
        strcpy(b, "\r\n\n*** KERNEL PANIC: ");
        strcat(b, reason ? reason : "(no reason)");
        if (t8) { strcat(b, " [task="); strcat(b, t8->name); strcat(b, "]"); }
        strcat(b, " ***\r\n  source: #DF Double Fault err=0x");
        u64_to_hex(f->error_code, n, 16); strcat(b, n);
        strcat(b, " rip=0x");
        u64_to_hex(f->rip, n, 16); strcat(b, n);
        strcat(b, "  [handled at dispatcher depth]\r\n");
        strcat(b, "  core dump: skipped in #DF mode (see KNOWN_ISSUES)\r\n  policy: ");
        strcat(b, crash_recovery_policy_name(g_policy));
        if (g_policy == CRASH_POLICY_REBOOT) {
            strcat(b, " - rebooting NOW\r\n\n");
            arch_exc_ser_puts(b);
            core_power_reboot();
        }
        strcat(b, " - system stopped (safe halt loop)\r\n\n");
        arch_exc_ser_puts(b);
        core_power_halt();
        for (;;) __asm__ volatile("hlt");
    }

    /* WP-11: NORMALIZE CR3. The panic may have fired while running on
     * a USER address space (user-task launcher, syscall path); the
     * user PML4 does not map the kernel identity window outside
     * [0x400000, 0x600000), so every later read here (the traceback
     * scan, the dump stack copy, even g_log in the heap) would #PF
     * and trip the nested-panic halt before producing any output.
     * Switching to the kernel CR3 first makes the whole panic path
     * safe from any context. */
    mem_vmm_switch_as(mem_vmm_kernel_as());

    /* capture the live registers ONCE (only used when f == NULL) so
     * the banner, the traceback and the dump see the same snapshot */
    u64 live[22];
    if (!f) crash_live_regs(live);

    /* 1. record BEFORE anything risky (static storage, no alloc) */
    {
        task_t *t = core_kthread_current();
        user_proc_t *up = crash_valid_proc(user_process_current());
        u32 vec = f ? (u32)f->int_no : OC_CRASH_SRC_PANIC;
        u64 cr2 = 0;
        if (f && f->int_no == 14)
            __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        crashlog_record(vec, f ? f->error_code : 0,
                        f ? f->rip : (u64)(uintptr_t)&kernel_panic,
                        f ? f->rsp : 0, cr2,
                        t ? t->name : (up ? "user" : "?"),
                        t ? t->tid : -1, up ? up->pid : -1,
                        f ? (u32)(f->cs & 3) : 0);
    }

    /* 2. banner on serial (authoritative) + VGA (best effort).
     * (hex build number: keeps the banner division-free)
     * cr_dec's decimal division on the hostile shallow stack. */
    cr_puts("\r\n\n");
    cr_puts("*** KERNEL PANIC: ");
    cr_puts(reason ? reason : "(no reason)");
    /* stack-exhaustion annotation: a #DF with the frame rsp below the
     * current task's stack base IS a burned stack (the dispatcher-side
     * attribution may miss it when the timer had switched tasks). */
    task_t *ta = core_kthread_current();
    if (f && f->int_no == 8 && ta && ta->stack_base && ta->stack_size &&
        f->rsp < (u64)(uintptr_t)ta->stack_base) {
        cr_puts(" [KERNEL STACK OVERFLOW: exhausted, task=");
        cr_puts(ta->name);
        cr_puts("]");
    }
    cr_puts(" ***\r\n");
    cr_puts("  kernel: Open Cube OS ");
    cr_puts(OC_RELEASE_VERSION);
    cr_puts(" (build 0x");
    cr_hex64((u64)OC_KERNEL_BUILD);
    cr_puts(")\r\n");
    if (f) {
        cr_puts("  source: exception ");
        cr_dec(f->int_no);
        cr_puts(" (");
        cr_puts(crash_src_name((u32)f->int_no));
        cr_puts(") err=0x");
        cr_hex64(f->error_code);
        cr_puts("\r\n");
    } else {
        cr_puts("  source: ");
        cr_puts(crash_src_name(OC_CRASH_SRC_PANIC));
        cr_puts("\r\n");
    }
    panic_print_task();
    panic_print_regs(f, f ? NULL : live);
    panic_print_trace(f, f ? NULL : live);

    {
        char vline[96];
        strcpy(vline, "*** KERNEL PANIC: ");
        if (reason) {
            int i = strlen(vline);
            for (int k = 0; reason[k] && i < 90; k++) vline[i++] = reason[k];
            vline[i] = 0;
        }
        strcat(vline, " ***");
        vga_line(vline);
        vga_line("Registers and call trace on the serial console.");
    }

    /* 3. core dump (memory image + /var/crash/core.dump or serial) */
    {
        int n = core_dump_save(reason, f);
        cr_puts("  core dump: ");
        if (n < 0) {
            cr_puts("UNAVAILABLE (no boot-time buffer)");
        } else {
            cr_dec((u64)n);
            cr_puts(" bytes captured, ");
            if (g_dump_persisted) {
                cr_puts("persisted to /var/crash/core.dump");
            } else if (g_dump_serial_dumped) {
                cr_puts("written to serial (no filesystem)");
            } else {
                cr_puts("persist FAILED, serial fallback above");
            }
        }
        cr_puts("\r\n");
    }

    /* 4. L1 panic handlers (most recent first). A handler returning
     * non-zero claims recovery: we clear the panic flag, re-enable
     * interrupts and RETURN - the exception dispatcher will resume
     * the (hopefully repaired) frame. */
    for (int i = OC_PANIC_HANDLER_MAX - 1; i >= 0; i--) {
        if (g_panic_handlers[i]) {
            int handled = g_panic_handlers[i](reason, f);
            if (handled) {
                cr_puts("*** panic handler claimed recovery - resuming ***\r\n");
                g_in_panic = 0;
                __asm__ volatile("sti");
                return;
            }
        }
    }

    /* 5. flush the crash log to /var/log/crash.log (best effort) */
    {
        int n = crashlog_flush();
        cr_puts("  crash log: ");
        cr_dec((u64)g_log_total);
        cr_puts(" total, flushed ");
        if (n < 0) cr_puts("FAILED (no filesystem)");
        else { cr_dec((u64)n); cr_puts(" bytes to /var/log/crash.log"); }
        cr_puts("\r\n");
    }

    /* 6. policy: HALT or REBOOT (both disable interrupts first) */
    cr_puts("  policy: ");
    cr_puts(crash_recovery_policy_name(g_policy));
    if (g_policy == CRASH_POLICY_REBOOT) {
        cr_puts(" - rebooting NOW\r\n\n");
        vga_line("Panic recorded - rebooting (see serial log).");
        core_power_reboot();
    }
    cr_puts(" - system stopped (safe halt loop)\r\n\n");
    vga_line("Panic recorded - system halted (see serial log).");
    core_power_halt();
    /* core_power_halt never returns; keep the compiler honest */
    for (;;) __asm__ volatile("hlt");
}

void kernel_panic(const char *reason) {
    kernel_panic_frame(reason, NULL);
}

/* contained fault: log + one serial line, then RETURN to the caller
 * (the TUI guard longjmps away right after). */
void crash_contained(const char *what, const arch_irq_frame_t *f) {
    task_t *t = core_kthread_current();
    crashlog_record(f ? (u32)f->int_no : OC_CRASH_SRC_CONTAINED,
                    f ? f->error_code : 0,
                    f ? f->rip : 0,
                    f ? f->rsp : 0, 0,
                    t ? t->name : "tui",
                    t ? t->tid : -1, -1,
                    f ? (u32)(f->cs & 3) : 0);
    cr_puts("\r\n*** CONTAINED: ");
    cr_puts(what ? what : "fault");
    cr_puts(" - isolated, kernel continues ***\r\n");
}
