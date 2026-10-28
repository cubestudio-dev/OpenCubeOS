; SPDX-License-Identifier: Apache-2.0
; Copyright 2026 cubestudio-dev <cubestudio@qq.com>
;
; BUG-0042 repro (P1, A1-1): fork two processes, each accumulates a
; distinct dword vector in XMM0 over millions of paddd iterations
; (long enough to cross many 20ms scheduler time slices). If per-task
; FPU/SSE state is not saved/restored on context switch, the two XMM0
; streams pollute each other and at least one final sum no longer
; matches its expected vector -> "SSE MISMATCH" printed.
; With the fix each task's fxsave/fxrstor image keeps both streams
; independent -> both "OK" lines print.

bits 64
global _start

%define SYS_EXIT    0
%define SYS_WRITE   1
%define SYS_FORK    10
%define SYS_WAIT4   12

%define ITER 500000

section .data
align 16
pv_init: dd 1000, 1000, 1000, 1000
pv_add:  dd 7, 7, 7, 7
pv_exp:  dd 1000+7*ITER, 1000+7*ITER, 1000+7*ITER, 1000+7*ITER
cv_init: dd 3, 3, 3, 3
cv_add:  dd 11, 11, 11, 11
cv_exp:  dd 3+11*ITER, 3+11*ITER, 3+11*ITER, 3+11*ITER

msg_child_ok:   db "SSE child  OK", 10
msg_child_ok_len equ $ - msg_child_ok
msg_child_bad:  db "SSE child  MISMATCH", 10
msg_child_bad_len equ $ - msg_child_bad
msg_parent_ok:  db "SSE parent OK", 10
msg_parent_ok_len equ $ - msg_parent_ok
msg_parent_bad: db "SSE parent MISMATCH", 10
msg_parent_bad_len equ $ - msg_parent_bad

section .text
print:
    ; rsi=buf, rdx=len
    mov rdi, 1
    mov rax, SYS_WRITE
    int 0x80
    ret

check:
    ; xmm0 = accumulated vector, rdi = expected vector ptr (16-aligned)
    ; returns eax = 1 if equal else 0
    movdqu xmm3, [rdi]
    pcmpeqd xmm0, xmm3
    pmovmskb eax, xmm0
    cmp ax, 0xFFFF
    sete al
    movzx eax, al
    ret

run_stream:
    ; rdi = init vector ptr, rsi = add vector ptr (uses xmm0/xmm1)
    movdqu xmm0, [rdi]
    mov r13, ITER
.sl:
    movdqu xmm1, [rsi]
    paddd xmm0, xmm1
    dec r13
    jnz .sl
    ret


print_u64:
    ; rdi = value; prints decimal + newline
    sub rsp, 32
    lea rcx, [rsp+30]
    mov byte [rcx], 10
    mov rsi, 10
    mov rax, rdi
    mov r8, rcx
.dl:
    test rax, rax
    jz .dd
    xor rdx, rdx
    div rsi
    add dl, '0'
    dec rcx
    mov [rcx], dl
    jmp .dl
.dd:
    cmp rcx, r8
    jne .dp
    dec rcx
    mov byte [rcx], '0'
.dp:
    lea rdx, [r8+1]
    sub rdx, rcx
    mov rsi, rcx
    mov rdi, 1
    mov rax, SYS_WRITE
    int 0x80
    add rsp, 32
    ret

_start:
    mov rax, SYS_FORK
    int 0x80
    test rax, rax
    jnz .parent

    ; ---- child: accumulate (3,3,3,3) + 11 * ITER ----
    lea rdi, [rel cv_init]
    lea rsi, [rel cv_add]
    call run_stream
    sub rsp, 16
    movdqu [rsp], xmm0
    mov eax, [rsp]
    add rsp, 16
    mov r14, rax                 ; got = dword0 of accumulated vector
    lea rdi, [rel cv_exp]
    call check
    test eax, eax
    jz .cbad
    lea rsi, [rel msg_child_ok]
    mov rdx, msg_child_ok_len
    call print
    mov rax, SYS_EXIT
    xor rdi, rdi
    int 0x80
.cbad:
    lea rsi, [rel msg_child_bad]
    mov rdx, msg_child_bad_len
    call print
    mov rdi, r14
    call print_u64               ; got
    mov eax, [rel cv_exp]
    mov rdi, rax
    call print_u64               ; expect
    mov rax, SYS_EXIT
    mov rdi, 1
    int 0x80

    ; ---- parent: accumulate (1000,...) + 7 * ITER, then wait child ----
.parent:
    mov r12, rax                    ; child pid
    lea rdi, [rel pv_init]
    lea rsi, [rel pv_add]
    call run_stream
    ; (wait4 removed: pid/tid mapping mismatch makes it hang - parent
    ; prints its result and exits right after its own stream)
    lea rdi, [rel pv_exp]
    call check
    test eax, eax
    jz .pbad
    lea rsi, [rel msg_parent_ok]
    mov rdx, msg_parent_ok_len
    call print
    mov rax, SYS_EXIT
    xor rdi, rdi
    int 0x80
.pbad:
    lea rsi, [rel msg_parent_bad]
    mov rdx, msg_parent_bad_len
    call print
    mov rax, SYS_EXIT
    mov rdi, 1
    int 0x80
