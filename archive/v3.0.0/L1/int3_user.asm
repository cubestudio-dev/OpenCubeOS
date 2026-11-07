; SPDX-License-Identifier: Apache-2.0
; Copyright 2026 cubestudio-dev <cubestudio@qq.com>
;
; int3_user - raise #BP (int3) from ring 3. Used by the BUG-0139 regression
; (IST2 kill path): the process is killed by the breakpoint exception and
; the shell must survive; run it repeatedly to stress the IST2 stack reuse.

bits 64
global _start
section .text
_start:
    mov rax, 1           ; sys_write
    mov rdi, 1           ; fd = stdout
    lea rsi, [rel msg]
    mov rdx, len
    int 0x80
    int3                 ; #BP from ring 3 -> kill via IST2 path (BUG-0139)
    mov rax, 16          ; sys_exit2 - must never be reached
    xor rdi, rdi
    int 0x80
section .rodata
msg: db "int3_user: about to int3", 10
len equ $ - msg
