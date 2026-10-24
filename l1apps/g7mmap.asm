; SPDX-License-Identifier: Apache-2.0
; Copyright 2026 cubestudio-dev <cubestudio@qq.com>

; WP-10-AUDIT_P2-fix3 G7 mmap regression (BUG-0281 / A2-9).
; Double-sided: legal paths must PASS, hostile inputs must be rejected
; with -1. Prot and addr are honoured, the mmap/munmap/mprotect window
; is unified at [USER_MMAP_BASE, USER_MMAP_WINDOW_END), and a PROT_READ
; page really is write-protected (T11 faults and the process is killed;
; on the pre-fix kernel the write silently succeeds).
bits 64
global _start

%define SYS_WRITE  1
%define SYS_EXIT   16
%define SYS_MMAP   30
%define SYS_MUNMAP 31
%define SYS_MPROT  32

section .text
print_str:
    mov rdx, rsi
    mov rsi, rdi
    mov rdi, 1
    mov rax, SYS_WRITE
    int 0x80
    ret

%macro PRINT 2
    lea rdi, [rel %1]
    mov rsi, %2
    call print_str
%endmacro

_start:
    PRINT start_msg, start_msg_len

    ; ---- T1: legal anonymous RW mmap (bump path), data round-trip ----
    mov rax, SYS_MMAP
    xor rdi, rdi
    mov rsi, 4096
    mov rdx, 3
    int 0x80
    cmp rax, -1
    je .t1_fail
    test rax, rax
    jz .t1_fail
    mov r12, rax
    mov byte [r12], 'G'
    mov byte [r12+1], '7'
    cmp byte [r12], 'G'
    jne .t1_fail
    cmp byte [r12+1], '7'
    jne .t1_fail
    PRINT pass_t1, pass_t1_len
    jmp .t2
.t1_fail:
    PRINT fail_t1, fail_t1_len

    ; ---- T2: addr hint honoured (page-aligned, inside window) ----
.t2:
    mov rax, SYS_MMAP
    mov rdi, 0x3C010000
    mov rsi, 8192
    mov rdx, 3
    int 0x80
    mov r13, rax
    cmp r13, 0x3C010000
    jne .t2_fail
    PRINT pass_t2, pass_t2_len
    jmp .t3
.t2_fail:
    PRINT fail_t2, fail_t2_len

    ; ---- T3: hostile hint (kernel region) must be rejected ----
.t3:
    mov rax, SYS_MMAP
    mov rdi, 0x100000
    mov rsi, 4096
    mov rdx, 3
    int 0x80
    cmp rax, -1
    jne .t3_fail
    PRINT pass_t3, pass_t3_len
    jmp .t4
.t3_fail:
    PRINT fail_t3, fail_t3_len

    ; ---- T4: hostile prot (0x8, undefined bit) must be rejected ----
.t4:
    mov rax, SYS_MMAP
    xor rdi, rdi
    mov rsi, 4096
    mov rdx, 8
    int 0x80
    cmp rax, -1
    jne .t4_fail
    PRINT pass_t4, pass_t4_len
    jmp .t5
.t4_fail:
    PRINT fail_t4, fail_t4_len

    ; ---- T5: length beyond the mmap window must be rejected ----
.t5:
    mov rax, SYS_MMAP
    xor rdi, rdi
    mov rsi, 0x3FF1000
    mov rdx, 3
    int 0x80
    cmp rax, -1
    je .t5_pass
    mov r14, rax                ; pre-fix kernel: allocation happened,
    mov rax, SYS_MUNMAP         ; roll it back so the box stays healthy
    mov rdi, r14
    mov rsi, 0x3FF1000
    int 0x80
    PRINT fail_t5, fail_t5_len
    jmp .t6
.t5_pass:
    PRINT pass_t5, pass_t5_len

    ; ---- T6: munmap on a kernel address must be rejected ----
.t6:
    mov rax, SYS_MUNMAP
    mov rdi, 0x100000
    mov rsi, 0x2000
    int 0x80
    cmp rax, -1
    jne .t6_fail
    PRINT pass_t6, pass_t6_len
    jmp .t7
.t6_fail:
    PRINT fail_t6, fail_t6_len

    ; ---- T7: legal munmap of the T2 mapping ----
.t7:
    mov rax, SYS_MUNMAP
    mov rdi, r13
    mov rsi, 8192
    int 0x80
    cmp rax, -1
    je .t7_fail
    PRINT pass_t7, pass_t7_len
    jmp .t8
.t7_fail:
    PRINT fail_t7, fail_t7_len

    ; ---- T8: mprotect legal R/W toggle on the T1 page ----
.t8:
    mov rax, SYS_MPROT
    mov rdi, r12
    mov rsi, 4096
    mov rdx, 1
    int 0x80
    cmp rax, -1
    je .t8_fail
    mov rax, SYS_MPROT
    mov rdi, r12
    mov rsi, 4096
    mov rdx, 3
    int 0x80
    cmp rax, -1
    je .t8_fail
    mov byte [r12], 'X'
    cmp byte [r12], 'X'
    jne .t8_fail
    PRINT pass_t8, pass_t8_len
    jmp .t9
.t8_fail:
    PRINT fail_t8, fail_t8_len

    ; ---- T9: hostile prot to mprotect must be rejected ----
.t9:
    mov rax, SYS_MPROT
    mov rdi, r12
    mov rsi, 4096
    mov rdx, 9
    int 0x80
    cmp rax, -1
    jne .t9_fail
    PRINT pass_t9, pass_t9_len
    jmp .t10
.t9_fail:
    PRINT fail_t9, fail_t9_len

    ; ---- T10: munmap beyond the unified window must be rejected ----
    ; (0x44000000 was inside the OLD munmap limit of 0x4C000000 while
    ; mmap could also grow past it - the inconsistency BUG-0281 is
    ; about. The unified window ends at 0x3FFF0000.)
.t10:
    mov rax, SYS_MUNMAP
    mov rdi, 0x44000000
    mov rsi, 0x1000
    int 0x80
    cmp rax, -1
    jne .t10_fail
    PRINT pass_t10, pass_t10_len
    jmp .t11
.t10_fail:
    PRINT fail_t10, fail_t10_len

    ; ---- T11: PROT_READ page must actually reject writes ----
    ; Pre-fix kernel: prot was ignored, the write below SUCCEEDS and the
    ; program prints its own FAIL line. Fixed kernel: the write raises
    ; #PF, the kernel kills this process ("killed by exception
    ; (vector=14)") and the shell survives.
.t11:
    mov rax, SYS_MMAP
    xor rdi, rdi
    mov rsi, 4096
    mov rdx, 1
    int 0x80
    cmp rax, -1
    je .t11_fail
    test rax, rax
    jz .t11_fail
    mov r15, rax
    PRINT pf_armed_msg, pf_armed_msg_len
    mov byte [r15], 1           ; must #PF (read-only page)
.t11_fail:
    PRINT fail_t11, fail_t11_len

    PRINT done_msg, done_msg_len
    mov rax, SYS_EXIT
    xor rdi, rdi
    int 0x80

section .rodata
start_msg:    db "G7MMAP: start", 10
start_msg_len equ $ - start_msg
pass_t1:      db "G7MMAP PASS T1 (bump RW map + round-trip)", 10
pass_t1_len   equ $ - pass_t1
fail_t1:      db "G7MMAP FAIL T1 (bump RW map)", 10
fail_t1_len   equ $ - fail_t1
pass_t2:      db "G7MMAP PASS T2 (addr hint honoured)", 10
pass_t2_len   equ $ - pass_t2
fail_t2:      db "G7MMAP FAIL T2 (addr hint ignored/rejected)", 10
fail_t2_len   equ $ - fail_t2
pass_t3:      db "G7MMAP PASS T3 (kernel-addr hint rejected)", 10
pass_t3_len   equ $ - pass_t3
fail_t3:      db "G7MMAP FAIL T3 (kernel-addr hint accepted)", 10
fail_t3_len   equ $ - fail_t3
pass_t4:      db "G7MMAP PASS T4 (prot 0x8 rejected)", 10
pass_t4_len   equ $ - pass_t4
fail_t4:      db "G7MMAP FAIL T4 (prot 0x8 accepted)", 10
fail_t4_len   equ $ - fail_t4
pass_t5:      db "G7MMAP PASS T5 (over-window length rejected)", 10
pass_t5_len   equ $ - pass_t5
fail_t5:      db "G7MMAP FAIL T5 (over-window length accepted)", 10
fail_t5_len   equ $ - fail_t5
pass_t6:      db "G7MMAP PASS T6 (munmap kernel addr rejected)", 10
pass_t6_len   equ $ - pass_t6
fail_t6:      db "G7MMAP FAIL T6 (munmap kernel addr accepted)", 10
fail_t6_len   equ $ - fail_t6
pass_t7:      db "G7MMAP PASS T7 (munmap legal range)", 10
pass_t7_len   equ $ - pass_t7
fail_t7:      db "G7MMAP FAIL T7 (munmap legal range)", 10
fail_t7_len   equ $ - fail_t7
pass_t8:      db "G7MMAP PASS T8 (mprotect RW toggle + write)", 10
pass_t8_len   equ $ - pass_t8
fail_t8:      db "G7MMAP FAIL T8 (mprotect legal toggle)", 10
fail_t8_len   equ $ - fail_t8
pass_t9:      db "G7MMAP PASS T9 (mprotect prot 0x9 rejected)", 10
pass_t9_len   equ $ - pass_t9
fail_t9:      db "G7MMAP FAIL T9 (mprotect prot 0x9 accepted)", 10
fail_t9_len   equ $ - fail_t9
pass_t10:     db "G7MMAP PASS T10 (munmap beyond window rejected)", 10
pass_t10_len  equ $ - pass_t10
fail_t10:     db "G7MMAP FAIL T10 (munmap beyond window accepted)", 10
fail_t10_len  equ $ - fail_t10
pf_armed_msg: db "G7MMAP T11: PF probe armed (write to R-only page)", 10
pf_armed_msg_len equ $ - pf_armed_msg
fail_t11:     db "G7MMAP FAIL T11 (write to R-only page succeeded)", 10
fail_t11_len  equ $ - fail_t11
done_msg:     db "G7MMAP DONE", 10
done_msg_len  equ $ - done_msg
