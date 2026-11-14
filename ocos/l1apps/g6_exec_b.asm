; SPDX-License-Identifier: Apache-2.0
; Copyright 2026 cubestudio-dev <cubestudio@qq.com>

; WP-10-AUDIT_P2-fix3 G6 regression (BUG-0277a): exec must reset the
; signal state.  This program is execve()d by g6_exec_a.  It registers
; NO handler and raises SIGUSR1 to itself: with the exec fix the default
; action kills it here with exit code 128+10 = 138, so nothing after the
; kill syscall ever prints.  If this program is run directly (run
; g6_exec_b) it must die the same way.
bits 64
global _start

%define SYS_WRITE   1
%define SYS_KILL    13
%define SYS_GETPID  14
%define SYS_EXIT2   16

section .text
print_str:
    mov rdx, rsi
    mov rsi, rdi
    mov rdi, 1
    mov rax, SYS_WRITE
    int 0x80
    ret

_start:
    lea rdi, [rel alive_msg]
    mov esi, alive_len
    call print_str
    mov rax, SYS_GETPID
    int 0x80
    mov rdi, rax
    mov rax, SYS_KILL
    mov rsi, 10                  ; SIGUSR1, no handler -> default action
    int 0x80
    ; reaching this point means a signal handler survived the exec
    ; (BUG-0277 not fixed) - report it loudly and exit non-138
    lea rdi, [rel survived_msg]
    mov esi, survived_len
    call print_str
    mov rax, SYS_EXIT2
    mov rdi, 7
    int 0x80

section .rodata
alive_msg:    db "g6_exec_b: alive, raising SIGUSR1 with no handler", 10
alive_len     equ $ - alive_msg
survived_msg: db "g6_exec_b: SURVIVED - signal state was not reset by exec", 10
survived_len  equ $ - survived_msg
