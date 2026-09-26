/* SPDX-License-Identifier: Apache-2.0 */
/* Open Cube OS - WP-02
 * File: kernel/idt.c
 * Purpose: GDT/TSS/IDT/PIC setup + exception handlers + IRQ dispatch.
 *
 * Layout:
 *   1. New GDT with TSS entry (for IST stack switching).
 *   2. TSS with IST1 (panic stack for #DF/#MC) and IST2 (debug stack).
 *   3. 256-entry IDT, populated from oc_isr_table[] in idt_stub.S.
 *   4. 8259 PIC remap (master at 0x20-0x27, slave at 0xA0-0xA7;
 *      IRQ0-15 ? vectors 32-47).
 *   5. Exception dispatch: 0..31 ? oc_exc_dispatch().
 *   6. IRQ dispatch: 32..47 ? oc_irq_dispatch().
 *   7. Per-vector counters for on-screen stats.
 */
#include "idt.h"
#include "pic.h"
#include "exceptions.h"
#include "irq.h"
#include "string.h"

/* ---- I/O port helpers ---- */
static inline void outb(u16 p, u8 v) { __asm__ volatile("outb %0, %1" :: "a"(v), "Nd"(p)); }
static inline u8   inb(u16 p)        { u8 v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline void outw(u16 p, u16 v){ __asm__ volatile("outw %0, %1" :: "a"(v), "Nd"(p)); }
static inline void io_wait(void)     { outb(0x80, 0); }

/* ---- Per-vector counters ---- */
u64 oc_irq_counts[OC_IDT_ENTRIES];
u64 oc_exc_counts[OC_IDT_ENTRIES];

/* ---- GDT / TSS ---- */
/* GDT layout (matches selector values used in boot.S):
 *   0x00 null
 *   0x08 code32  (legacy, unused in long mode but kept for compatibility)
 *   0x10 data    (used by kernel data segments)
 *   0x18 code64  (kernel code)
 *   0x20 TSS     (64-bit TSS, 16 bytes)
 *
 * We lay the GDT out as a single byte array so the TSS (16-byte system
 * descriptor) is guaranteed to be contiguous with the 8-byte entries.
 */
typedef struct oc_gdt_entry {
    u16 limit_low;
    u16 base_low;
    u8  base_mid;
    u8  access;
    u8  flags_limit_high;
    u8  base_high;
} __attribute__((packed)) oc_gdt_entry_t;

typedef struct oc_tss {
    u32 reserved0;
    u32 rsp0_low;
    u32 rsp0_high;
    u32 rsp1_low;
    u32 rsp1_high;
    u32 rsp2_low;
    u32 rsp2_high;
    u32 reserved1;
    u32 reserved2;
    u32 ist1_low;
    u32 ist1_high;
    u32 ist2_low;
    u32 ist2_high;
    u32 ist3_low;
    u32 ist3_high;
    u32 ist4_low;
    u32 ist4_high;
    u32 ist5_low;
    u32 ist5_high;
    u32 ist6_low;
    u32 ist6_high;
    u32 ist7_low;
    u32 ist7_high;
    u32 reserved3;
    u32 reserved4;
    u16 iopb_offset;
    u16 reserved5;
} __attribute__((packed)) oc_tss_t;

/* GDT: 6 8-byte entries + 1 16-byte system entry = 64 bytes total.
 * Entries: null, code32, data, code64, TSS(16B), user_code64, user_data. */
static u8 g_gdt[64] __attribute__((aligned(16)));
static oc_tss_t g_tss __attribute__((aligned(16)));
static u8 g_panic_stack[8192] __attribute__((aligned(16)));
static u8 g_debug_stack[8192] __attribute__((aligned(16)));

/* The IDT itself. */
static oc_idt_gate_t g_idt[OC_IDT_ENTRIES] __attribute__((aligned(4096)));
static oc_idt_ptr_t  g_idt_ptr;

/* isr table from idt_stub.S (256 function pointers). */
extern u64 oc_isr_table[];

/* GDT pointer used by `lgdt`. */
typedef struct oc_gdt_ptr {
    u16 limit;
    u64 base;
} __attribute__((packed)) oc_gdt_ptr_t;
static oc_gdt_ptr_t g_gdt_ptr;

static void gdt_set_entry(int i, u32 base, u32 limit, u8 access, u8 flags) {
    oc_gdt_entry_t *e = (oc_gdt_entry_t*)&g_gdt[i * 8];
    e->limit_low         = limit & 0xFFFF;
    e->base_low          = base & 0xFFFF;
    e->base_mid          = (base >> 16) & 0xFF;
    e->access            = access;
    e->flags_limit_high  = ((limit >> 16) & 0x0F) | (flags << 4);
    e->base_high         = (base >> 24) & 0xFF;
}

static void gdt_set_sys_entry(int i, u64 base, u32 limit, u8 access, u8 flags) {
    /* 16-byte system descriptor (TSS in long mode). */
    u8 *e = &g_gdt[i * 8];
    u16 lim_lo = limit & 0xFFFF;
    u16 base_lo = base & 0xFFFF;
    u8  base_mid = (base >> 16) & 0xFF;
    u8  base_hi  = (base >> 24) & 0xFF;
    u32 base_hi32 = (base >> 32) & 0xFFFFFFFF;
    *(u16*)(e + 0)  = lim_lo;
    *(u16*)(e + 2)  = base_lo;
    *(u8 *)(e + 4)  = base_mid;
    *(u8 *)(e + 5)  = access;
    *(u8 *)(e + 6)  = ((limit >> 16) & 0x0F) | (flags << 4);
    *(u8 *)(e + 7)  = base_hi;
    *(u32*)(e + 8)  = base_hi32;
    *(u32*)(e + 12) = 0;
}

static void tss_init(void) {
    oc_memset(&g_tss, 0, sizeof(g_tss));
    /* IST1 = panic stack (for #DF / #MC). */
    u64 p1 = (u64)(uintptr_t)(g_panic_stack + sizeof(g_panic_stack));
    g_tss.ist1_low  = p1 & 0xFFFFFFFF;
    g_tss.ist1_high = (p1 >> 32) & 0xFFFFFFFF;
    /* IST2 = debug stack. */
    u64 p2 = (u64)(uintptr_t)(g_debug_stack + sizeof(g_debug_stack));
    g_tss.ist2_low  = p2 & 0xFFFFFFFF;
    g_tss.ist2_high = (p2 >> 32) & 0xFFFFFFFF;
    /* rsp0 = kernel stack (the boot stack). Set by long_mode_init; we leave
     * it zero here, which means the CPU uses the current RSP on int->kernel
     * transitions. That's fine because we already run at ring 0. */
    g_tss.iopb_offset = sizeof(g_tss);
}

static void gdt_init(void) {
    /* Indices in g_gdt (each entry is 8 bytes; system entry at index 4
     * occupies 16 bytes):
     *   0 = null          (selector 0x00)
     *   1 = code32        (selector 0x08)
     *   2 = data          (selector 0x10)
     *   3 = code64        (selector 0x18)
     *   4 = TSS (16-byte) (selector 0x20)
     *   5 = user code64   (selector 0x28, ring 3)
     *   6 = user data     (selector 0x30, ring 3) */
    gdt_set_entry(1, 0, 0xFFFFF, 0x9A, 0xC);  /* code32: P|ring0|code|RX, 4K|32-bit */
    gdt_set_entry(2, 0, 0xFFFFF, 0x92, 0xC);  /* data:   P|ring0|data|RW, 4K|32-bit */
    gdt_set_entry(3, 0, 0xFFFFF, 0x9A, 0xA);  /* code64: P|ring0|code|RX, 4K|64-bit (L=1) */
    gdt_set_sys_entry(4, (u64)(uintptr_t)&g_tss, sizeof(g_tss) - 1, 0x89, 0x0);
    /* 0x89 = P|ring0|system|TSS64-available. TSS is 16 bytes, occupies
     * indices 4 AND 5. So user segments start at index 6. */
    gdt_set_entry(6, 0, 0xFFFFF, 0xFA, 0xA);  /* user code64: P|ring3|code|RX, 4K|64-bit (sel 0x30) */
    gdt_set_entry(7, 0, 0xFFFFF, 0xF2, 0xC);  /* user data:   P|ring3|data|RW, 4K|32-bit (sel 0x38) */

    g_gdt_ptr.limit = sizeof(g_gdt) - 1;   /* 63 */
    g_gdt_ptr.base  = (u64)(uintptr_t)g_gdt;
}

/* Set TSS RSP0 (the kernel stack used on int/syscall from ring 3). */
void oc_set_tss_rsp0(u64 rsp0) {
    g_tss.rsp0_low  = rsp0 & 0xFFFFFFFF;
    g_tss.rsp0_high = (rsp0 >> 32) & 0xFFFFFFFF;
}

void oc_idt_set_gate(int vector, void (*handler)(void), u8 flags, u8 ist) {
    u64 addr = (u64)(uintptr_t)handler;
    oc_idt_gate_t *g = &g_idt[vector];
    g->offset_low   = addr & 0xFFFF;
    g->selector     = 0x18;       /* kernel code64 */
    g->ist          = ist & 0x7;
    g->flags        = flags;
    g->offset_mid   = (addr >> 16) & 0xFFFF;
    g->offset_high  = (addr >> 32) & 0xFFFFFFFF;
    g->reserved0    = 0;
}

static void idt_install_stubs(void) {
    for (int i = 0; i < OC_IDT_ENTRIES; i++) {
        u8 ist = 0;
        u8 flags = 0x8E;  /* P|ring0|interrupt gate */
        /* #DF (8) and #MC (18) use IST1 (panic stack). */
        if (i == 8 || i == 18) ist = 1;
        /* NMI (2) and #DB (1) and #BP (3) use IST2 (debug stack). */
        if (i == 1 || i == 2 || i == 3) ist = 2;
        /* Syscall entry (vector 128 = 0x80) uses DPL=3 so ring 3 can call it. */
        if (i == 0x80) flags = 0xEE;  /* P|ring3|interrupt gate */
        oc_idt_set_gate(i, (void(*)(void))oc_isr_table[i], flags, ist);
    }
}

static void idt_ptr_init(void) {
    g_idt_ptr.limit = sizeof(g_idt) - 1;
    g_idt_ptr.base  = (u64)(uintptr_t)g_idt;
}

/* ---- 8259 PIC ---- */
void oc_pic_remap(void) {
    /* ICW1: start init, cascade, expect ICW4. */
    outb(0x20, 0x11); io_wait();
    outb(0xA0, 0x11); io_wait();
    /* ICW2: vector offsets. Master 0x20-0x27, slave 0x28-0x2F. */
    outb(0x21, OC_IRQ0_VECTOR); io_wait();
    outb(0xA1, OC_IRQ0_VECTOR + 8); io_wait();
    /* ICW3: master tells slave is on IRQ2 (bit 2); slave tells its cascade
     * identity is 2. */
    outb(0x21, 0x04); io_wait();
    outb(0xA1, 0x02); io_wait();
    /* ICW4: 8086 mode, auto-EOI off, normal EOI. */
    outb(0x21, 0x01); io_wait();
    outb(0xA1, 0x01); io_wait();
    /* Mask everything initially; drivers unmask what they need. */
    outb(0x21, 0xFB);  /* mask all except IRQ2 (cascade) */
    outb(0xA1, 0xFF);
}

/* P1-3 FIX: all three PIC helpers now bounds-check irq < 16. Previously
 * irq >= 16 caused a shift past the 8-bit outb operand width (UB). */
void oc_pic_mask(u8 irq) {
    if (irq >= 16) return;
    u16 port = irq < 8 ? 0x21 : 0xA1;
    u8  bit  = irq < 8 ? irq : irq - 8;
    outb(port, inb(port) | (1 << bit));
}

void oc_pic_unmask(u8 irq) {
    if (irq >= 16) return;
    u16 port = irq < 8 ? 0x21 : 0xA1;
    u8  bit  = irq < 8 ? irq : irq - 8;
    outb(port, inb(port) & ~(1 << bit));
}

void oc_pic_eoi(u8 irq) {
    if (irq >= 16) return;
    if (irq >= 8) outb(0xA0, 0x20);
    outb(0x20, 0x20);
}

/* ---- Load GDT/IDT/TR (assembly helper) ---- */
/* Defined in idt_load.S */
void oc_load_gdt_idt_tr(const oc_gdt_ptr_t *gdt_ptr,
                        const oc_idt_ptr_t *idt_ptr,
                        u16 tr_selector);

void oc_idt_load(void) {
    /* Enable NXE (no-execute) bit in EFER so VMM_FLAG_NOEXEC works. */
    __asm__ volatile(
        "mov $0xC0000080, %%ecx\n"
        "rdmsr\n"
        "or $0x800, %%eax\n"
        "wrmsr\n"
        ::: "eax", "ecx", "edx", "memory"
    );
    oc_load_gdt_idt_tr(&g_gdt_ptr, &g_idt_ptr, 0x20);
}

/* ---- Top-level init ---- */
void oc_idt_init(void) {
    oc_memset(g_idt, 0, sizeof(g_idt));
    oc_memset(oc_irq_counts, 0, sizeof(oc_irq_counts));
    oc_memset(oc_exc_counts, 0, sizeof(oc_exc_counts));

    tss_init();
    gdt_init();
    idt_install_stubs();
    idt_ptr_init();
    oc_pic_remap();
    oc_idt_load();

    /* Install exception + IRQ dispatch tables (in exceptions.c / irq.c). */
    oc_exc_init();
    oc_irq_init();
}

/* ---- Dispatch (called from idt_stub.S) ----
 * oc_isr_dispatch receives rdi = pointer to an oc_irq_frame_t on the stack.
 * It dispatches to either oc_exc_dispatch (vector 0..31) or
 * oc_irq_dispatch (vector 32..47), then returns. The stub does the iretq.
 */
void oc_isr_dispatch(oc_irq_frame_t *f) {
    u64 v = f->int_no;
    if (v < 32) {
        oc_exc_counts[v]++;
        oc_exc_dispatch(f);
    } else if (v < 48) {
        oc_irq_counts[v]++;
        oc_irq_dispatch(f);
    } else if (v == 0x80) {
        /* Syscall (int 0x80 from ring 3). */
        oc_irq_counts[v]++;
        extern void syscall_dispatch(u64 *regs);
        /* Pass the register array from the frame. The frame starts with
         * r15 at offset 0, so f itself is the register array. */
        syscall_dispatch((u64*)f);
    }
}
