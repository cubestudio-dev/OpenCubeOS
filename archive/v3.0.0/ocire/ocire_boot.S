; SPDX-License-Identifier: Apache-2.0
; Copyright 2026 cubestudio-dev <cubestudio@qq.com>
; Open Cube OS - 3.0.0-WP02-ocire_recovery
; File: ocire/ocire_boot.S
; Purpose: OCIRE boot entry.  GRUB (BIOS or UEFI) jumps to _start in
;          32-bit protected mode, paging OFF, interrupts OFF:
;              eax = 0x36d76289  (multiboot2 magic)
;              ebx = multiboot2 info pointer
;          WP-02: the magic + info pointer are now SAVED and handed to
;          ocire_main(magic, mbi) - the boot supervisor needs the
;          framebuffer tag, the command line (ocire.mode=) and the raw
;          info block for the kexec hand-over to OCOS (see ocire_mb2.h).
;
;          Steps: own GDT -> identity-map the first 4 GiB with 2 MiB
;          pages -> enable PAE + LME + PG -> far jump into 64-bit
;          long mode -> set up the boot stack -> call ocire_main().
;
;          The file also carries the 32 CPU exception stubs used by
;          the minimal OCIRE IDT (ocire_idt.c): a trap prints a short
;          diagnostic and halts - OCIRE is a recovery kernel, it must
;          never silently spin on a fault.

bits 32

global _start
global __ocire_pml4
global __ocire_pdpt
global __ocire_pd
global __ocire_stack_top
global ocire_isr_stubs
extern ocire_main
extern ocire_isr_trap

section .bss
align 4096
__ocire_pml4:
    resb 4096
__ocire_pdpt:
    resb 4096
__ocire_pd:
    resb 32768             ; 4 PD tables * 4096 bytes (covers 4 GiB)
__ocire_stack_bottom:
    resb 32768              ; 32 KiB boot stack
__ocire_stack_top:


section .rodata
align 8
; GDT - flat 32-bit + flat 64-bit code/data segments (same shape every
; long-mode kernel needs; written for OCIRE, independent of OCOS).
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
.gdt_data64:
    dw 0xffff
    dw 0x0000
    db 0x00
    db 10010010b                 ; access: present, ring0, data, read/write
    db 11001111b
    db 0x00
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start
    dd 0                         ; upper 32 bits of the GDT base (long mode)

section .text
align 16
_start:
    ; Interrupts are already off per the multiboot2 spec; make the
    ; direction flag well-defined too.
    cli
    cld

    ; Save the multiboot2 magic + info pointer (WP-02): they ride in
    ; edi/esi through the GDT load and the far jump; the 32->64
    ; transition zero-extends them into rdi/rsi, which is exactly the
    ; SysV ABI for ocire_main(u64 magic, u64 mbi).
    mov     edi, eax             ; magic
    mov     esi, ebx             ; mbi pointer

    ; Load our own GDT and switch to the flat 32-bit segments.
    lgdt    [gdt_descriptor]
    mov     ax, 0x10             ; data32 selector (index 2 * 8)
    mov     ds, ax
    mov     es, ax
    mov     fs, ax
    mov     gs, ax
    mov     ss, ax
    jmp     0x08:.reload_cs32    ; code32 selector (index 1 * 8)
.reload_cs32:

    ; ---- 4-level paging: identity-map the first 4 GiB -------------
    ; PML4[0] -> PDPT
    mov     eax, __ocire_pdpt
    or      eax, 0x3             ; present, writable
    mov     [__ocire_pml4], eax
    ; PDPT[0..3] -> 4 PD tables (each covers 1 GiB)
    mov     eax, __ocire_pd
    mov     ecx, 0
.fill_pdpt:
    mov     edx, eax
    or      edx, 0x3             ; present, writable
    mov     [__ocire_pdpt + ecx*8], edx
    add     eax, 4096            ; next PD table
    inc     ecx
    cmp     ecx, 4
    jl      .fill_pdpt
    ; Fill all 4 PD tables with 2 MiB huge pages, identity-mapped,
    ; present + writable + huge + global.
    mov     ecx, 0
    mov     eax, 0x83
.fill_pd:
    mov     [__ocire_pd + ecx*8], eax
    add     eax, 0x200000        ; next 2 MiB page
    inc     ecx
    cmp     ecx, 2048            ; 2048 * 2 MiB = 4 GiB
    jl      .fill_pd

    ; CR3 = PML4, enable PAE, set LME, enable paging.
    mov     eax, __ocire_pml4
    mov     cr3, eax

    mov     eax, cr4
    or      eax, 1 << 5           ; PAE
    mov     cr4, eax

    mov     ecx, 0xC0000080       ; IA32_EFER
    rdmsr
    or      eax, (1 << 8)         ; LME
    wrmsr

    mov     eax, cr0
    or      eax, 1 << 31          ; PG
    or      eax, 1 << 16          ; WP
    mov     cr0, eax

    ; Far jump into the 64-bit code segment (0x18 = index 3 * 8).
    jmp     0x18:ocire_long_start

.hang32:
    hlt
    jmp .hang32

; ---------------------------------------------------------------------------
; 64-bit entry
; ---------------------------------------------------------------------------
bits 64
align 16
ocire_long_start:
    ; Reload the data segments with the 64-bit data selector.
    ; GDT layout: 0x00 null, 0x08 code32, 0x10 data32, 0x18 code64,
    ; 0x20 data64 - five descriptors, so the data64 selector is 0x20.
    mov     ax, 0x20
    mov     ds, ax
    mov     es, ax
    mov     fs, ax
    mov     gs, ax
    mov     ss, ax

    ; Boot stack.
    mov     rsp, __ocire_stack_top
    xor     ebp, ebp
    and     rsp, 0xfffffffffffffff0   ; SysV ABI: 16-byte aligned

    call    ocire_main

.hang:
    cli
    hlt
    jmp .hang

; ---------------------------------------------------------------------------
; CPU exception stubs (32 IA vectors).  Vectors that carry an error
; code push nothing extra; the others push a dummy 0 so the stack
; frame layout is uniform for the C handler:
;
;   [saved regs...] [error code] [vector] [rip] [cs] [rflags]
;
; ocire_isr_trap(u64 vector, u64 error, u64 rip) prints the diagnostic.
; Each stub gets its own label isr_0..isr_31; the .rodata table below
; collects their addresses for the C-side IDT setup (ocire_idt.c).
; ---------------------------------------------------------------------------
extern ocire_isr_trap

section .text
align 16
ocire_isr_common:
    ; Save the volatile registers so the C handler can look at them.
    push rax
    push rcx
    push rdx
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    ; C handler: (vector = rdi, error = rsi, rip = rdx)
    mov rdi, [rsp + 9*8]          ; vector
    mov rsi, [rsp + 8*8]          ; error code
    mov rdx, [rsp + 10*8]         ; rip (after vector+error slots)
    call ocire_isr_trap
    ; Never returns on a fault; if it does, halt.
.halt:
    cli
    hlt
    jmp .halt

; Each stub gets its own label isr_0..isr_31; the .rodata table below
; collects their addresses for the C-side IDT setup (ocire_idt.c).
%macro ISR_NOERR 1
isr_%1:
    push qword 0                 ; dummy error code
    push qword %1                 ; vector
    jmp   ocire_isr_common
%endmacro

%macro ISR_ERR 1
isr_%1:
    push qword %1                 ; vector (error code already on stack)
    jmp   ocire_isr_common
%endmacro

%assign i 0
%rep 32
%if i = 8
    ISR_ERR i
%elif (i >= 10 && i <= 14)
    ISR_ERR i
%elif i = 17
    ISR_ERR i
%elif i = 30
    ISR_ERR i
%else
    ISR_NOERR i
%endif
%assign i i+1
%endrep

section .rodata
align 8
global ocire_stub_ptrs

; Macro-parameter text substitution (documented NASM label concatenation).
%macro STUB_DQ 1
    dq isr_%1
%endmacro

ocire_stub_ptrs:
%assign i 0
%rep 32
    STUB_DQ i
%assign i i+1
%endrep
