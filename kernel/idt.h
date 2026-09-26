/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Cube Studio <cubestudio@qq.com> */
/* Open Cube OS - WP-02
 * File: kernel/idt.h
 * Purpose: IDT (Interrupt Descriptor Table) + GDT/TSS setup for x86_64.
 *
 * WP-02 introduces:
 *   - 256-entry IDT with interrupt gates
 *   - New GDT with TSS entry (for IST stack switching on critical exceptions)
 *   - TSS with IST1 = panic stack (for #DF/#MC), IST2 = debug stack
 *   - 8259 PIC remap: IRQ0-15 ? vectors 32-47
 *   - Exception handlers for CPU faults (DE/UD/#PF/#GP/#DF/NMI/etc.)
 *   - IRQ dispatch with per-IRQ handler chains (L1 extension point)
 *
 * The IDT is loaded by oc_idt_init() which is called from kmain after
 * the framebuffer console is up.
 */
#ifndef OC_IDT_H
#define OC_IDT_H

#include "types.h"

/* CPU exception vector numbers (Intel SDM Vol 3, Table 6-1). */
#define OC_EXC_DE   0   /* Divide Error (#DE) */
#define OC_EXC_DB   1   /* Debug (#DB) */
#define OC_EXC_NMI  2   /* NMI */
#define OC_EXC_BP   3   /* Breakpoint (#BP) */
#define OC_EXC_OF   4   /* Overflow (#OF) */
#define OC_EXC_BR   5   /* Bound Range Exceeded (#BR) */
#define OC_EXC_UD   6   /* Invalid Opcode (#UD) */
#define OC_EXC_NM   7   /* Device Not Available (#NM) */
#define OC_EXC_DF   8   /* Double Fault (#DF) */
#define OC_EXC_TS  10   /* Invalid TSS (#TS) */
#define OC_EXC_NP  11   /* Segment Not Present (#NP) */
#define OC_EXC_SS  12   /* Stack-Segment Fault (#SS) */
#define OC_EXC_GP  13   /* General Protection (#GP) */
#define OC_EXC_PF  14   /* Page Fault (#PF) */
#define OC_EXC_MF  16   /* x87 FPU Error (#MF) */
#define OC_EXC_AC  17   /* Alignment Check (#AC) */
#define OC_EXC_MC  18   /* Machine Check (#MC) */
#define OC_EXC_XM  19   /* SIMD FP Exception (#XM/#XF) */
#define OC_EXC_SX  30   /* Security Exception (#SX) */

/* PIC remap: IRQ0..IRQ15 ? vectors 0x20..0x2F. */
#define OC_IRQ0_VECTOR  32
#define OC_IRQ1_VECTOR  33
#define OC_IRQ2_VECTOR  34
#define OC_IRQ3_VECTOR  35
#define OC_IRQ4_VECTOR  36
#define OC_IRQ5_VECTOR  37
#define OC_IRQ6_VECTOR  38
#define OC_IRQ7_VECTOR  39
#define OC_IRQ8_VECTOR  40
#define OC_IRQ9_VECTOR  41
#define OC_IRQ10_VECTOR 42
#define OC_IRQ11_VECTOR 43
#define OC_IRQ12_VECTOR 44   /* PS/2 mouse */
#define OC_IRQ13_VECTOR 45
#define OC_IRQ14_VECTOR 46   /* primary ATA IRQ */
#define OC_IRQ15_VECTOR 47

#define OC_IDT_ENTRIES 256

/* 16-byte IDT gate descriptor (Intel SDM Vol 3, Figure 6-7). */
typedef struct oc_idt_gate {
    u16 offset_low;
    u16 selector;       /* code segment selector */
    u8  ist;            /* IST index (low 3 bits) */
    u8  flags;          /* P|DPL|0|type */
    u16 offset_mid;
    u32 offset_high;
    u32 reserved0;
} __attribute__((packed)) oc_idt_gate_t;

/* 10-byte IDT descriptor (base + limit, for `lidt`). */
typedef struct oc_idt_ptr {
    u16 limit;
    u64 base;
} __attribute__((packed)) oc_idt_ptr_t;

/* CPU-pushed part of an interrupt frame (without error code). */
typedef struct oc_irq_frame {
    u64 r15, r14, r13, r12, r11, r10, r9, r8;
    u64 rdi, rsi, rbp, rdx, rcx, rbx, rax;
    u64 int_no, error_code;
    u64 rip, cs, rflags, rsp, ss;
} oc_irq_frame_t;

/* Public API */
void oc_idt_init(void);          /* Load GDT + TSS + IDT, remap PIC, enable IRQs */
void oc_idt_load(void);          /* lidt + lgdt + ltr (assembly helper) */
void oc_idt_set_gate(int vector, void (*handler)(void), u8 flags, u8 ist);
void oc_pic_remap(void);
void oc_pic_mask(u8 irq);
void oc_pic_unmask(u8 irq);
void oc_pic_eoi(u8 irq);

/* WP-04: Set TSS RSP0 (kernel stack for ring 3 -> ring 0 transitions). */
void oc_set_tss_rsp0(u64 rsp0);

/* Per-IRQ and per-exception counters (for the on-screen stats line). */
extern u64 oc_irq_counts[OC_IDT_ENTRIES];
extern u64 oc_exc_counts[OC_IDT_ENTRIES];

#endif /* OC_IDT_H */
