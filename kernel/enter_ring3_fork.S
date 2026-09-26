; SPDX-License-Identifier: Apache-2.0
; Copyright 2026 Cube Studio <cubestudio@qq.com>
; Open Cube OS - WP-08a
; File: kernel/enter_ring3_fork.S
; Purpose: Enter ring 3 with a saved fork frame (RIP, RSP, RFLAGS, RAX).
;          Pure assembly to avoid compiler register clobbering.
;
; SysV ABI arguments:
;   rdi = rip   (user return address — the instruction after int 0x80)
;   rsi = rsp   (user stack pointer at the time of the fork syscall)
;   rdx = rflags (saved RFLAGS from the parent's interrupt frame)
;   rcx = rax    (fork return value: 0 for child)
;   r8  = cr3    (child address space — PML4 physical address)

bits 64
default rel

global enter_ring3_fork

section .text
align 16
enter_ring3_fork:
    ; 1. Switch to the child address space.
    mov cr3, r8

    ; 2. Set up user data segments (0x3B = GDT entry 7 | ring 3).
    mov ax, 0x3B
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; 3. Build the iretq frame on the kernel stack.
    push 0x3B           ; SS  (user data segment | ring 3)
    push rsi            ; RSP (user stack pointer)
    push rdx            ; RFLAGS (saved flags — includes IF=1)
    push 0x33           ; CS  (user code segment | ring 3)
    push rdi            ; RIP (fork return address)

    ; 4. Set RAX = return value (0 for child).
    mov rax, rcx

    ; 5. iretq — pop the frame and jump to ring 3.
    iretq
