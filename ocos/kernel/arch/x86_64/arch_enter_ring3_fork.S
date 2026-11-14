; SPDX-License-Identifier: Apache-2.0
; Copyright 2026 cubestudio-dev <cubestudio@qq.com>
; Open Cube OS - WP-08cd
; File: kernel/enter_ring3_fork.S
; Purpose: Enter ring 3 restoring ALL callee-saved registers.
;
; Arguments (SysV ABI):
;   rdi = frame_ptr  (pointer to fork_regs[22] in proc struct)
;   rsi = cr3         (child address space)
;
; arch_irq_frame_t layout (22 u64 fields):
;   [0]  r15  [1]  r14  [2]  r13  [3]  r12
;   [4]  r11  [5]  r10  [6]  r9   [7]  r8
;   [8]  rdi  [9]  rsi  [10] rbp  [11] rdx
;   [12] rcx  [13] rbx  [14] rax
;   [15] int_no  [16] error_code
;   [17] rip  [18] cs  [19] rflags  [20] rsp  [21] ss
;
; Byte offsets:
;   r15=0  r14=8  r13=16 r12=24 r11=32 r10=40 r9=48  r8=56
;   rdi=64 rsi=72 rbp=80 rdx=88 rcx=96 rbx=104 rax=112
;   int_no=120 error_code=128
;   rip=136 cs=144 rflags=152 rsp=160 ss=168

bits 64
default rel

global enter_ring3_fork

section .text
align 16
enter_ring3_fork:
    ; rdi = frame_ptr, rsi = cr3

    ; 1. Switch to child address space
    mov cr3, rsi

    ; 2. Set user data segments
    mov ax, 0x3B
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; 3. Build IRET frame: push SS, RSP, RFLAGS, CS, RIP (reverse order)
    ;    Correct offsets: rip=[17]=136, rflags=[19]=152, rsp=[20]=160, ss=[21]=168
    mov rax, 0x3B           ; SS
    push rax
    mov rax, [rdi + 160]    ; rsp (frame[20])
    push rax
    mov rax, [rdi + 152]    ; rflags (frame[19])
    push rax
    mov rax, 0x33           ; CS
    push rax
    mov rax, [rdi + 136]    ; rip (frame[17])
    push rax

    ; 4. Restore callee-saved GPRs from frame
    ;    rsi=[9]=72, r15=[0]=0, r14=[1]=8, r13=[2]=16, r12=[3]=24
    ;    rbp=[10]=80, rbx=[13]=104, rdi=[8]=64
    mov rsi, [rdi + 72]     ; rsi (restore FIRST, before rdi)
    mov r15, [rdi + 0]      ; r15
    mov r14, [rdi + 8]      ; r14
    mov r13, [rdi + 16]     ; r13
    mov r12, [rdi + 24]     ; r12
    mov rbp, [rdi + 80]     ; rbp
    mov rbx, [rdi + 104]    ; rbx

    ; 5. 3.0.0-WP04 G7 live-catch: restore the CALLER-saved registers
    ;    too.  The parent returns from int 0x80 through iretq with the
    ;    complete frame - rdx/rcx/r8..r11 intact; this child path
    ;    restored only callee-saved + rsi/rdi, so the forked child saw
    ;    garbage in rcx/rdx/r8-r11 while the parent kept its values
    ;    (asymmetric; every asm-era test used only low registers).
    ;    The first C caller-saved user (WP-04 `run` - argv cached in
    ;    rcx across fork) crashed on garbage.  Order matters: these
    ;    reads must run while rdi is STILL the frame pointer - the
    ;    first attempt placed the block after the rdi reload and read
    ;    user-garbage through it.  A fork child now continues with the
    ;    same register state as its parent at the fork point.
    mov r11, [rdi + 32]     ; r11 (frame[4])
    mov r10, [rdi + 40]     ; r10 (frame[5])
    mov r9,  [rdi + 48]     ; r9  (frame[6])
    mov r8,  [rdi + 56]     ; r8  (frame[7])
    mov rdx, [rdi + 88]     ; rdx (frame[11])
    mov rcx, [rdi + 96]     ; rcx (frame[12])

    ; 6. Restore rdi LAST (frame[8]=offset 64) - after this the frame
    ;    pointer is gone.
    mov rdi, [rdi + 64]     ; rdi

    ; 6. Set RAX = 0 (fork return value)
    xor rax, rax

    ; 7. IRETQ
    iretq
