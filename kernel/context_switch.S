; SPDX-License-Identifier: Apache-2.0
; Copyright 2026 cubestudio-dev <cubestudio@qq.com>
; Open Cube OS - WP-04
; File: kernel/context_switch.S
; Purpose: Context switch routine.
;
; oc_context_switch(old_task, new_task):
;   rdi = pointer to old task_t
;   rsi = pointer to new task_t
;
; The task_t struct has a register save area. We save the current
; CPU state into old_task's save area, then load new_task's save area.
;
; The save area offsets are computed at compile time using the struct
; layout. To avoid manual offset bugs, we use NASM's %define with
; the actual offsets from the C struct.
;
; Struct layout (task_t):
;   int  tid            offset 0
;   char name[32]       offset 4
;   int  state          offset 36
;   int  priority       offset 40
;   u64  cpu_time_ticks offset 48 (8-byte aligned)
;   u64  switch_count   offset 56
;   u64  ticks_remaining offset 64
;   u64  r15            offset 72
;   u64  r14            offset 80
;   ... (17 u64 fields total: r15..rax + rip + cs + rflags + rsp + ss)
;   u64  rax            offset 184
;   u64  rip            offset 192
;   u64  cs             offset 200
;   u64  rflags         offset 208
;   u64  rsp            offset 216
;   u64  ss             offset 224
;   u64* stack_base     offset 232
;   u64  stack_size     offset 240
;   u64  cr3            offset 248
;   void (*entry)()     offset 256
;   void *arg           offset 264
;   int  in_use         offset 272

bits 64
default rel

global oc_context_switch

%define OFF_R15    72
%define OFF_R14    80
%define OFF_R13    88
%define OFF_R12    96
%define OFF_R11   104
%define OFF_R10   112
%define OFF_R9    120
%define OFF_R8    128
%define OFF_RDI   136
%define OFF_RSI   144
%define OFF_RBP   152
%define OFF_RDX   160
%define OFF_RCX   168
%define OFF_RBX   176
%define OFF_RAX   184
%define OFF_RIP   192
%define OFF_CS    200
%define OFF_RFLAGS 208
%define OFF_RSP   216
%define OFF_SS    224
%define OFF_CR3   248

section .text
align 16
oc_context_switch:
    ; rdi = old task, rsi = new task
    push rsi              ; save new_task pointer on stack (we'll need it after restoring regs)

    ; --- Save current state to old task ---
    mov [rdi + OFF_R15], r15
    mov [rdi + OFF_R14], r14
    mov [rdi + OFF_R13], r13
    mov [rdi + OFF_R12], r12
    mov [rdi + OFF_R11], r11
    mov [rdi + OFF_R10], r10
    mov [rdi + OFF_R9],  r9
    mov [rdi + OFF_R8],  r8
    mov [rdi + OFF_RBP], rbp
    mov [rdi + OFF_RDX], rdx
    mov [rdi + OFF_RCX], rcx
    mov [rdi + OFF_RBX], rbx
    mov [rdi + OFF_RAX], rax
    mov [rdi + OFF_RDI], rdi    ; save old rdi (the old_task pointer itself)
    mov [rdi + OFF_RSI], rsi    ; save old rsi (before we pushed it)

    ; Save RFLAGS
    pushfq
    pop rax
    mov [rdi + OFF_RFLAGS], rax

    ; Save CS and SS
    mov ax, cs
    mov [rdi + OFF_CS], rax
    mov ax, ss
    mov [rdi + OFF_SS], rax

    ; Save RIP: the return address is at [rsp+8] (we pushed rsi, then
    ; the call pushed the return address before that).
    ; Stack layout: [return_addr][pushed_rsi]
    ; rsp -> pushed_rsi, rsp+8 -> return_addr
    mov rax, [rsp + 8]
    mov [rdi + OFF_RIP], rax

    ; Save RSP: the stack pointer that the new task should restore to
    ; is the value AFTER popping the return address (i.e., rsp + 16,
    ; which is the rsp before the call + before the push rsi).
    ; When restoring: we set rsp to this value, push the saved RIP, then ret.
    lea rax, [rsp + 16]
    mov [rdi + OFF_RSP], rax

    ; --- Switch CR3 if different (skip if new cr3=0, meaning kernel space) ---
    mov rdx, [rsi + OFF_CR3]
    test rdx, rdx            ; is cr3 == 0?
    jz .no_cr3_switch        ; yes: keep current CR3 (kernel identity mapping)
    mov rax, cr3
    cmp rax, rdx
    je .no_cr3_switch
    mov cr3, rdx
.no_cr3_switch:

    ; --- Load new task's state ---
    pop rsi               ; restore new_task pointer into rsi
    mov rdi, [rsi + OFF_RDI]   ; load new rdi (we need rsi first)

    ; Load new RSP
    mov rsp, [rsi + OFF_RSP]

    ; Push the saved RIP so ret will jump to it
    push qword [rsi + OFF_RIP]

    ; Load remaining registers
    mov r15, [rsi + OFF_R15]
    mov r14, [rsi + OFF_R14]
    mov r13, [rsi + OFF_R13]
    mov r12, [rsi + OFF_R12]
    mov r11, [rsi + OFF_R11]
    mov r10, [rsi + OFF_R10]
    mov r9,  [rsi + OFF_R9]
    mov r8,  [rsi + OFF_R8]
    mov rbp, [rsi + OFF_RBP]
    mov rdx, [rsi + OFF_RDX]
    mov rcx, [rsi + OFF_RCX]
    mov rbx, [rsi + OFF_RBX]
    mov rax, [rsi + OFF_RAX]

    ; Load RSI last (we were using it)
    mov rsi, [rsi + OFF_RSI]

    ; Return to the new task's saved RIP
    ret
