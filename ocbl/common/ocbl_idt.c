/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - WP-tui-input-encoding-hardening 4.5
 * File: ocbl/common/ocbl_idt.c
 * Purpose: build + load the minimal long-mode IDT (see ocbl_idt.S).
 *
 *          The table sits in the core's own .bss (4 KiB, 256 x 16-byte
 *          interrupt gates, DPL 0, IST 0) so no fixed low-memory
 *          address is consumed and the UEFI flavor works unchanged.
 *          Vectors 0..31 route to the report-and-halt exception stubs
 *          (vector 2 = NMI is deliberately an iretq: a bootloader
 *          does not act on parity/ECC NMIs); 32..255 are iretq.
 */
#include "ocbl_types.h"
#include "ocbl_serial.h"

/* from ocbl_idt.S */
extern void ocbl_idt_irq_stub(void);
extern const u64 ocbl_idt_exc_stub_table[32];
void ocbl_idt_exception_report(u64 vector, u64 rip);

/* 256 gates x 16 bytes = 4096, 16-byte aligned */
static u8 g_idt[4096] __attribute__((aligned(16)));

/* the table facts the BIOS trampoline needs to RESTORE the IDT on
 * its way back from a real-mode window (tramp_init copies them to
 * the fixed scratch pair 0x404F4/0x404F6; BACK32 lidt's from there).
 * Exported non-static for ocbl_bios_asm.S; unused (but harmless) in
 * the UEFI flavor, which has no real-mode window at all. */
u64 ocbl_idt_table_base = 0;
u32 ocbl_idt_table_limit = 0;

/* the 16-byte long-mode gate, laid out EXACTLY like the kernel's
 * arch_idt_gate_t (arch/x86_64/arch_idt.h) - the in-repo authority:
 *   bytes 0-1  offset bits 15:0
 *   bytes 2-3  selector
 *   byte  4    IST index
 *   byte  5    P|DPL|0|type   (0x8E = present, DPL 0, interrupt gate)
 *   bytes 6-7  offset bits 31:16
 *   bytes 8-11 offset bits 63:32
 *   bytes 12-15 reserved
 * (The first draft of this file put the type byte at 7 and shifted
 * the offsets - a gate with P=0 - and every vectoring #GP'd into a
 * triple fault; live-caught by the breakpoint-NMI injection test
 * before it ever shipped.) */
typedef struct {
    u16 offset_low;
    u16 selector;
    u8  ist;
    u8  flags;
    u16 offset_mid;
    u32 offset_high;
    u32 reserved0;
} __attribute__((packed)) ocbl_gate_t;

static void idt_fill_gate(int vector, u64 handler, u16 selector)
{
    ocbl_gate_t *g = (ocbl_gate_t *)(g_idt + (u32)vector * 16);
    g->offset_low  = (u16)(handler & 0xFFFF);
    g->selector    = selector;
    g->ist         = 0;
    g->flags       = 0x8E;             /* P=1 DPL=0 type=0xE (int gate) */
    g->offset_mid  = (u16)((handler >> 16) & 0xFFFF);
    g->offset_high = (u32)((handler >> 32) & 0xFFFFFFFF);
    g->reserved0   = 0;
}

void ocbl_idt_setup(void)
{
    /* the gate selector is the CURRENTLY ACTIVE 64-bit code
     * segment - read from CS at runtime instead of hardcoding a
     * number: the BIOS flavor runs under core_gdt's 0x18 (code64),
     * the UEFI flavor under whatever 64-bit CS OVMF handed us.
     * Hardcoding either value would misvector the other flavor
     * (a compat-mode gate selector makes the 64-bit stubs run in
     * 32-bit compat mode, where iretq decodes as iretd and the
     * stack frame sizes mismatch - found in review before the
     * first build, never shipped). */
    u16 cs;
    __asm__ volatile("movw %%cs, %0" : "=r"(cs));

    for (int v = 0; v < 256; v++) {
        u64 h;
        if (v < 32 && ocbl_idt_exc_stub_table[v] != 0)
            h = ocbl_idt_exc_stub_table[v];
        else
            h = (u64)ocbl_idt_irq_stub;
        idt_fill_gate(v, h, cs);
    }
    static const struct { u16 limit; u64 base; } __attribute__((packed))
    idtr = { 256 * 16 - 1, (u64)g_idt };
    __asm__ volatile("lidt %0" : : "m"(idtr));

    /* publish the facts for the trampoline's return path */
    ocbl_idt_table_base = (u64)g_idt;
    ocbl_idt_table_limit = 256 * 16 - 1;
    /* a fault BEFORE this point still escalates the old way; from
     * here on every vector lands in our own handlers */
}

/* the report body: called from ocbl_idt.S with IF cleared by the
 * interrupt gate; never returns */
static const char *exc_name(u64 v)
{
    switch (v) {
    case 0:  return "#DE divide";
    case 1:  return "#DB debug";
    case 3:  return "#BP breakpoint";
    case 4:  return "#OF overflow";
    case 5:  return "#BR bound";
    case 6:  return "#UD invalid-opcode";
    case 7:  return "#NM no-fpu";
    case 8:  return "#DF double-fault";
    case 10: return "#TS tss";
    case 11: return "#NP segment";
    case 12: return "#SS stack";
    case 13: return "#GP general-protection";
    case 14: return "#PF page";
    case 16: return "#MF fpu";
    case 17: return "#AC alignment";
    case 18: return "#MC machine-check";
    case 19: return "#XF simd";
    default: return "exception";
    }
}

void ocbl_idt_exception_report(u64 vector, u64 rip)
{
    ocbl_serial_puts("\r\n[ocbl] CPU exception ");
    ocbl_serial_dec(vector);
    ocbl_serial_puts(" (");
    ocbl_serial_puts(exc_name(vector));
    ocbl_serial_puts(") at rip=0x");
    ocbl_serial_hex(rip, 16);
    ocbl_serial_puts(" - the IDT caught it; halting (no reset loop)\r\n");
    /* deliberate park: a diagnosable stop, never a silent reboot */
    for (;;)
        __asm__ volatile("cli; hlt");
}
