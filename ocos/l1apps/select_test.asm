; SPDX-License-Identifier: Apache-2.0
; Copyright 2026 cubestudio-dev <cubestudio@qq.com>

; WP-08a select test: fork, child writes pipe, parent selects
bits 64
global _start
section .text
print_str:
    mov rdx, rsi
    mov rsi, rdi
    mov rdi, 1
    mov rax, 1
    int 0x80
    ret
_start:
    lea rdi, [rel start_msg]
    mov rsi, 18
    call print_str
    sub rsp, 16
    lea rdi, [rsp]
    mov rax, 20          ; pipe
    int 0x80
    test rax, rax
    jnz .fail
    mov r12d, [rsp]
    mov r13d, [rsp+4]
    add rsp, 16
    ; dup2 to 3,4
    mov rax, 51
    mov rdi, r12
    mov rsi, 3
    int 0x80
    mov rax, 51
    mov rdi, r13
    mov rsi, 4
    int 0x80
    ; fork
    mov rax, 10
    int 0x80
    test rax, rax
    jz .child
    ; Parent
    lea rdi, [rel select_msg]
    mov rsi, 29
    call print_str
    sub rsp, 128
    lea rdi, [rsp]
    xor rax, rax
    mov rcx, 16
    rep stosq
    mov rax, 3
    bts [rsp], rax
    mov rdi, 4           ; nfds
    lea rsi, [rsp]
    mov rdx, 500         ; timeout ms
    mov rax, 80          ; select
    int 0x80
    test rax, rax
    jle .no_ready
    mov rax, 3
    bt [rsp], rax
    jc .ready
    jmp .no_ready
.ready:
    add rsp, 128
    lea rdi, [rel ready_msg]
    mov rsi, 34
    call print_str
    mov rax, 12
    xor rdi, rdi
    xor rsi, rsi
    int 0x80
    lea rdi, [rel pass_msg]
    mov rsi, 17
    call print_str
    mov rax, 16
    xor rdi, rdi
    int 0x80
.no_ready:
    add rsp, 128
    lea rdi, [rel no_ready_msg]
    mov rsi, 25
    call print_str
    jmp .fail2
.child:
    mov rax, 1
    mov rdi, 4
    lea rsi, [rel data_a]
    mov rdx, 1
    int 0x80
    mov rax, 16
    xor rdi, rdi
    int 0x80
.fail:
    add rsp, 16
.fail2:
    lea rdi, [rel fail_msg]
    mov rsi, 17
    call print_str
    mov rax, 16
    mov rdi, 1
    int 0x80
section .rodata
start_msg:    db "select_test: start", 10
select_msg:   db "select_test: parent selecting", 10
ready_msg:    db "select_test: pipe ready (PASS)", 10
no_ready_msg: db "select_test: no fd ready", 10
data_a:       db "A"
pass_msg:     db "select_test: PASS", 10
fail_msg:     db "select_test: FAIL", 10
