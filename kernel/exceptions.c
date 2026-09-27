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
#include "exceptions.h"
#include "idt.h"
#include "string.h"
#include "fb.h"
#include "console.h"
#include "vmm.h"
#include "usermode.h"
#include "sched.h"

/* Symbol name table for vectors 0..31. */
static const char *exc_names[32] = {
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
static oc_exc_handler_fn g_exc_handlers[32][OC_EXC_CHAIN_LEN];

void oc_exc_init(void) {
    oc_memset(g_exc_handlers, 0, sizeof(g_exc_handlers));
}

int oc_exc_register_handler(int vector, oc_exc_handler_fn handler) {
    if (vector < 0 || vector >= 32 || !handler) return -1;
    for (int i = 0; i < OC_EXC_CHAIN_LEN; i++) {
        if (g_exc_handlers[vector][i] == NULL) {
            g_exc_handlers[vector][i] = handler;
            return 0;
        }
    }
    return -2;  /* chain full */
}

int oc_exc_unregister_handler(int vector, oc_exc_handler_fn handler) {
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

void oc_exc_dispatch(oc_irq_frame_t *f) {
    /* Try L1 handlers first (most-recently-registered runs first). */
    for (int i = OC_EXC_CHAIN_LEN - 1; i >= 0; i--) {
        if (g_exc_handlers[f->int_no][i]) {
            if (g_exc_handlers[f->int_no][i](f)) {
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
        if (vmm_handle_page_fault(cr2, f->error_code, f->rip, f->rsp)) {
            return;  /* fault handled, resume */
        }
    }

    /* Default: print diagnostic. */
    u64 v = f->int_no;
    const char *name = (v < 32) ? exc_names[v] : "Unknown";

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
            oc_u64_to_str((u64)proc->pid, num);
            ser_puts(num);
            ser_puts(") due to exception ");
            oc_u64_to_str(v, num);
            ser_puts(num);
            ser_puts(", kernel continues ***\r\n");

            /* Print on framebuffer too. */
            u32 saved_fg = oc_console_get()->fg_pixel;
            oc_console_get()->fg_pixel = oc_fb_rgb(0xFF, 0x40, 0x40);
            oc_console_puts("[user] process killed by exception (vector=");
            oc_u64_to_str(v, num);
            oc_console_puts(num);
            oc_console_puts("), kernel continues\n");
            oc_console_get()->fg_pixel = saved_fg;

            /* Kill the process: mark dead, set thread state to EXITED
             * (same pattern as syscall_exit). The scheduler's sched_yield
             * will pick the next ready task and context-switch to it,
             * loading the next task's CR3. We must NOT call kthread_destroy
             * here because we're running on this thread's stack. */
            proc->alive = 0;
            /* P1-8 FIX: Destroy user address space before yielding.
             * Same cleanup as sys_exit2: switch to kernel CR3, destroy AS.
             * Without this, the process's PML4/PDPT/PD0/PT pages leak
             * (~2.4 pages per killed process). After 31 kills, combined
             * with P0-4 (task slot leak), the system would be paralyzed. */
            if (proc->as) {
                extern vmm_as_t vmm_kernel_as(void);
                extern void vmm_destroy_address_space(vmm_as_t);
                __asm__ volatile("mov %0, %%cr3" : : "r"(vmm_kernel_as()) : "memory");
                vmm_destroy_address_space(proc->as);
                proc->as = 0;
            }
            task_t *t = kthread_current();
            if (t) t->state = TASK_EXITED;
            sched_yield();

            /* Should not return here (sched_yield switches away).
             * If it does (no other task to switch to), halt. */
        }
    }

    /* Ring-0 (kernel) exception: this is a kernel panic. Halt. */
    ser_puts("*** HALTING (kernel exception) ***\r\n");

    /* Also try to print on the framebuffer in red. */
    {
        u32 saved_fg = oc_console_get()->fg_pixel;
        oc_console_get()->fg_pixel = oc_fb_rgb(0xFF, 0x40, 0x40);
        oc_console_putc('\n');
        char line[80];
        oc_strcpy(line, "*** EXCEPTION ");
        char num[20];
        oc_u64_to_str(v, num); oc_strcpy(line + oc_strlen(line), num);
        oc_strcpy(line + oc_strlen(line), ": ");
        oc_strcpy(line + oc_strlen(line), name);
        oc_strcpy(line + oc_strlen(line), " ***");
        oc_console_puts(line);
        oc_console_putc('\n');
        oc_strcpy(line, "rip=0x");
        oc_u64_to_hex(f->rip, num, 16); oc_strcpy(line + oc_strlen(line), num);
        oc_strcpy(line + oc_strlen(line), " err=0x");
        oc_u64_to_hex(f->error_code, num, 4); oc_strcpy(line + oc_strlen(line), num);
        oc_console_puts(line);
        oc_console_putc('\n');
        oc_console_puts("Halting.");
        oc_console_get()->fg_pixel = saved_fg;
    }

    /* Disable interrupts and halt forever. */
    __asm__ volatile("cli");
    for (;;) __asm__ volatile("hlt");
}
