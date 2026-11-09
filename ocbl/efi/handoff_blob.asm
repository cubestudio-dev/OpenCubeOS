; SPDX-License-Identifier: Apache-2.0
; Copyright 2026 cubestudio-dev <cubestudio@qq.com>
; Open Cube OS - 3.0.0-WP05-ocbl
; File: ocbl/efi/handoff_blob.asm
; Purpose: the UEFI payload handoff trampoline (B5-5).  Assembled as a
;          POSITION-FREE flat binary org'd at 0x30000 and copied there
;          by efi_entry.c (the fixed bounce window the UEFI flavor
;          reserved at B5-3 - the read paths are done with by then).
;
;          Why a low-address blob at all: OVMF loads the OCBL PE image
;          HIGH (typically 0x3Exxxxxx), far above QEMU's RAM - every
;          instruction fetch there is firmware-page-table fiction.  The
;          moment CR0.PG goes off, linear = physical, and the high
;          mapping evaporates.  The downgrade sequence must therefore
;          already live at a physical address that survives, and the
;          far-jump target must be such an address too - the exact
;          trick the BIOS flavor never needs because its core IS low.
;
;          Contract (identical to bios/core_head.S ocbl_handoff, the
;          GRUB mirror): 32-bit protected mode, paging OFF, our GDT,
;          EAX = 0x36D76289, EBX = MBI physical, jump to the kernel's
;          32-bit entry (boot.S _start).
;
;          C entry (System V, called through a plain function pointer
;          after the memcpy - still in long mode here):
;              rdi = kernel 32-bit entry
;              rsi = multiboot2 info physical (0x50000)
;
;          Layout inside the 64 KiB bounce window:
;              0x30000  this code + GDT (< 4 KiB)
;              0x3E000  downgrade stack top (grows down, ~52 KiB free)
;
;          The blob is embedded into the EFI image through
;          efi/handoff_data.asm (incbin), the same two-step shape as
;          the BIOS trampoline (tramp16.bin -> tramp_data.asm).

                bits 64
                org 0x30000

                global ocbl_efi_ho_entry
ocbl_efi_ho_entry:
        ; interrupts off from here to the kernel (the firmware IDT is
        ; about to become fiction together with its page tables)
        cli
        cld
        mov     rcx, rdi               ; kernel 32-bit entry (low 32)
        mov     ebx, esi               ; MBI physical (low 32)
        ; our own GDT: code32 0x08 / data32 0x10, flat 4 GiB
        lgdt    [ocbl_ho_gdt_desc]
        ; low-address stack INSIDE the reserved window
        mov     rsp, 0x3E000
        ; STEP 1: far-jump into the 32-bit COMPATIBLE code segment while
        ; long mode is still active (clearing PG with CS.L=1 faults -
        ; the B1 lesson, unchanged under UEFI).  64-bit far jmp reads
        ; m16:64: 8-byte offset + selector.
        mov     rax, ocbl_ho_32
        mov     [ocbl_ho_fptr], rax
        mov     word [ocbl_ho_fptr+8], 0x08
        jmp     far [ocbl_ho_fptr]

        align 8
ocbl_ho_fptr:
        dq 0
        dw 0

        align 8
ocbl_ho_gdt:
        dq 0                                    ; 0x00 null
        dw 0xFFFF, 0x0000
        db 0x00, 0x9A, 0xCF, 0x00               ; 0x08 code32
        dw 0xFFFF, 0x0000
        db 0x00, 0x92, 0xCF, 0x00               ; 0x10 data32
ocbl_ho_gdt_end:

ocbl_ho_gdt_desc:
        dw ocbl_ho_gdt_end - ocbl_ho_gdt - 1
        dq ocbl_ho_gdt                          ; 64-bit LGDT wants 10 bytes

        bits 32
ocbl_ho_32:
        ; STEP 2: drop the firmware paging (CS.L=0 keeps legacy legal)
        mov     eax, cr0
        btc     eax, 31                         ; PG off -> legacy mode
        mov     cr0, eax
        mov     eax, cr4
        and     eax, ~0x20                      ; PAE off
        mov     cr4, eax
        xor     eax, eax
        mov     cr3, eax
        ; flat 32-bit segments from OUR GDT
        mov     ax, 0x10
        mov     ds, ax
        mov     es, ax
        mov     fs, ax
        mov     gs, ax
        mov     ss, ax
        mov     esp, 0x3E000
        ; the multiboot2 boot magic + MBI, then the kernel's _start
        mov     eax, 0x36D76289
        jmp     ecx
