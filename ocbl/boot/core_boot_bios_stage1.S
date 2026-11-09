/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 cubestudio-dev <cubestudio@qq.com> */
/*
 * Open Cube OS NAK - OCBL BIOS stage 1, the MBR boot sector
 * (WP-02-ocbl).
 *
 * Exactly 512 bytes, loaded by the firmware at 0x7C00 in 16-bit real
 * mode. No hardcoded absolute memory addresses beyond the documented
 * OCBL disk/memory layout (see ocbl/README.md): every destination is
 * a named constant defined once in this file or in stage 2.
 *
 * Job:
 *   1. save the boot drive number (DL) at BOOT_DRIVE_SLOT,
 *   2. set up a stack,
 *   3. verify INT 13h extensions are present (AH=41h, BX=55AA),
 *   4. LBA-read stage 2a (32-bit trampoline) to STAGE2A_LOAD and
 *      stage 2b (64-bit C payload) to STAGE2B_LOAD,
 *   5. far jump into stage 2a.
 *
 * The kernel itself is NOT read here - stage 2a reads it in real
 * mode with the full (patched) sector count.
 *
 * Disk layout of ocbl-bios.bin (LBA):
 *   0        this sector
 *   1..31    stage 2a (31 sectors max)
 *   32..63   stage 2b (32 sectors max)
 *   64..     kernel.elf (count patched into stage 2a by the Makefile)
 *
 * Naming: [class]_[category]_[role] - core (boot core), boot
 * (bootloader), bios_stage1 (BIOS first stage).
 */

.set BOOT_DRIVE_SLOT,   0x500   /* conventional free byte in low memory */
.set STAGE2A_LOAD,      0x7E00  /* right after this sector */
.set STAGE2A_SECTORS,   32      /* LBA 1..32: stage 2a (0x7E00..0xBE00) */
.set STAGE2B_SECTORS,   32      /* LBA 33..64: stage 2b (0xBE00..0xFE00) */
.set STAGE2_TOTAL,      63      /* one extended read loads both */

.code16
.text
.globl _start
_start:
    cli
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0x7C00, %sp
    sti                         /* keep interrupts for the BIOS calls */

    /* remember which drive the firmware booted us from (DL = 0x80
     * for the boot HDD) - before any register gets clobbered */
    movb %dl, BOOT_DRIVE_SLOT

#ifdef OCBL_BOOT_CD
    /* El Torito no-emulation flavor (OCBL_BOOT_CD, the BIOS boot
     * image of opencubenak-WP-02-ocbl.iso): the firmware has already
     * loaded the WHOLE image - this sector, stage 2a, stage 2b and
     * the kernel.elf at image LBA 64 - contiguously at 0x7C00, which
     * is exactly the layout the image is assembled for (stage 2a at
     * 0x7E00, stage 2b at its link address 0xBC00, kernel at
     * 0x7C00 + 32 KiB). Nothing has to be read from the disk:
     * jump straight into stage 2a. */
    ljmp $0, $STAGE2A_LOAD
#else
    /* INT 13h extensions present? (AH=41h, BX=55AA) */
    movw $0x55AA, %bx
    movb $0x41, %ah
    movb BOOT_DRIVE_SLOT, %dl
    int  $0x13
    jc   .Lno_ext
    cmpw $0xAA55, %bx
    jne  .Lno_ext

    /* stage 2a + 2b -> 0x7E00..0xFE00 (one read, buffer stays inside
     * the first 64 KiB segment: 0x7E00 + 64*512 = 0xFE00) */
    movw $STAGE2_TOTAL, (dap_stage2a + 2)
    movl $STAGE2A_LOAD, (dap_stage2a + 4)
    movl $1, (dap_stage2a + 8)          /* LBA 1 */
    movb $0x42, %ah
    movb BOOT_DRIVE_SLOT, %dl
    movw $dap_stage2a, %si
    int  $0x13
    jc   .Lno_ext

    ljmp $0, $STAGE2A_LOAD

.Lno_ext:
    /* no extended INT 13h: OCBL cannot read the disk. Halt forever. */
    cli
.Lhalt:
    hlt
    jmp .Lhalt
#endif

/* Disk Address Packets (16 bytes each):
 *   +0 size, +1 zero, +2 count, +4 transfer buffer packed as
 *   (segment << 16) | offset - NOT a physical address: segment 0
 *   covers only the first 64 KiB. Higher buffers need a real segment
 *   (0x1000:0000 = 0x100000, 0x2000:0000 = 0x200000...). +8 LBA. */
.align 4
dap_stage2a:
    .byte 0x10, 0
    .word 0, 0, 0                       /* count, buffer, patched above */
    .quad 0                             /* LBA */
    .quad 0                             /* padding to 16 bytes */

.fill 510 - (. - _start), 1, 0        /* pad out to 510 bytes */
.word 0xAA55
