; SPDX-License-Identifier: Apache-2.0
; Copyright 2026 cubestudio-dev <cubestudio@qq.com>

; WP-10-AUDIT_P2-fix3 G6 regression (BUG-0277a): exec must reset the
; signal state (POSIX).  This program registers a SIGUSR1 handler, then
; forks; the child execve()s g6_exec_b, which immediately raises SIGUSR1
; to itself WITHOUT registering any handler.
;
;   fixed:   handlers were reset by exec -> default action -> the child
;            is killed by the signal with exit code 128+10 = 138 ->
;            parent wait4 collects 138 -> PASS.
;   bug:     the stale handler VA from THIS program survives exec ->
;            the signal is "handled" by wild code in the fresh image ->
;            the child dies some other way (or survives) -> exit code
;            is anything but 138 -> FAIL.
;
; The assertion is exact: exit code 138 can only be written by the
; default-action path, which only runs when the handler table is empty.
bits 64
global _start

%define SYS_WRITE   1
%define SYS_FORK    10
%define SYS_EXECVE  11
%define SYS_WAIT4   12
%define SYS_SIGNAL  40
%define SYS_EXIT2   16
%define SYS_SIGRETURN 42

section .text
print_str:
    mov rdx, rsi
    mov rsi, rdi
    mov rdi, 1
    mov rax, SYS_WRITE
    int 0x80
    ret

print_u64:
    sub rsp, 24
    lea rcx, [rsp+20]
    mov byte [rcx], 10
    inc rcx
    mov rsi, 10
    mov rax, rdi
    test rax, rax
    jnz .pu_loop
    dec rcx
    mov byte [rcx], '0'
    jmp .pu_print
.pu_loop:
    test rax, rax
    jz .pu_print
    xor rdx, rdx
    div rsi
    add dl, '0'
    dec rcx
    mov [rcx], dl
    jmp .pu_loop
.pu_print:
    lea rdx, [rsp+21]
    sub rdx, rcx
    mov rsi, rcx
    mov rdi, 1
    mov rax, SYS_WRITE
    int 0x80
    add rsp, 24
    ret

_start:
    lea rdi, [rel start_msg]
    mov esi, start_len
    call print_str
    ; register a handler that must NOT survive the exec below
    mov rax, SYS_SIGNAL
    mov rdi, 10                  ; SIGUSR1
    lea rsi, [rel stale_handler]
    int 0x80
    mov rax, SYS_FORK
    int 0x80
    test rax, rax
    jnz parent
child:
    lea rdi, [rel exec_msg]
    mov esi, exec_len
    call print_str
    mov rax, SYS_EXECVE
    lea rdi, [rel prog_b]
    xor rsi, rsi
    xor rdx, rdx
    int 0x80
    lea rdi, [rel execfail_msg]
    mov esi, execfail_len
    call print_str
    mov rax, SYS_EXIT2
    mov rdi, 3
    int 0x80
parent:
    sub rsp, 16
    mov rax, SYS_WAIT4
    xor rdi, rdi                 ; any child
    mov rsi, rsp                 ; status
    xor rdx, rdx
    int 0x80
    mov eax, [rsp]
    add rsp, 16
    mov r12d, eax                ; child exit status
    lea rdi, [rel status_msg]
    mov esi, status_len
    call print_str
    mov rdi, r12
    call print_u64
    cmp r12d, 138                ; 128 + SIGUSR1: default action ran
    jne fail
    lea rdi, [rel pass_msg]
    mov esi, pass_len
    call print_str
    mov rax, SYS_EXIT2
    xor rdi, rdi
    int 0x80
fail:
    lea rdi, [rel fail_msg]
    mov esi, fail_len
    call print_str
    mov rax, SYS_EXIT2
    mov rdi, 1
    int 0x80

; never expected to run after the exec (this is the stale entry)
stale_handler:
    mov rax, SYS_SIGRETURN
    int 0x80
    ret

section .rodata
start_msg:    db "g6_exec_a: start", 10
start_len     equ $ - start_msg
exec_msg:     db "g6_exec_a: child execing g6_exec_b", 10
exec_len      equ $ - exec_msg
execfail_msg: db "g6_exec_a: execve failed", 10
execfail_len  equ $ - execfail_msg
status_msg:   db "g6_exec_a: child status = "
status_len    equ $ - status_msg
pass_msg:     db "g6_exec_a: PASS", 10
pass_len      equ $ - pass_msg
fail_msg:     db "g6_exec_a: FAIL", 10
fail_len      equ $ - fail_msg
prog_b:       db "g6_exec_b", 0
