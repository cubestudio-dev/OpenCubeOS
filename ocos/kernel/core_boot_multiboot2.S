/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS NAK - Multiboot2 boot image (WP-01-tree).
 *
 * GRUB hands control to _start in 32-bit protected mode with the
 * Multiboot2 boot information block. The NAK kernel is x86_64-only,
 * so this file carries the Multiboot2 header plus the minimal
 * architectural trampoline - identity page tables, EFER.LME, a tiny
 * 64-bit GDT - to enter long mode and call the C entry point
 * core_main_entry() (kernel/core_main_entry.c).
 *
 * It implements no kernel services by design: no allocator, no
 * interrupt handling, no drivers (those arrive in later NAK packages).
 *
 * _start and _start64 keep their conventional ELF entry-point names
 * (kernel/linker.ld ENTRY() references _start; the name is the
 * ecosystem-wide convention, not an ad-hoc one). Every other symbol
 * here follows the [class]_[category]_[role] naming scheme:
 * CORE_BOOT_* assembler-time constants.
 */

.set CORE_BOOT_MB2_HEADER_MAGIC, 0xE85250D6
.set CORE_BOOT_MB2_ARCH_I386,    0
.set CORE_BOOT_MB2_HEADER_LEN,   multiboot2_header_end - multiboot2_header
.set CORE_BOOT_MB2_CHECKSUM,     -(CORE_BOOT_MB2_HEADER_MAGIC + CORE_BOOT_MB2_ARCH_I386 + CORE_BOOT_MB2_HEADER_LEN)

.set CORE_BOOT_MB2_BOOTLOADER_MAGIC, 0x36D76289

.set CORE_BOOT_GDT_CODE64, 0x08
.set CORE_BOOT_GDT_DATA,   0x10

/*
 * Multiboot2 header: must be 8-byte aligned and sit inside the first
 * 32 KiB of the image; kernel/linker.ld places this section first.
 */
.section .multiboot2_header, "a"
.align 8
multiboot2_header:
    .long CORE_BOOT_MB2_HEADER_MAGIC
    .long CORE_BOOT_MB2_ARCH_I386
    .long CORE_BOOT_MB2_HEADER_LEN
    .long CORE_BOOT_MB2_CHECKSUM
    /* end tag */
    .short 0                    /* type = end */
    .short 0                    /* flags */
    .long 8                     /* size */
multiboot2_header_end:

/* Boot data: the bootloader handoff registers, saved before paging. */
.section .data
.align 8
mb2_boot_magic: .long 0         /* EAX - Multiboot2 magic */
mb2_info_addr:  .long 0         /* EBX - boot info physical address */

.align 8
gdt64:
    .quad 0x0000000000000000    /* 0x00: null descriptor */
    .quad 0x00AF9A000000FFFF    /* 0x08: 64-bit code (L = 1) */
    .quad 0x00CF92000000FFFF    /* 0x10: data */
gdt64_end:
gdt64_descriptor:
    .word gdt64_end - gdt64 - 1 /* limit */
    .long gdt64                 /* base (32-bit form for lgdt) */

/*
 * Boot page tables and stack, one page each; the trampoline identity
 * maps the first 1 GiB of physical memory with 2 MiB pages, which is
 * exactly where this kernel is loaded and linked.
 */
.section .bss, "aw", @nobits
.align 4096
pml4:
    .skip 4096
pdpt:
    .skip 4096
pd:
    .skip 4096
boot_stack:
    .skip 16384
boot_stack_top:

/*
 * Entry point: 32-bit protected mode, paging off.
 */
.section .text
.code32
.global _start
.type _start, @function
_start:
    cli
    cld

    /* keep the bootloader handoff values before clobbering registers */
    movl %eax, mb2_boot_magic
    movl %ebx, mb2_info_addr

    movl $boot_stack_top, %esp

    /* CPUID available? (EFLAGS.ID toggle test) */
    pushfl
    popl %eax
    movl %eax, %ecx
    xorl $0x00200000, %eax
    pushl %eax
    popfl
    pushfl
    popl %eax
    cmpl %ecx, %eax
    je .Lno_long_mode

    /* long mode available? (CPUID 0x80000001, EDX bit 29) */
    movl $0x80000000, %eax
    cpuid
    cmpl $0x80000001, %eax
    jb .Lno_long_mode
    movl $0x80000001, %eax
    cpuid
    testl $0x20000000, %edx
    jz .Lno_long_mode

    /* identity map the first 1 GiB: PML4[0] -> PDPT[0] -> 512 x 2 MiB.
     * PAE page-table entries are 64-bit, so every entry is written as
     * a low dword plus an explicitly zeroed high dword (stride 8). */
    movl $pdpt, %eax
    orl  $0x3, %eax             /* present | writable */
    movl %eax, pml4
    movl $0x0, pml4+4
    movl $pd, %eax
    orl  $0x3, %eax
    movl %eax, pdpt
    movl $0x0, pdpt+4

    xorl %ecx, %ecx
    xorl %esi, %esi             /* zero high dword of each entry */
    movl $pd, %edi
    movl $0x83, %eax            /* present | writable | 2 MiB page */
.Lfill_pd:
    movl %eax, (%edi, %ecx, 8)
    movl %esi, 4(%edi, %ecx, 8)
    addl $0x200000, %eax
    incl %ecx
    cmpl $512, %ecx
    jne .Lfill_pd

    /* CR4.PAE */
    movl %cr4, %eax
    orl  $0x20, %eax
    movl %eax, %cr4

    /* CR3 = PML4 */
    movl $pml4, %eax
    movl %eax, %cr3

    /* EFER.LME = 1 (MSR 0xC0000080, bit 8) */
    movl $0xC0000080, %ecx
    rdmsr
    orl  $0x100, %eax
    wrmsr

    /* CR0.PG -> compatibility mode with long mode armed */
    movl %cr0, %eax
    orl  $0x80000000, %eax
    movl %eax, %cr0

    /* far jump into the 64-bit code segment */
    lgdt gdt64_descriptor
    ljmp $CORE_BOOT_GDT_CODE64, $_start64

.Lno_long_mode:
    /* x86_64 is locked in as the NAK architecture; nothing to fall
     * back to. Halt forever. */
    cli
.Lhalt:
    hlt
    jmp .Lhalt
.size _start, . - _start

/*
 * 64-bit entry: reload segment registers, hand the saved Multiboot2
 * values to core_main_entry(magic, info_addr) and stop.
 */
.code64
.global _start64
.type _start64, @function
_start64:
    movw $CORE_BOOT_GDT_DATA, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    xorw %ax, %ax
    movw %ax, %fs
    movw %ax, %gs

    movl mb2_boot_magic, %edi
    movl mb2_info_addr, %esi
    call core_main_entry

    /* core_main_entry never returns; spin if it ever does */
    cli
.Lspin:
    hlt
    jmp .Lspin
.size _start64, . - _start64

/*
 * 64-bit entry pointer for GRUB-less bootloaders (OCBL, WP-02-ocbl):
 * GRUB enters the kernel in 32-bit protected mode at _start, but a
 * bootloader that is ALREADY in long mode must not execute that
 * 32-bit trampoline. Such a bootloader locates this section by name
 * (.ocbl_boot64) and jumps to the absolute _start64 address stored
 * here. The section travels with the loaded image (linker.ld keeps
 * it in the PT_LOAD coverage).
 */
.section .ocbl_boot64, "a"
.align 8
.globl core_boot_entry64_ptr
core_boot_entry64_ptr:
    .quad _start64

.section .note.GNU-stack, "", @progbits
