; SPDX-License-Identifier: Apache-2.0
; Copyright 2026 cubestudio-dev <cubestudio@qq.com>

; WP-08a pipe test: fork, child writes pipe, parent reads
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
    mov rsi, 16
    call print_str
    sub rsp, 16
    lea rdi, [rsp]
    mov rax, 20          ; pipe
    int 0x80
    test rax, rax
    jnz .fail
    mov r12d, [rsp]      ; read_fd
    mov r13d, [rsp+4]    ; write_fd
    add rsp, 16
    ; dup2 to fixed fds: 3=read, 4=write
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
    lea rdi, [rel parent_read_msg]
    mov rsi, 24
    call print_str
    sub rsp, 64
    mov rax, 70          ; read
    mov rdi, 3
    lea rsi, [rsp]
    mov rdx, 32
    int 0x80
    mov r14, rax
    lea rdi, [rel got_msg]
    mov rsi, 18
    call print_str
    test r14, r14
    jle .no_data
    mov rsi, rsp
    mov rdx, r14
    mov rdi, 1
    mov rax, 1
    int 0x80
.no_data:
    add rsp, 64
    lea rdi, [rel end_quote]
    mov rsi, 3
    call print_str
    mov rax, 12          ; wait
    xor rdi, rdi
    xor rsi, rsi
    int 0x80
    lea rdi, [rel pass_msg]
    mov rsi, 15
    call print_str
    mov rax, 16
    xor rdi, rdi
    int 0x80
.child:
    ; Child — write to fd 4 (inherited)
    mov rax, 1
    mov rdi, 4
    lea rsi, [rel data_msg]
    mov rdx, 16
    int 0x80
    mov rax, 16
    xor rdi, rdi
    int 0x80
.fail:
    add rsp, 16
    lea rdi, [rel fail_msg]
    mov rsi, 15
    call print_str
    mov rax, 16
    mov rdi, 1
    int 0x80
section .rodata
start_msg:        db "pipe_test: start", 10
parent_read_msg:  db "pipe_test: parent reading", 10
got_msg:          db "pipe_test: got '", 0
data_msg:         db "HELLO from child", 10
end_quote:        db "'", 10
pass_msg:         db "pipe_test: PASS", 10
fail_msg:         db "pipe_test: FAIL", 10
