; SPDX-License-Identifier: Apache-2.0
; Copyright 2026 cubestudio-dev <cubestudio@qq.com>
; Open Cube OS - WP-10-input_chipset
; File: kernel/arch/x86_64/arch_acpi_s3.S
; Purpose: S3 suspend leaf stub. Saves the minimal CPU state into the
;          trampoline save area, then performs the PM1a_CNT write that
;          puts the machine to sleep. If the write returns (sleep not
;          supported), the frame is unwound normally with eax = -1.
;
;          On RESUME the firmware jumps to the 32-bit trampoline, which
;          restores RSP to the saved value, pops the six pushed
;          callee-saved registers and RETs back into the C caller
;          exactly as if this call had returned. The C caller detects
;          the resume via the magic word the trampoline writes.
;
; SysV ABI: rdi = u16 PM1a_CNT port
;           rsi = u16 value (SLP_TYP|SLP_EN)
;           rdx = save area base (trampoline page)
; Save area offsets used here (must match driver_acpi_power.c):
;   0x100 saved RSP   0x104 CR3   0x108 CR4   0x10C CR0

global acpi_s3_fire_stub
acpi_s3_fire_stub:
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15

    ; save RSP (the trampoline restores it verbatim)
    mov [rdx + 0x100], rsp

    ; save control registers (all < 4 GiB in this kernel)
    mov rax, cr3
    mov [rdx + 0x104], eax
    mov rax, cr4
    mov [rdx + 0x108], eax
    mov rax, cr0
    mov [rdx + 0x10C], eax

    ; the sleep-write: interrupts are already off (caller did cli)
    mov dx, di
    mov ax, si
    out dx, ax          ; SLP_EN write -> CPU stops here on success

    ; control reaches here only when the machine did NOT sleep
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    mov eax, -1
    ret
