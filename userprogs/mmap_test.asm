; SPDX-License-Identifier: Apache-2.0
; Copyright 2026 Cube Studio <cubestudio@qq.com>

; WP-08a mmap test
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
    mov rax, 30          ; mmap
    xor rdi, rdi
    mov rsi, 4096
    mov rdx, 3
    int 0x80
    test rax, rax
    jz .fail
    mov r12, rax
    mov byte [r12], 'M'
    mov byte [r12+1], 'M'
    mov byte [r12+2], 'A'
    mov byte [r12+3], 'P'
    mov byte [r12+4], '_'
    mov byte [r12+5], 'O'
    mov byte [r12+6], 'K'
    mov byte [r12+7], '!'
    lea rdi, [rel wrote_msg]
    mov rsi, 23
    call print_str
    mov rsi, r12
    mov rdx, 8
    mov rdi, 1
    mov rax, 1
    int 0x80
    lea rdi, [rel nl]
    mov rsi, 1
    call print_str
    mov rax, 31          ; munmap
    mov rdi, r12
    mov rsi, 4096
    int 0x80
    mov rax, 30          ; mmap again
    xor rdi, rdi
    mov rsi, 4096
    mov rdx, 3
    int 0x80
    test rax, rax
    jz .fail
    mov r12, rax
    mov byte [r12], 'X'
    mov rax, 32          ; mprotect read-only
    mov rdi, r12
    mov rsi, 4096
    mov rdx, 1
    int 0x80
    lea rdi, [rel mprotect_msg]
    mov rsi, 26
    call print_str
    lea rdi, [rel pass_msg]
    mov rsi, 15
    call print_str
    mov rax, 16
    xor rdi, rdi
    int 0x80
.fail:
    lea rdi, [rel fail_msg]
    mov rsi, 15
    call print_str
    mov rax, 16
    mov rdi, 1
    int 0x80
section .rodata
start_msg:    db "mmap_test: start", 10
wrote_msg:    db "mmap_test: wrote to mmap", 10
mprotect_msg: db "mmap_test: mprotect done", 10
nl:           db 10
pass_msg:     db "mmap_test: PASS", 10
fail_msg:     db "mmap_test: FAIL", 10
