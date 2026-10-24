; SPDX-License-Identifier: Apache-2.0
; Copyright 2026 cubestudio-dev <cubestudio@qq.com>
;
; priv_test - ring-3 privilege escalation must be contained.  The task
; executes the privileged instruction `out %al, $0x70` (the CMOS port)
; from ring 3.  The CPU raises #GP(0); the kernel kills the process and
; the shell (or ush) MUST survive.  Never reaches the second print.

bits 64
global _start
section .text
_start:
    mov rax, 1              ; sys_write
    mov rdi, 1              ; fd = stdout
    lea rsi, [rel msg_try]
    mov rdx, msg_try.len
    int 0x80

    mov al, 0x0C            ; CMOS address register value
    out 0x70, al            ; PRIVILEGED in ring 3 -> #GP(0)

    ; --- must never get here: the kernel kills us on the #GP ---
    mov rax, 1
    mov rdi, 1
    lea rsi, [rel msg_bad]
    mov rdx, msg_bad.len
    int 0x80
    mov rax, 16             ; sys_exit2(0) - would be a WRONG pass
    xor rdi, rdi
    int 0x80

section .rodata
msg_try: db "priv_test: attempting out 0x70 from ring 3", 10
    .len equ $ - msg_try
msg_bad: db "priv_test: BUG - privileged instruction executed!", 10
    .len equ $ - msg_bad
