; SPDX-License-Identifier: Apache-2.0
; Open Cube OS - WP-02
; File: kernel/idt_stub.S
; Purpose: Generate 256 interrupt entry stubs. Each stub pushes its vector
;          number (and a fake 0 error code for exceptions that don't push one)
;          and jumps to oc_isr_dispatch.
;
; We use a NASM macro to emit 256 stubs. Stubs for vectors 0..31 (exceptions)
; are split into two groups:
;   - vectors that DO push an error code: 8, 10, 11, 12, 13, 14, 17, 30
;     (DF, TS, NP, SS, GP, PF, AC, SX) - stub pushes only the vector number.
;   - all others - stub pushes a fake 0 error code first, then vector number.
; This keeps the frame layout uniform: every entry to oc_isr_dispatch sees
; [vector, error_code] on the stack.

bits 64
default rel

extern oc_isr_dispatch

section .rodata
align 8
global oc_isr_table
oc_isr_table:
    %assign i 0
    %rep 256
        dq oc_isr_stub_%+ i
    %assign i i+1
    %endrep

section .text
align 16

; Macro: define a stub for vector `i`.
; If `i` is in the error-code-pushing list, the stub does NOT push a fake 0.
%macro OC_STUB 1
    global oc_isr_stub_%+%1
    align 16
oc_isr_stub_%+%1:
    %if !(%1 == 8 || %1 == 10 || %1 == 11 || %1 == 12 || %1 == 13 || %1 == 14 || %1 == 17 || %1 == 30)
        push qword 0           ; fake error code so the frame is uniform
    %endif
    push qword %1              ; vector number
    jmp oc_isr_common
%endmacro

; Emit all 256 stubs.
%assign i 0
%rep 256
    OC_STUB i
%assign i i+1
%endrep

; Common entry: save all GPRs, call dispatcher, restore, iretq.
align 16
oc_isr_common:
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
    ; The oc_irq_frame_t struct starts with r15 at its lowest address, so
    ; rdi = rsp is exactly the frame pointer we want.
    mov rdi, rsp
    ; Align stack to 16 bytes for the call (SysV ABI requirement).
    ; Entry to oc_isr_common: rsp = original - 56 (40 CPU + 8 error + 8 vector).
    ; After 15 GPR pushes: rsp = original - 176 (176 % 16 = 0, aligned).
    ; But call itself pushes 8, making it 184 % 16 = 8 (misaligned).
    ; So sub rsp, 8 before the call to keep alignment.
    sub rsp, 8
    call oc_isr_dispatch
    add rsp, 8

    ; Restore GPRs.
    pop r15
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
