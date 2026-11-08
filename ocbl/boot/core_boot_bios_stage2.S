/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS NAK - OCBL BIOS stage 2a, the 32-bit trampoline
 * (WP-02-ocbl).
 *
 * Entered at 0x7E00 in 16-bit real mode from stage 1. Job, in order:
 *
 *   1. read the kernel ELF from disk into CORE_BOOT_BIOS_KERNEL_BASE
 *      (real mode, INT 13h AH=42h, 64-sector batches; the transfer
 *      buffer is passed to the BIOS packed as segment:offset, and the
 *      sector count is patched into ocbl_kernel_sectors by the
 *      Makefile);
 *   2. open A20 (INT 15h AX=2401h, then the fast A20 gate at port
 *      0x92 - both are idempotent when already enabled);
 *   3. load the OCBL GDT, set CR0.PE and enter 32-bit protected mode;
 *   4. build the identity page tables (PML4/PDPT/PD at 0x1000/0x2000/
 *      0x3000, 2 MiB pages covering the first 1 GiB), enable PAE,
 *      CR3, EFER.LME and CR0.PG;
 *   5. far jump into the 64-bit code segment at CORE_BOOT_BIOS_64_ENTRY,
 *      where stage 2b + the 64-bit C payload live.
 *
 * Naming: [class]_[category]_[role] - core (boot core), boot
 * (bootloader), bios_stage2 (BIOS second stage, protected-mode part).
 */

#include "boot/core_boot_bios_layout.h"

.set BOOT_DRIVE_SLOT,   0x500

.set SEL_CODE32, 0x08
.set SEL_DATA32, 0x10
.set SEL_CODE64, 0x18

.set CR0_PG,  0x80000000
.set CR4_PAE, 0x00000020
.set MSR_EFER, 0xC0000080
.set EFER_LME, 0x00000100

.set PML4_ADDR, 0x1000
.set PDPT_ADDR, 0x2000
.set PD_ADDR,   0x3000

/* VGA-screen progress marks: one glyph per completed boot step, row
 * 24, left to right (0x0F = white on black). Purely diagnostic; the
 * kernel clears the screen again at startup. The cursor variable
 * lives in the data area at the END of the file so that _start stays
 * exactly at the link base 0x7E00. */
.set VGA_TRACE_SEG,   0xB800
.set VGA_TRACE_OFF,   0x0F00          /* row 24, column 0 (bytes) */
.set VGA_TRACE_ATTR,  0x0F00

.macro VGA_TRACE ch
    pushw %ax
    pushw %bx
    pushw %es
    movw $VGA_TRACE_SEG, %ax
    movw %ax, %es
    movzwl vga_trace_pos, %ebx
    movw $VGA_TRACE_ATTR | \ch, %es:(%ebx)
    addw $2, vga_trace_pos
    popw %es
    popw %bx
    popw %ax
.endm

.macro VGA_TRACE32 ch
    pushl %eax
    pushl %ebx
    movzwl vga_trace_pos, %ebx
    movw $VGA_TRACE_ATTR | \ch, 0xB8000(%ebx)
    addw $2, vga_trace_pos
    popl %ebx
    popl %eax
.endm

.code16
.text
.globl _start
_start:
    VGA_TRACE '2'
    /* ---- 1. read the kernel in 64-sector batches -------------------- */
    movl (ocbl_kernel_sectors), %ecx
    testl %ecx, %ecx
    jz   .Lkernel_done
    xorl %esi, %esi                    /* LBA cursor, relative to 64 */
    movl $CORE_BOOT_BIOS_KERNEL_BASE, %edi

.Lread_batch:
    movl $64, %ebx                     /* full batch */
    cmpl %ebx, %ecx
    jae  .Lhave_batch
    movl %ecx, %ebx                    /* final short batch */
.Lhave_batch:
    movw %bx, (dap_kernel + 2)         /* block count */
    movl %edi, %eax                    /* buffer packed seg:off: the
                                          batch cursor is always 16-byte
                                          aligned, so segment = addr >> 4
                                          and offset = 0, i.e. addr << 12 */
    shll $12, %eax
    movl %eax, (dap_kernel + 4)
    leal CORE_BOOT_BIOS_KERNEL_LBA(%esi), %eax
    movl %eax, (dap_kernel + 8)        /* absolute LBA of this batch */

    movb $0x42, %ah
    movb BOOT_DRIVE_SLOT, %dl
    movw $dap_kernel, %si
    int  $0x13
    jc   .Lfail

    /* the BIOS back-fills the DAP count field with the number of
     * blocks actually transferred - AH/AX themselves are NOT the
     * transfer count on return */
    movzwl (dap_kernel + 2), %eax
    subl %eax, %ecx                    /* blocks actually transferred */
    shll $9, %eax                      /* * 512 bytes */
    addl %eax, %edi
    addl %eax, %esi
    testl %ecx, %ecx
    jnz  .Lread_batch
.Lkernel_done:
    VGA_TRACE '3'

    /* ---- 2. A20 ------------------------------------------------------ */
    xorw %ax, %ax
    movw $0x2401, %ax                  /* INT 15h enable A20; ignore rc */
    int  $0x15
    inb  $0x92, %al                    /* fast A20 gate */
    testb $0x02, %al
    jnz  .La20_ok
    orb  $0x02, %al
    andb $0xFE, %al                    /* never touch the fast-reset bit */
    outb %al, $0x92
.La20_ok:

    /* ---- 3. protected mode ------------------------------------------- */
    cli
    lgdt gdt_desc
    movl %cr0, %eax
    orl  $1, %eax                      /* CR0.PE */
    movl %eax, %cr0
    ljmp $SEL_CODE32, $pm32

/* ---- 32-bit protected mode ------------------------------------------- */
.code32
pm32:
    movw $SEL_DATA32, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw %ax, %fs
    movw %ax, %gs
    movl $0x7C00, %esp                 /* below stage 1, growing down */

    /* ---- 4. identity page tables: 1 GiB with 2 MiB pages ------------ */
    movl $PDPT_ADDR, %eax
    orl  $3, %eax                      /* present | writable */
    movl %eax, PML4_ADDR
    movl $PD_ADDR, %eax
    orl  $3, %eax
    movl %eax, PDPT_ADDR

    xorl %ecx, %ecx
    movl $PD_ADDR, %edi
    movl $0x83, %eax                   /* present | writable | 2 MiB page */
1:  movl %eax, (%edi, %ecx, 8)
    movl $0, 4(%edi, %ecx, 8)          /* PAE entries are 64-bit */
    addl $0x200000, %eax
    incl %ecx
    cmpl $512, %ecx
    jne  1b
    VGA_TRACE32 '5'

    movl %cr4, %eax
    orl  $CR4_PAE, %eax
    movl %eax, %cr4
    movl $PML4_ADDR, %eax
    movl %eax, %cr3
    movl $MSR_EFER, %ecx
    rdmsr
    orl  $EFER_LME, %eax
    wrmsr
    movl %cr0, %eax
    orl  $CR0_PG, %eax
    movl %eax, %cr0
    VGA_TRACE32 '6'


    /* ---- 5. far jump into the 64-bit payload ------------------------- */
    ljmp $SEL_CODE64, $CORE_BOOT_BIOS_64_ENTRY

.Lfail:
    cli
.Lhalt:
    hlt
    jmp .Lhalt

/* ---- data ------------------------------------------------------------- */

/* GDT: null | 32-bit code | 32-bit data | 64-bit code */
.align 8
gdt:
    .quad 0x0000000000000000
    .quad 0x00CF9A000000FFFF
    .quad 0x00CF92000000FFFF
    .quad 0x00AF9A000000FFFF
gdt_end:
gdt_desc:
    .word gdt_end - gdt - 1
    .long gdt

.align 4
dap_kernel:
    .byte 0x10, 0
    .word 0                            /* count, patched per batch */
    .long 0                            /* buffer */
    .quad 0                            /* LBA */

/* Disk layout facts consumed by stage 2a and patched by the Makefile.
 * The sector count is patched at build time (kernel size is not known
 * until the kernel is built). */
.globl ocbl_kernel_first_lba
ocbl_kernel_first_lba:
    .long CORE_BOOT_BIOS_KERNEL_LBA
.globl ocbl_kernel_sectors
ocbl_kernel_sectors:
    .long 0                            /* PATCHED by ocbl/Makefile */
.globl ocbl_kernel_load_base
ocbl_kernel_load_base:
    .long CORE_BOOT_BIOS_KERNEL_BASE

/* cursor for the VGA progress marks (loaded with the image, so the
 * initial value below is live) */
vga_trace_pos:
    .word VGA_TRACE_OFF
