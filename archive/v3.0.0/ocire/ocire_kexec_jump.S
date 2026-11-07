; SPDX-License-Identifier: Apache-2.0
; Copyright 2026 cubestudio-dev <cubestudio@qq.com>
; Open Cube OS - 3.0.0-WP02-ocire_recovery
; File: ocire/ocire_kexec.S
; Purpose: the kexec 64 -> 32 transition.  OCIRE runs in 64-bit long
;          mode with a 4 GiB identity map; OCOS's boot.S expects the
;          multiboot2 machine state (32-bit protected mode, paging
;          OFF, flat segments, eax = 0x36d76289, ebx = info pointer).
;
;          The whole sequence executes from OCIRE's own .text at
;          16 MiB.  Because the identity map makes virtual == physical
;          for every address the code touches, turning paging off
;          mid-sequence keeps instruction fetch and stack access
;          valid (the same linear addresses keep working as physical
;          ones).  The OCOS image was already copied to its 1 MiB
;          home by ocire_kexec_exec, and the OCIRE image at 16 MiB
;          is never touched by it.
;
;          Register state at entry (SysV):
;            rdi = OCOS entry point (physical, < 4 GiB)
;            rsi = rebuilt multiboot2 info address (physical)
;          The GDT selectors are OCIRE's own flat set:
;            0x08 code32, 0x10 data32, 0x18 code64, 0x20 data64.

bits 64

global ocire_kexec_jump32

section .text
align 16
ocire_kexec_jump32:
    ; Interrupts stay off (they were never enabled in OCIRE).
    cli

    ; Save the arguments where the 32-bit side can reach them
    ; (callee-saved regs survive the far return untouched).
    mov rbx, rsi                ; ebx = multiboot2 info address
    mov rbp, rdi                ; ebp = target entry

    ; ---- switch the instruction stream to the 32-bit code segment
    ; (compatibility mode; long mode + paging still on)
    lea rax, [rel .compat32]
    push qword 0x08             ; OCIRE GDT: code32
    push rax
    retfq

bits 32
.compat32:
    ; ---- paging off (RIP keeps resolving: identity map) ----
    mov eax, cr0
    and eax, 0x7FFFFFFF         ; clear PG
    mov cr0, eax

    ; ---- leave long mode: clear EFER.LME ----
    mov ecx, 0xC0000080
    rdmsr
    and eax, 0xFFFFFEFF         ; clear LME (bit 8)
    wrmsr

    ; ---- PAE off (32-bit tables semantics no longer needed) ----
    mov eax, cr4
    and eax, 0xFFFFFFDF         ; clear PAE (bit 5)
    mov cr4, eax

    ; ---- flat 32-bit data segments (OCIRE GDT, still loaded) ----
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; ---- multiboot2 handover registers ----
    mov eax, 0x36D76289         ; magic
    ; ebx already holds the info address (rsi was parked in rbx above;
    ; the 64->compat switch preserves the low 32 bits)

    ; ---- jump into the OCOS entry (32-bit flat near jump) ----
    mov edx, ebp
    jmp edx

.hang32:
    cli
    hlt
    jmp .hang32
