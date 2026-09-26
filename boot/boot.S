; SPDX-License-Identifier: Apache-2.0
; Copyright 2026 cubestudio-dev <cubestudio@qq.com>
; Open Cube OS — WP-01
; File: boot/boot.S
; Purpose: 32-bit entry point. GRUB (BIOS or UEFI) jumps here in 32-bit
;          protected mode with paging OFF, IRQs OFF, with:
;            eax = 0x36d76289  (Multiboot2 magic)
;            ebx = pointer to Multiboot2 info structure
;          We set up our own GDT, identity-map the first 2 GiB with 2 MiB
;          pages, enable PAE+LME+PGE, switch to 64-bit long mode, and call
;          kmain(eax, ebx) — preserving the multiboot2 magic/info pointer.

bits 32

global _start
global __boot_pml4
global __boot_pdpt
global __boot_pd
global __boot_stack_top
extern long_mode_start
extern __mb2_header_start

section .bss
align 4096
__boot_pml4:
    resb 4096
__boot_pdpt:
    resb 4096
__boot_pd:
    resb 32768              ; 4 PD tables * 4096 bytes = 16 KiB (covers 4 GiB)
__boot_stack_bottom:
    resb 16384                  ; 16 KiB boot stack
__boot_stack_top:

section .rodata
; GDT — flat 32-bit + flat 64-bit code/data segments.
gdt_start:
    dq 0                         ; null descriptor
.gdt_code32:
    dw 0xffff                    ; limit low
    dw 0x0000                    ; base low
    db 0x00                      ; base mid
    db 10011010b                 ; access: present, ring0, code, exec/read
    db 11001111b                 ; flags + limit high: 4K gran, 32-bit
    db 0x00                      ; base high
.gdt_data32:
    dw 0xffff
    dw 0x0000
    db 0x00
    db 10010010b                 ; access: present, ring0, data, read/write
    db 11001111b
    db 0x00
.gdt_code64:
    dw 0xffff
    dw 0x0000
    db 0x00
    db 10011010b                 ; access: present, ring0, code, exec/read
    db 10101111b                 ; flags: 4K gran, 64-bit (L=1, D=0)
    db 0x00
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

section .text
align 16
_start:
    ; Save multiboot2 magic + info pointer for later.
    mov     edi, eax             ; magic
    mov     esi, ebx             ; mbi ptr

    ; Make sure we're in a sane state: disable IRQs, clear direction flag.
    cli
    cld

    ; Set up our own GDT and load 32-bit segments.
    lgdt    [gdt_descriptor]
    mov     ax, 0x10             ; data32 selector (index 2 * 8)
    mov     ds, ax
    mov     es, ax
    mov     fs, ax
    mov     gs, ax
    mov     ss, ax
    jmp     0x08:.reload_cs32   ; code32 selector (index 1 * 8)
.reload_cs32:

    ; --- Set up 4-level paging for the first 4 GiB (identity-mapped) ---
    ; PML4[0] -> PDPT
    mov     eax, __boot_pdpt
    or      eax, 0x3             ; present, writable
    mov     [__boot_pml4], eax
    ; PDPT[0..3] -> 4 PD tables (each covers 1 GiB)
    mov     eax, __boot_pd
    mov     ecx, 0
.fill_pdpt:
    mov     edx, eax
    or      edx, 0x3             ; present, writable
    mov     [__boot_pdpt + ecx*8], edx
    add     eax, 4096            ; next PD table
    inc     ecx
    cmp     ecx, 4               ; 4 PD tables = 4 GiB
    jl      .fill_pdpt
    ; Fill all 4 PD tables with 2 MiB pages, identity-mapped, present, writable, huge, global
    mov     ecx, 0
    mov     eax, 0x83           ; present | writable | huge | global
    ; Note: NX bit (bit 63) is NOT set here, so all kernel pages are executable.
    ; The G (global) flag is set for performance but makes TLB flushing harder.
.fill_pd:
    mov     [__boot_pd + ecx*8], eax
    add     eax, 0x200000       ; next 2 MiB
    inc     ecx
    cmp     ecx, 2048           ; 2048 * 2 MiB = 4 GiB
    jl      .fill_pd

    ; Load CR3, enable PAE, set LME, enable paging+PGE.
    mov     eax, __boot_pml4
    mov     cr3, eax

    mov     eax, cr4
    or      eax, 1 << 5         ; PAE
    mov     cr4, eax

    mov     ecx, 0xC0000080     ; IA32_EFER
    rdmsr
    or      eax, (1 << 8) | (1 << 11)  ; LME + NXE (no-execute)
    wrmsr

    mov     eax, cr0
    or      eax, 1 << 31        ; PG
    or      eax, 1 << 16        ; WP (write protect, kernel can't write RO)
    mov     cr0, eax

    ; Far jump to 64-bit code segment (0x18 = code64 selector, index 3 * 8).
    jmp     0x18:long_mode_start

    ; Should never reach here.
.hang:
    hlt
    jmp .hang
