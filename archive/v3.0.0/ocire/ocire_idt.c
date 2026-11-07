/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP-01-ocire-core
 * File: ocire/ocire_idt.c
 * Purpose: OCIRE minimal IDT (32 exception vectors, interrupt gates).
 */

#include "ocire_idt.h"
#include "ocire_io.h"
#include "ocire_util.h"
#include "ocire_console.h"

/* The stub address table lives in the boot asm (ocire_boot.S). */
extern const u64 ocire_stub_ptrs[32];

/* 16-byte interrupt gate descriptor. */
#pragma pack(push, 1)
typedef struct {
    u16 offset_lo;
    u16 selector;      /* 0x18 = 64-bit code segment in our GDT */
    u8  ist;
    u8  flags;         /* present | type 14 (interrupt gate) */
    u16 offset_mid;
    u32 offset_hi;
    u32 reserved;
} idt_entry_t;

typedef struct {
    u16 limit;
    u64 base;
} idt_ptr_t;
#pragma pack(pop)

static idt_entry_t g_idt[32] __attribute__((aligned(16)));
static idt_ptr_t   g_idt_ptr;

static const char *const exc_name[32] = {
    "#DE divide error", "#DB debug", "NMI", "#BP breakpoint",
    "#OF overflow", "#BR bound range", "#UD invalid opcode",
    "#NM device not available", "#DF double fault", "coproc seg",
    "#TS invalid TSS", "#NP segment not present",
    "#SS stack fault", "#GP general protection", "#PF page fault",
    "reserved", "#MF x87", "#AC alignment check", "#MC machine check",
    "#XF SIMD", "reserved", "reserved", "reserved", "reserved",
    "reserved", "reserved", "reserved", "reserved", "reserved",
    "reserved", "#VE virtualization", "#CP control protection"
};

void ocire_idt_init(void) {
    oc_memset(g_idt, 0, sizeof(g_idt));
    for (int i = 0; i < 32; i++) {
        u64 addr = ocire_stub_ptrs[i];
        g_idt[i].offset_lo  = (u16)(addr & 0xFFFF);
        g_idt[i].offset_mid = (u16)((addr >> 16) & 0xFFFF);
        g_idt[i].offset_hi  = (u32)((addr >> 32) & 0xFFFFFFFF);
        g_idt[i].selector = 0x18;
        g_idt[i].ist = 0;
        g_idt[i].flags = 0x8E;      /* present, interrupt gate, ring0 */
        g_idt[i].reserved = 0;
    }
    g_idt_ptr.limit = sizeof(g_idt) - 1;
    g_idt_ptr.base = (u64)g_idt;
    __asm__ volatile("lidt %0" : : "m"(g_idt_ptr));
    /* External interrupts stay masked: OCIRE polls its devices and
     * never raises the IF flag, but mask the PIC lines explicitly in
     * case the firmware left them configured. */
    oc_outb(0xA1, 0xFF);
    oc_outb(0x21, 0xFF);
}

void ocire_isr_trap(u64 vector, u64 error, u64 rip) {
    ocire_puts("\nOCIRE FAULT: ");
    ocire_puts(vector < 32 ? exc_name[vector] : "unknown vector");
    ocire_puthex(" (vector 0x", vector, 2);
    ocire_puthex(" error 0x", error, 4);
    ocire_puthex(" rip 0x", rip, 16);
    if (vector == 14) {
        u64 cr2;
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        ocire_puthex(" cr2 0x", cr2, 16);
    }
    ocire_puts("\nSystem halted.\n");
    oc_halt();
}
