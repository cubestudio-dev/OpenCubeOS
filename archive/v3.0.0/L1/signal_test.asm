; SPDX-License-Identifier: Apache-2.0
; Copyright 2026 cubestudio-dev <cubestudio@qq.com>

; WP-08a signal test: fork, child sends SIGUSR1 to parent
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
    mov rax, 40          ; signal(SIGUSR1=10, handler)
    mov rdi, 10
    lea rsi, [rel handler]
    int 0x80
    lea rdi, [rel reg_msg]
    mov rsi, 32
    call print_str
    mov rax, 10          ; fork
    int 0x80
    test rax, rax
    jz .child
    ; Parent
    lea rdi, [rel wait_msg]
    mov rsi, 29
    call print_str
    mov rax, 12          ; wait
    xor rdi, rdi
    xor rsi, rsi
    int 0x80
    lea rdi, [rel pass_msg]
    mov rsi, 17
    call print_str
    mov rax, 16
    xor rdi, rdi
    int 0x80
.child:
    lea rdi, [rel send_msg]
    mov rsi, 35
    call print_str
    mov rax, 15          ; getppid
    int 0x80
    mov r12, rax
    mov rax, 13          ; kill(ppid, SIGUSR1)
    mov rdi, r12
    mov rsi, 10
    int 0x80
    mov rax, 16
    xor rdi, rdi
    int 0x80
handler:
    lea rsi, [rel caught_msg]
    mov rdx, 28
    mov rdi, 1
    mov rax, 1
    int 0x80
    mov rax, 42          ; sigreturn
    int 0x80
    ret
section .rodata
start_msg:  db "signal_test: start", 10
reg_msg:    db "signal_test: handler registered", 10
wait_msg:   db "signal_test: parent waiting", 10
send_msg:   db "signal_test: child sending SIGUSR1", 10
caught_msg: db "signal_test: SIGNAL CAUGHT", 10
pass_msg:   db "signal_test: PASS", 10
