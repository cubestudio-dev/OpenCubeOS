; SPDX-License-Identifier: Apache-2.0
; Copyright 2026 cubestudio-dev <cubestudio@qq.com>
; Open Cube OS - WP-02
; File: kernel/arch_idt_stub.S
; Purpose: Generate 256 interrupt entry stubs. Each stub pushes its vector
;          number (and a fake 0 error code for exceptions that don't push one)
;          and jumps to arch_isr_dispatch.
;
; We use a NASM macro to emit 256 stubs. Stubs for vectors 0..31 (exceptions)
; are split into two groups:
;   - vectors that DO push an error code: 8, 10, 11, 12, 13, 14, 17, 30
;     (DF, TS, NP, SS, GP, PF, AC, SX) - stub pushes only the vector number.
;   - all others - stub pushes a fake 0 error code first, then vector number.
; This keeps the frame layout uniform: every entry to arch_isr_dispatch sees
; [vector, error_code] on the stack.

bits 64
default rel

extern arch_isr_dispatch

section .rodata
align 8
global arch_isr_table
arch_isr_table:
    %assign i 0
    %rep 256
        dq arch_isr_stub_%+ i
    %assign i i+1
    %endrep

section .text
align 16

; Macro: define a stub for vector `i`.
; If `i` is in the error-code-pushing list, the stub does NOT push a fake 0.
%macro OC_STUB 1
    global arch_isr_stub_%+%1
    align 16
arch_isr_stub_%+%1:
    %if !(%1 == 8 || %1 == 10 || %1 == 11 || %1 == 12 || %1 == 13 || %1 == 14 || %1 == 17 || %1 == 30)
        push qword 0           ; fake error code so the frame is uniform
    %endif
    push qword %1              ; vector number
    jmp arch_isr_common
%endmacro

; Emit all 256 stubs.
%assign i 0
%rep 256
    OC_STUB i
%assign i i+1
%endrep

; Common entry: save all GPRs, call dispatcher, restore, iretq.
align 16
arch_isr_common:
    ; BUG-0138 FIX (A1-5): interrupt/trap gates do not clear DF. A ring3
    ; `std` followed by any interrupt or syscall would otherwise run the
    ; whole kernel dispatch chain with DF=1, and GCC can emit `rep movsb`
    ; for aggregate copies even under -mno-sse -> silent memory corruption.
    cld
    ; Save general-purpose registers (SysV order doesn't matter here, we
    ; pass the frame pointer in rdi).
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    ; rdi = pointer to the saved-register block on the stack.
    ; After pushing 15 GPRs, rsp points at r15 (the last thing pushed).
    ; The arch_irq_frame_t struct starts with r15 at its lowest address, so
    ; rdi = rsp is exactly the frame pointer we want.
    mov rdi, rsp
    ; BUG-0137 FIX (A1-4): SysV ABI requires rsp = 0 (mod 16) AT the call
    ; instruction, so that the callee sees rsp = 8 (mod 16) on entry. In
    ; 64-bit mode the CPU pushes 5 quadwords (SS, RSP, RFLAGS, CS, RIP =
    ; 40 bytes) for EVERY interrupt/exception - ring3 and ring0 sources
    ; alike; IRETQ always pops those 5, and a same-privilege frame is also
    ; 5 quadwords (the 3-quadword frame is a 32-bit-mode concept). So the
    ; frame below the stub base is a constant 40 + 16 (err+vector) + 120
    ; (15 GPRs) = 176 = 11*16 bytes. What varies is the BASE alignment:
    ; TSS rsp0 / IST tops are 16-aligned in this kernel (kmalloc payloads
    ; are 16-aligned, static stacks are aligned(16)), but the interrupted
    ; ring0 rsp can be 0 or 8 (mod 16) depending on the interrupt point.
    ; The previous fixed `sub rsp, 8` was therefore inverted: on every
    ; 16-aligned-base path the call site was ALREADY aligned and the sub
    ; misaligned it (call at 8 mod 16 -> dispatch entry rsp = 0 mod 16,
    ; an ABI violation masked only by -mno-sse). Align dynamically instead.
    ; r15 already carries its saved copy on the stack (it was pushed as the
    ; last GPR, exactly where rdi points), so we can borrow it to bridge
    ; the aligned region and let its own `pop r15` restore the real value.
    mov r15, rsp            ; remember the saved-register block top...
    and rsp, -16            ; ...align for the call...
    call arch_isr_dispatch
    mov rsp, r15            ; ...return to the block top...
    pop r15                 ; ...and restore the real r15 from its slot

    ; Restore GPRs (r15 was already restored above).
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rdx
    pop rcx
    pop rbx
    pop rax

    ; Drop the (vector, error_code) pair pushed by the stub.
    add rsp, 16

    iretq
