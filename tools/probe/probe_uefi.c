/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/* Open Cube OS - 3.0.0-WP-runtime-hardening-and-experience-rebase
 * File: tools/probe/probe_uefi.c
 * Purpose: the bare-metal QEMU-behavior probe (UEFI/OVMF side) - the
 *          companion of tools/probe/probe_bios.asm.  Built as its own
 *          EFI application (the same elf_to_efi.py pipeline OCRMECL
 *          uses) and placed as \EFI\BOOT\BOOTX64.EFI on a probe ESP;
 *          it dumps the machine state the firmware hands a UEFI
 *          application:
 *
 *            - the MS-ABI entry registers (image handle / system table)
 *            - GDTR / IDTR (sgdt / sidt)
 *            - CR0 / CR2 / CR3 / CR4 / EFER / RFLAGS
 *            - 8259A IMRs (port reads - harmless under the firmware)
 *            - IOAPIC version (MMIO read)
 *            - the live CR3 page table summary: present entries,
 *              NX-set entries, RO entries per level (what the OCRMECL
 *              NX/RO strip normalizes)
 *
 *          One line per fact, "PROBE:" prefixed, ending with
 *          "PROBE-UEFI done"; the shell driver diffs the DEFAULT vs
 *          STRICT transcripts into the flattening list.
 */
#include "ocr_efi.h"
#include "boot_handoff.h"

static void outb_(u16 port, u8 val)
{
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}
static u8 inb_(u16 port)
{
    u8 v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static void putc_(char c)
{
    while (!(inb_(0x3F8 + 5) & 0x20))
        ;
    outb_(0x3F8, (u8)c);
    if (c == '\n')
        putc_('\r');
}
static void puts_(const char* s)
{
    while (*s)
        putc_(*s++);
}
static void hex_(u64 v, int digits)
{
    static const char h[] = "0123456789ABCDEF";
    if (digits <= 0) {
        digits = 16;
        while (digits > 1 && !(v >> ((digits - 1) * 4)))
            digits--;
    }
    for (int i = digits - 1; i >= 0; i--)
        putc_(h[(v >> (i * 4)) & 0xF]);
}

EFI_CALL efi_status probe_uefi_main(efi_handle image, efi_system_table* st)
{
    (void)image; (void)st;
    outb_(0x3F8 + 1, 0);
    outb_(0x3F8 + 3, 0x80);
    outb_(0x3F8 + 0, 1);
    outb_(0x3F8 + 1, 0);
    outb_(0x3F8 + 3, 0x03);
    outb_(0x3F8 + 2, 0xC7);
    outb_(0x3F8 + 4, 0x0B);

    puts_("PROBE-UEFI v1 (OVMF BOOTX64.EFI entry state)\n");

    u64 rdi, rsi, rbx, rdx, rsp, rbp;
    __asm__ volatile("mov %%rdi, %0" : "=r"(rdi));
    __asm__ volatile("mov %%rsi, %0" : "=r"(rsi));
    __asm__ volatile("mov %%rbx, %0" : "=r"(rbx));
    __asm__ volatile("mov %%rdx, %0" : "=r"(rdx));
    __asm__ volatile("mov %%rsp, %0" : "=r"(rsp));
    __asm__ volatile("mov %%rbp, %0" : "=r"(rbp));
    puts_("PROBE: RCX(image)=");
    hex_((u64)image, 12);
    puts_(" RDX(st)=");
    hex_((u64)st, 12);
    puts_(" RDI=");
    hex_(rdi, 12);
    puts_(" RSI=");
    hex_(rsi, 12);
    puts_(" RBX=");
    hex_(rbx, 12);
    puts_(" RSP=");
    hex_(rsp, 12);
    puts_(" RBP=");
    hex_(rbp, 12);
    puts_("\n");

    struct { u16 limit; u64 base; } __attribute__((packed)) gdtr, idtr;
    __asm__ volatile("sgdt %0" : "=m"(gdtr));
    __asm__ volatile("sidt %0" : "=m"(idtr));
    puts_("PROBE: GDTR=");
    hex_(gdtr.limit, 4);
    putc_(':');
    hex_(gdtr.base, 12);
    puts_(" IDTR=");
    hex_(idtr.limit, 4);
    putc_(':');
    hex_(idtr.base, 12);
    puts_("\n");

    u64 cr0, cr2, cr3, cr4, efer, flags;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    __asm__ volatile("rdmsr" : "=a"(efer) : "c"(0xC0000080u));
    __asm__ volatile("pushfq; pop %0" : "=r"(flags));
    puts_("PROBE: CR0=");
    hex_(cr0, 16);
    puts_(" CR2=");
    hex_(cr2, 12);
    puts_(" CR3=");
    hex_(cr3, 12);
    puts_(" CR4=");
    hex_(cr4, 8);
    puts_(" EFER=");
    hex_(efer, 8);
    puts_(" RFLAGS=");
    hex_(flags, 8);
    puts_("\n");

    puts_("PROBE: PIC-IMR=");
    hex_(inb_(0x21), 2);
    putc_('/');
    hex_(inb_(0xA1), 2);
    puts_("\n");

    /* IOAPIC version via the (identity-mapped) MMIO window */
    *(volatile u32*)(u64)0xFEC00000 = 1;
    puts_("PROBE: IOAPIC-VER=");
    hex_(*(volatile u32*)(u64)0xFEC00010, 8);
    puts_("\n");

    /* the live CR3 tree summary - what the OCRMECL strip normalizes */
    u64* pml4 = (u64*)(cr3 & 0x000FFFFFFFFFF000ULL);
    u64 present = 0, nx = 0, ro = 0;
    for (int i = 0; i < 512; i++) {
        u64 e = pml4[i];
        if (!(e & 1))
            continue;
        present++;
        if (e & (1ULL << 63))
            nx++;
        if (!(e & 2))
            ro++;
    }
    puts_("PROBE: PML4 present=");
    hex_(present, 0);
    puts_(" nx=");
    hex_(nx, 0);
    puts_(" ro=");
    hex_(ro, 0);
    puts_(" (of 512)\n");

    puts_("PROBE-UEFI done\n");
    for (;;)
        __asm__ volatile("hlt");
    return EFI_SUCCESS;
}
