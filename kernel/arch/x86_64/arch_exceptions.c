/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-02
 * File: kernel/exceptions.c
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

/* P4 fix: crash log/dump buffer. Records the last CRASH_LOG_LEN
 * exceptions so the user can review them later (the on-screen console
 * may have scrolled past the original message). */
#define CRASH_LOG_LEN 8
typedef struct {
    u64 vector;
    u64 error_code;
    u64 rip;
    u64 rsp;
    u64 cr2;       /* for #PF only; 0 otherwise */
    u64 jiffies;   /* timer ticks at the time of the crash */
} crash_log_entry_t;
crash_log_entry_t g_crash_log[CRASH_LOG_LEN];
int g_crash_log_count = 0;

/* Symbol name table for vectors 0..31. */
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
     * handles (stack/heap growth) decrement the counter on resume. */
    static int exc_in_pf = 0;
    int is_pf = (f->int_no == 14);
    if (is_pf && exc_in_pf) {
        ser_puts("\r\n*** NESTED #PF INSIDE EXCEPTION HANDLER - HALTING ***\r\n");
        for (;;) {
            __asm__ volatile("cli; hlt");
        }
    }
    if (is_pf) exc_in_pf++;

    /* P4 fix: crash log/dump. Old code only printed the exception to the
     * serial console and framebuffer, then halted. If you missed the
     * message (e.g. console scrolled), the diagnostic was lost.
     *
     * Now we keep a circular buffer of the last CRASH_LOG_LEN exceptions,
     * recording: vector, error_code, rip, rsp, cr2 (for #PF), and the
     * jiffies timestamp. A new `crashlog` shell command prints the buffer.
     *
     * The buffer is small (8 entries × 48 bytes = 384 bytes) and lives
     * in BSS — no heap allocation, safe to use even when the heap is
     * corrupted. */
    extern u64 core_timer_ticks(void);  /* timer.h */
    if (g_crash_log_count < CRASH_LOG_LEN) {
        int i = g_crash_log_count++;
        g_crash_log[i].vector = f->int_no;
        g_crash_log[i].error_code = f->error_code;
        g_crash_log[i].rip = f->rip;
        g_crash_log[i].rsp = f->rsp;
        g_crash_log[i].jiffies = core_timer_ticks();
        g_crash_log[i].cr2 = 0;
        if (f->int_no == 14 /* #PF */) {
            __asm__ volatile("mov %%cr2, %0" : "=r"(g_crash_log[i].cr2));
        }
    } else {
        /* Circular: shift everything down, append at end. */
        for (int i = 0; i < CRASH_LOG_LEN - 1; i++) {
            g_crash_log[i] = g_crash_log[i + 1];
        }
        int i = CRASH_LOG_LEN - 1;
        g_crash_log[i].vector = f->int_no;
        g_crash_log[i].error_code = f->error_code;
        g_crash_log[i].rip = f->rip;
        g_crash_log[i].rsp = f->rsp;
        g_crash_log[i].jiffies = core_timer_ticks();
        g_crash_log[i].cr2 = 0;
        if (f->int_no == 14 /* #PF */) {
            __asm__ volatile("mov %%cr2, %0" : "=r"(g_crash_log[i].cr2));
        }
    }

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
     * halt handler. */
    if (f->int_no == OC_EXC_PF) {
        u64 cr2;
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        if (mem_vmm_handle_page_fault(cr2, f->error_code, f->rip, f->rsp)) {
            /* P4: pop this entry from the crash log — it was handled
             * by the L1/L0 fault handler, so it's not a crash. */
            if (g_crash_log_count > 0) g_crash_log_count--;
            if (is_pf) exc_in_pf--;   /* P0fix2 BUG-0041: resumed */
            return;  /* fault handled, resume */
        }
    }

    /* Default: print diagnostic. */
    u64 v = f->int_no;
    const char *name = (v < 32) ? arch_exc_names[v] : "Unknown";

    ser_puts("\r\n\n*** EXCEPTION ");
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
     * (kernel) exceptions cause a kernel halt. */
    if ((f->cs & 3) == 3) {
        /* User-mode exception: kill the current user process. */
        user_proc_t *proc = user_process_current();
        if (proc) {
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
            /* P3-11 FIX: Use the unified reaper so the exception-kill path
             * closes all open pipe fds too (old code only destroyed the
             * AS, leaving pipe readers blocked forever waiting for a
             * writer that had just been killed by an exception). */
            extern void user_process_reap_resources(user_proc_t *p, int exit_code);
            user_process_reap_resources(proc, 128 + (int)v);
            task_t *t = core_kthread_current();
            if (t) t->state = TASK_EXITED;
            if (is_pf) exc_in_pf--;   /* P0fix2 BUG-0041: this stack leaves */
            core_sched_yield();

            /* Should not return here (core_sched_yield switches away).
             * If it does (no other task to switch to), halt. */
        }
    }

    /* Ring-0 (kernel) exception: this is a kernel panic. Halt. */
    ser_puts("*** HALTING (kernel exception) ***\r\n");

    /* Also try to print on the framebuffer in red. */
    {
        u32 saved_fg = screen_console_get()->fg_pixel;
        screen_console_get()->fg_pixel = screen_fb_rgb(0xFF, 0x40, 0x40);
        screen_console_putc('\n');
        char line[80];
        strcpy(line, "*** EXCEPTION ");
        char num[20];
        u64_to_str(v, num); strcpy(line + strlen(line), num);
        strcpy(line + strlen(line), ": ");
        strcpy(line + strlen(line), name);
        strcpy(line + strlen(line), " ***");
        screen_console_puts(line);
        screen_console_putc('\n');
        strcpy(line, "rip=0x");
        u64_to_hex(f->rip, num, 16); strcpy(line + strlen(line), num);
        strcpy(line + strlen(line), " err=0x");
        u64_to_hex(f->error_code, num, 4); strcpy(line + strlen(line), num);
        screen_console_puts(line);
        screen_console_putc('\n');
        screen_console_puts("Halting.");
        screen_console_get()->fg_pixel = saved_fg;
    }

    /* Disable interrupts and halt forever. */
    __asm__ volatile("cli");
    for (;;) __asm__ volatile("hlt");
}
