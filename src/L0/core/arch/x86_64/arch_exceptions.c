/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-02
 * File: kernel/arch/x86_64/arch_exceptions.c
 * Purpose: Default CPU exception handlers + L1 exception-handler chain.
 *
 * Default behavior: print a diagnostic to the serial console + framebuffer
 * (in red), dump the frame, and halt. L1 can override per-vector by
 * registering a handler.
 */
#include "arch_exceptions.h"
#include "arch_idt.h"
#include "lib_string.h"
#include "screen_fb.h"
#include "screen_console.h"
#include "mem_vmm.h"
#include "core_usermode.h"
#include "core_sched.h"
/* 3.0.0-WP03 G5: no core_crash.h - the crash capture lives in
 * services/crash and a core file must not depend on services/.  The
 * panic entry points below route through a handler SLOT the crash
 * service installs at boot (crash_install_panic_handlers); before
 * that (early boot) or without it, the fallback prints and halts. */

/* Symbol name table for vectors 0..31 (moved to core_crash.c crash_src_name
 * for the panic banner; keep a local alias used by the ring-3 kill path) */
static const char *arch_exc_names[32] = {
    "#DE Divide Error",
    "#DB Debug",
    "NMI Non-Maskable Interrupt",
    "#BP Breakpoint",
    "#OF Overflow",
    "#BR Bound Range Exceeded",
    "#UD Invalid Opcode",
    "#NM Device Not Available",
    "#DF Double Fault",
    "?? Coprocessor Segment Overrun",
    "#TS Invalid TSS",
    "#NP Segment Not Present",
    "#SS Stack-Segment Fault",
    "#GP General Protection",
    "#PF Page Fault",
    "?? Reserved",
    "#MF x87 FPU Error",
    "#AC Alignment Check",
    "#MC Machine Check",
    "#XM SIMD FP Exception",
    "?? Virtualization",
    "?? Reserved",
    "?? Reserved",
    "?? Reserved",
    "?? Reserved",
    "?? Reserved",
    "?? Reserved",
    "?? Reserved",
    "?? Reserved",
    "?? Reserved",
    "#SX Security Exception",
    "?? Reserved",
};

/* L1 handler chain. We support up to 4 handlers per vector. */
#define OC_EXC_CHAIN_LEN 4
static arch_exc_handler_fn g_exc_handlers[32][OC_EXC_CHAIN_LEN];

void arch_exc_init(void) {
    memset(g_exc_handlers, 0, sizeof(g_exc_handlers));
}

int arch_exc_register_handler(int vector, arch_exc_handler_fn handler) {
    if (vector < 0 || vector >= 32 || !handler) return -1;
    for (int i = 0; i < OC_EXC_CHAIN_LEN; i++) {
        if (g_exc_handlers[vector][i] == NULL) {
            g_exc_handlers[vector][i] = handler;
            return 0;
        }
    }
    return -2;  /* chain full */
}

int arch_exc_unregister_handler(int vector, arch_exc_handler_fn handler) {
    if (vector < 0 || vector >= 32 || !handler) return -1;
    for (int i = 0; i < OC_EXC_CHAIN_LEN; i++) {
        if (g_exc_handlers[vector][i] == handler) {
            g_exc_handlers[vector][i] = NULL;
            /* compact */
            for (int j = i; j + 1 < OC_EXC_CHAIN_LEN; j++) {
                g_exc_handlers[vector][j] = g_exc_handlers[vector][j+1];
                g_exc_handlers[vector][j+1] = NULL;
            }
            return 0;
        }
    }
    return -2;
}

/* Serial output for exception dumps (so we can see them even if the
 * framebuffer is broken). COM1 at 0x3F8. */
static inline void outb(u16 p, u8 v) { __asm__ volatile("outb %0, %1" :: "a"(v), "Nd"(p)); }
static inline u8   inb(u16 p)        { u8 v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(p)); return v; }
#define COM1 0x3F8
static void ser_putc(char c) {
    while ((inb(COM1 + 5) & 0x20) == 0) ;
    outb(COM1, (u8)c);
}
static void ser_puts(const char *s) { while (*s) ser_putc(*s++); }
static void ser_hex(u64 v) {
    char buf[17];
    const char *hex = "0123456789ABCDEF";
    for (int i = 15; i >= 0; i--) { buf[i] = hex[v & 0xF]; v >>= 4; }
    buf[16] = 0;
    ser_puts(buf);
}
static void ser_dec(u64 v) {
    char buf[21];
    int i = 20;
    if (v == 0) { ser_putc('0'); return; }
    buf[i--] = 0;
    while (v) { buf[i--] = '0' + (v % 10); v /= 10; }
    ser_puts(&buf[i+1]);
}

/* BUG-0139 FIX (A1-6): reap-and-yield continuation for kills taken on
 * IST2-backed exceptions (#DB=1, NMI=2, #BP=3). Runs on the dedicated exit
 * stack (arch_exc_exit.S), NEVER on IST2, so the scheduler records an
 * address on this private stack and the global IST2 region is abandoned
 * instead of carrying a suspended frame that the next IST2 exception would
 * overwrite. Never returns. */
void arch_exc_kill_and_yield(user_proc_t *proc, u64 vec) {
    extern void arch_ist2_range(u64 *base, u64 *size);
    u64 ist2_base = 0, ist2_size = 0, rsp = 0;
    arch_ist2_range(&ist2_base, &ist2_size);
    __asm__ volatile("mov %%rsp, %0" : "=r"(rsp));

    ser_puts("*** IST-exit: vector=");
    ser_dec(vec);
    ser_puts(" exit-stack rsp=0x");
    ser_hex(rsp);
    if (rsp >= ist2_base && rsp < ist2_base + ist2_size) {
        ser_puts(" [FAIL: still on IST2]\r\n");
    } else {
        ser_puts(" [off-IST2 OK]\r\n");
    }

    user_process_reap_resources(proc, 128 + (int)vec);
    /* WP-11 FIX: wake the parent (same as the main kill path above;
     * see the comment there). */
    if (proc->parent_tid > 0) core_kthread_wake(proc->parent_tid);
    task_t *t = core_kthread_current();
    if (t) t->state = TASK_EXITED;
    core_sched_yield();
    /* The task is EXITED; the scheduler never resumes this stack. */
    for (;;) __asm__ volatile("hlt");
}

/* WP-11: public shim onto the PROVEN serial printer (this file's
 * ser_puts printed every exception banner since WP-02, including the
 * #DF banner itself). The #DF panic path in core_crash.c must print
 * through THIS printer: the -Os cr_puts variant proved unreliable in
 * the hostile #DF context (locked up mid-word during bring-up). */
void arch_exc_ser_puts(const char *s) { ser_puts(s); }

/* ---- 3.0.0-WP03 G5: the panic handler slot --------------------------
 * kernel_panic / kernel_panic_frame are the CORE-side entry points
 * (the scheduler's stack-bounds guard and the exception dispatcher
 * call them).  The implementation (register capture, dump, crashlog,
 * halt-or-reboot policy) lives in services/crash and registers here
 * at boot.  The fallback (no handler yet: the first ~2 s of boot, or
 * a crash-service-less system) prints the reason and halts - the
 * honest pre-crash-infra behaviour. */
static void (*g_panic_fn)(const char *reason) = 0;
static void (*g_panic_frame_fn)(const char *reason, const arch_irq_frame_t *f) = 0;

/* the crashlog recorder slot (same inversion): the ring-3 fault
 * path records the fault before the signal delivery - the recorder
 * itself (the on-disk crashlog) lives in services/crash */
typedef void (*crashlog_record_fn_t)(u32 vector, u64 error_code, u64 rip,
                                     u64 rsp, u64 cr2, const char *pname,
                                     i32 tid, i32 pid, u32 ring);
static crashlog_record_fn_t g_crashlog_record_fn = 0;

void arch_set_panic_handlers(void (*panic)(const char *),
                             void (*panic_frame)(const char *,
                                                 const arch_irq_frame_t *)) {
    g_panic_fn = panic;
    g_panic_frame_fn = panic_frame;
}

void arch_set_crashlog_record(void (*record)(u32 vector, u64 error_code,
                                             u64 rip, u64 rsp, u64 cr2,
                                             const char *pname, i32 tid,
                                             i32 pid, u32 ring)) {
    g_crashlog_record_fn = record;
}

static void panic_fallback(const char *reason) {
    screen_console_puts("\nKERNEL PANIC (no crash handler installed): ");
    screen_console_puts(reason ? reason : "?");
    screen_console_puts("\nSystem halted.\n");
    for (;;) __asm__ volatile("cli; hlt");
}

void kernel_panic(const char *reason) {
    if (g_panic_fn) g_panic_fn(reason);
    panic_fallback(reason);
}

void kernel_panic_frame(const char *reason, const arch_irq_frame_t *f) {
    if (g_panic_frame_fn) g_panic_frame_fn(reason, f);
    (void)f;
    panic_fallback(reason);
}

void arch_exc_dispatch(arch_irq_frame_t *f) {
    /* P0fix2 BUG-0041 (RUN-01): guard against recursive fault handling.
     * The fault-printing path itself can #PF (the KNOWN_ISSUES 6.1
     * framebuffer rolling-edge page), and the old handler re-entered
     * itself with no bound: each entry pushed ~0x230 bytes until the
     * stack ran out and the CPU raised a triple fault (tcptest 100%
     * crash chain in the audit).  An in-handler counter stops the
     * recursion: on a NESTED #PF (a fault while already handling one)
     * we report once via SERIAL ONLY (no framebuffer, no crash-log
     * write) and halt immediately.  Faults that the VMM legitimately
     * handles (stack/heap growth) decrement the counter on resume.
     * (WP-11: the nested-panic guard inside kernel_panic_frame is the
     * second layer of the same defence.) */
    static int exc_in_pf = 0;
    int is_pf = (f->int_no == 14);
    if (is_pf && exc_in_pf) {
        ser_puts("\r\n*** NESTED #PF INSIDE EXCEPTION HANDLER - HALTING ***\r\n");
        for (;;) {
            __asm__ volatile("cli; hlt");
        }
    }
    if (is_pf) exc_in_pf++;

    /* Try L1 handlers first (most-recently-registered runs first). */
    for (int i = OC_EXC_CHAIN_LEN - 1; i >= 0; i--) {
        if (g_exc_handlers[f->int_no][i]) {
            if (g_exc_handlers[f->int_no][i](f)) {
                if (is_pf) exc_in_pf--;   /* P0fix2 BUG-0041: resumed */
                return;  /* L1 handled it, resume */
            }
        }
    }

    /* Special handling for #PF (vector 14): call VMM page fault handler.
     * It may handle the fault (stack growth, heap growth, etc.) and we
     * resume. If it returns 0 (illegal), fall through to the default
     * panic path. (WP-11: crashlog entries are now written only on the
     * unhandled paths, so there is nothing to pop here.) */
    if (f->int_no == OC_EXC_PF) {
        u64 cr2;
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        if (mem_vmm_handle_page_fault(cr2, f->error_code, f->rip, f->rsp)) {
            if (is_pf) exc_in_pf--;   /* P0fix2 BUG-0041: resumed */
            return;  /* fault handled, resume */
        }
    }

    /* Default: print diagnostic. */
    u64 v = f->int_no;
    const char *name = (v < 32) ? arch_exc_names[v] : "Unknown";

    ser_puts("\r\n*** EXCEPTION ");
    ser_dec(v);
    ser_puts(": ");
    ser_puts(name);
    ser_puts(" ***\r\n");
    ser_puts("  error_code=0x"); ser_hex(f->error_code);
    ser_puts("  rip=0x");        ser_hex(f->rip);
    ser_puts("  cs=0x");         ser_hex(f->cs);
    ser_puts("  rflags=0x");     ser_hex(f->rflags);
    ser_puts("\r\n  rsp=0x");    ser_hex(f->rsp);
    ser_puts("  ss=0x");         ser_hex(f->ss);
    ser_puts("\r\n");

    /* For #PF, also print CR2. */
    if (v == 14) {
        u64 cr2;
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        ser_puts("  cr2=0x");
        ser_hex(cr2);
        ser_puts("  (faulting address)\r\n");
    }

    /* BUG-001 FIX: If the exception came from ring-3 (user mode), kill the
     * offending process and continue running the kernel. Only ring-0
     * (kernel) exceptions cause a kernel panic. */
    if ((f->cs & 3) == 3) {
        /* User-mode exception: kill the current user process. */
        user_proc_t *proc = user_process_current();
        if (proc) {
            /* WP-11 4.5: SIGSEGV delivery. A user program touching
             * illegal memory (or executing garbage) gets a REAL
             * signal first: if it registered a SIGSEGV handler, the
             * fault is delivered exactly like the syscall path
             * delivers pending signals (saved frame + handler entry +
             * restorer trampoline) and the process RESUMES in its
             * handler. No handler (or a handler already running, or
             * the 3-delivery ping-pong guard tripped) -> the kill
             * path below, exactly as before. */
            {
                int can_deliver =
                    (proc->sig_handlers[SIGSEGV] != NULL) &&
                    (proc->sig_in_progress == 0) &&
                    (proc->sigsegv_delivered < 3);
                if (can_deliver) {
                    proc->sigsegv_delivered++;
                    /* record the ring-3 fault in the crash log FIRST
                     * (same record-once rule as the kill path) */
                    {
                        task_t *kt = core_kthread_current();
                        u64 cr2 = 0;
                        if (v == 14)
                            __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
                        if (g_crashlog_record_fn)
                            g_crashlog_record_fn((u32)v, f->error_code,
                                                 f->rip, f->rsp, cr2,
                                                 proc->name,
                                                 kt ? kt->tid : -1,
                                                 proc->pid, 3);
                    }
                    if (user_signal_deliver(proc, f, SIGSEGV) == 0) {
                        ser_puts("*** SIGSEGV delivered to pid=");
                        char num[20];
                        u64_to_str((u64)proc->pid, num);
                        ser_puts(num);
                        ser_puts(" (handler will run, delivery #");
                        u64_to_str((u64)proc->sigsegv_delivered, num);
                        ser_puts(num);
                        ser_puts(") - kernel continues ***\r\n");
                        if (is_pf) exc_in_pf--;
                        return;   /* iretq straight into the handler */
                    }
                    /* delivery failed (impossible in practice: the
                     * raw rip/rdi fallback has no preconditions) -
                     * fall through to the kill path */
                }
            }
            /* WP-11: record the USER crash in the persistent crash log
             * (ring=3) BEFORE any risky printing, same rule as the
             * panic path. */
            {
                task_t *kt = core_kthread_current();
                u64 cr2 = 0;
                if (v == 14)
                    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
                if (g_crashlog_record_fn)
                    g_crashlog_record_fn((u32)v, f->error_code, f->rip,
                                         f->rsp, cr2, proc->name,
                                         kt ? kt->tid : -1, proc->pid, 3);
            }
            ser_puts("*** KILLING user process (pid=");
            char num[20];
            u64_to_str((u64)proc->pid, num);
            ser_puts(num);
            ser_puts(") due to exception ");
            u64_to_str(v, num);
            ser_puts(num);
            ser_puts(", kernel continues ***\r\n");

            /* Print on framebuffer too. */
            u32 saved_fg = screen_console_get()->fg_pixel;
            screen_console_get()->fg_pixel = screen_fb_rgb(0xFF, 0x40, 0x40);
            screen_console_puts("[user] process killed by exception (vector=");
            u64_to_str(v, num);
            screen_console_puts(num);
            screen_console_puts("), kernel continues\n");
            screen_console_get()->fg_pixel = saved_fg;

            /* Kill the process: mark dead, set thread state to EXITED
             * (same pattern as core_syscall_exit). The scheduler's core_sched_yield
             * will pick the next ready task and context-switch to it,
             * loading the next task's CR3. We must NOT call core_kthread_destroy
             * here because we're running on this thread's stack. */
            /* BUG-0139 FIX (A1-6): vectors 1/2/3 (#DB/NMI/#BP) are backed
             * by IST2 (arch_idt.c) - a single global debug stack. Killing
             * the offender used to context-switch away from the middle of
             * IST2 (the scheduler stored that rsp in the task block), so
             * the NEXT IST2 exception reloaded the TSS top and overwrote
             * the suspended frame of this dying task. Switch to a dedicated
             * exit stack first (arch_exc_exit.S) and do the reap + yield
             * there; the scheduler saves an address on that private stack
             * and the IST2 region is abandoned. Never returns. */
            if (v == 1 || v == 2 || v == 3) {
                extern void arch_exc_run_on_exit_stack(user_proc_t *proc, u64 vec)
                    __attribute__((noreturn));
                arch_exc_run_on_exit_stack(proc, v);
            }

            /* P3-11 FIX: Use the unified reaper so the exception-kill path
             * closes all open pipe fds too (old code only destroyed the
             * AS, leaving pipe readers blocked forever waiting for a
             * writer that had just been killed by an exception). */
            extern void user_process_reap_resources(user_proc_t *p, int exit_code);
            user_process_reap_resources(proc, 128 + (int)v);
            /* WP-11 FIX (found by run stackovf): the exception-kill path
             * never woke the parent task, so a parent blocked in SYS_WAIT
             * after a forked child CRASHED stayed asleep forever (the
             * sys_exit2 path wakes it - sys_syscall.c:325). Same rule
             * here, after the reap. */
            if (proc->parent_tid > 0) core_kthread_wake(proc->parent_tid);
            task_t *t = core_kthread_current();
            if (t) t->state = TASK_EXITED;
            if (is_pf) exc_in_pf--;   /* P0fix2 BUG-0041: this stack leaves */
            core_sched_yield();

            /* Should not return here (core_sched_yield switches away).
             * If it does (no other task to switch to), fall through to
             * the ring-0 panic path below. */
        }
    }

    /* ---- Ring-0 (kernel) exception = kernel panic (WP-11) ----
     *
     * 1. TUI CONTAINMENT: if the faulting RIP is inside the TUI code
     *    window (linker.ld exports __tui_text_start and __tui_text_end
     *    around the TUI objects: build/tui plus the tui_cn_* Chinese
     *    renderers) AND a tui_dt_guard() is active on this task, the
     *    crash is contained: record it via crash_contained(), then
     *    tui_dt_panic() longjmps back to the guarded runner (the
     *    desktop shows "page contained" and the menu survives).
     *    Limited to non-IST vectors (a fault on the current task
     *    stack is safe to abandon; vectors 1/2/3 ride IST2 and
     *    8/18 ride IST1/IST3 - those never longjmp).
     *
     * 2. Otherwise: the unified panic path (core_crash.c) prints the
     *    banner + full register snapshot + traceback, saves the core
     *    dump, flushes the crash log and applies the policy
     *    (HALT safe loop / REBOOT 8042+0xCF9). */
/* 3.0.0-WP03: the TUI containment window (guarded ring-0 fault
     * recovery for desktop page faults) died with the TUI user plane -
     * every ring-0 fault goes straight to the unified panic path
     * below (WP-11). */

    {
        char reason[112];
        char num[12];
        /* WP-11 4.4: name stack overflows for what they are.
         * (a) #PF whose CR2 lands in a kthread's UNMAPPED guard page
         *     (core_kthread_create unmaps the lowest stack page).
         * (b) #DF with the frame RSP below the current task's stack
         *     base: the guard-page #PF pushed its error code onto the
         *     already-burned stack, the push itself faulted, the CPU
         *     escalated to a Double Fault and switched to the IST1
         *     stack - which is why the panic machinery can still run.
         *     (Same shape as mainstream kernels: guard page ->
         *     double fault.) */
        const char *ovf_task = NULL;
        const char *ovf_kind = NULL;
        if (v == 14) {
            u64 cr2;
            __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
            extern task_t *core_kthread_get_task(int tid);
            for (int ti = 1; ti < 32 && !ovf_task; ti++) {
                task_t *tt = core_kthread_get_task(ti);
                if (tt && tt->stack_base && tt->stack_size) {
                    u64 base = (u64)(uintptr_t)tt->stack_base;
                    if (cr2 >= base && cr2 < base + 4096)
                        ovf_task = tt->name;
                }
            }
            if (ovf_task) ovf_kind = "guard page hit";
        } else if (v == 8) {
            /* A burned stack: the guard-page #PF tried to push its
             * frame onto the already-burned stack, the push itself
             * faulted, the CPU escalated to #DF and switched to the
             * IST1 panic stack. What the #DF frame then records as
             * rsp depends on the implementation: real hardware often
             * keeps a garbage slot (observed 0xFFFFFFFFFFFFF040),
             * while QEMU rolls the failed push back and records the
             * LAST GOOD slot at the very bottom of the task stack
             * window (observed stack_base+0x100). Attribute the
             * overflow whenever the recorded rsp sits at/below the
             * bottom page of the current task's stack, or is
             * implausible garbage - all three are signatures of a
             * stack destroyed by recursion. A #DF whose rsp is in the
             * middle of a healthy stack (e.g. a fault inside a page
             * fault handler) stays a generic ring-0 exception. */
            task_t *ct = core_kthread_current();
            if (ct && ct->stack_base && ct->stack_size &&
                (f->rsp < (u64)(uintptr_t)ct->stack_base + 4096 ||
                 f->rsp >= 0x40000000000ULL)) {
                ovf_task = ct->name;
                ovf_kind = "stack exhausted -> #DF";
            }
        }
        if (ovf_task) {
            strcpy(reason, "KERNEL STACK OVERFLOW (");
            strcat(reason, ovf_kind);
            strcat(reason, ", task=");
            strcat(reason, ovf_task);
            strcat(reason, ")");
        } else {
            strcpy(reason, "ring-0 exception ");
            u64_to_str(v, num); strcat(reason, num);
            strcat(reason, " (");
            strcat(reason, name);
            strcat(reason, ")");
        }
        kernel_panic_frame(reason, f);
    }
}

