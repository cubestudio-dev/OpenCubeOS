; SPDX-License-Identifier: Apache-2.0
; Copyright 2026 cubestudio-dev <cubestudio@qq.com>

bits 64
global _start
section .text
_start:
    mov rax, 1
    mov rdi, 1
    lea rsi, [rel msg1]
    mov rdx, 31
    int 0x80
    mov rsi, 0xFD000000
    mov rax, [rsi]
    mov rax, 16
    xor rdi, rdi
    int 0x80
section .rodata
msg1: db "badapp: trying kernel access", 10
