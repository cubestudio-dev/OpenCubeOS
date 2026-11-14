; SPDX-License-Identifier: Apache-2.0
; Copyright 2026 cubestudio-dev <cubestudio@qq.com>

; WP-08a exec test: fork, child execs hello, parent waits
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
    mov rsi, 17          ; P4 fix: was 16 (off-by-one, missing \n)
    call print_str
    mov rax, 10          ; fork
    int 0x80
    test rax, rax
    jz .child
    ; Parent
    lea rdi, [rel wait_msg]
    mov rsi, 28          ; P4 fix: was 27
    call print_str
    mov rax, 12          ; wait
    xor rdi, rdi
    xor rsi, rsi
    int 0x80
    lea rdi, [rel pass_msg]
    mov rsi, 17          ; P4 fix: was 16
    call print_str
    mov rax, 16
    xor rdi, rdi
    int 0x80
.child:
    lea rdi, [rel exec_msg]
    mov rsi, 30          ; P4 fix: was 28
    call print_str
    mov rax, 11          ; execve
    lea rdi, [rel hello_name]
    xor rsi, rsi
    xor rdx, rdx
    int 0x80
    lea rdi, [rel fail_msg]
    mov rsi, 17          ; P4 fix: was 16
    call print_str
    mov rax, 16
    mov rdi, 1
    int 0x80
section .rodata
start_msg:  db "exec_test: start", 10
wait_msg:   db "exec_test: parent waiting", 10
exec_msg:   db "exec_test: child execing hello", 10
hello_name: db "hello", 0
pass_msg:   db "exec_test: PASS", 10
fail_msg:   db "exec_test: FAIL", 10
