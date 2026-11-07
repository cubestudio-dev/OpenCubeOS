; SPDX-License-Identifier: Apache-2.0 */
; Copyright 2026 cubestudio-dev <cubestudio@qq.com>

; WP-11-crash_recovery 4.4: ring-3 stack-overflow interception test.
;
; An unbounded recursion with a fat frame (4 KiB per call) burns the
; user stack downward. At the 64 KiB initial-stack boundary the next
; page belongs to the kernel's low-4GiB identity mapping
; (supervisor-only: P=1, USER=0) - the hardware itself raises a
; protection #PF the moment ring 3 touches it, the VMM rejects it
; (BUG-0044 (a): P=1 is never growth), the dispatcher kills the
; process and the KERNEL KEEPS RUNNING. The parent (in SYS_WAIT)
; wakes up (WP-11 fix: exception-kill now wakes the parent) and
; prints the wrap-up. This is the "recursive loop gets intercepted"
; acceptance test.
bits 64
global _start

%define SYS_EXIT   0
%define SYS_WRITE  1
%define SYS_FORK  10
%define SYS_WAIT  12

section .text
print_str:                       ; rdi=buf, rsi=len
    mov rdx, rsi
    mov rsi, rdi
    mov rdi, 1
    mov rax, SYS_WRITE
    int 0x80
    ret

; infinite recursion: 4 KiB frame per level (a fat local array).
; NOTE: the guard touch is [rsp-8] (STRICTLY below rsp) - the VMM's
; growth window only accepts faults BELOW the faulting rsp
; (in_stack_region: vaddr >= faulting_rsp is rejected), so touching
; [rsp] itself would be refused at every depth regardless of the
; floor. With [rsp-8] each level takes the LEGAL growth path until
; the 8 MiB floor (VMM_STACK_LIMIT_BYTES) rejects it - the true
; "recursion burned through the stack" detection.
recurse:
    sub rsp, 4096
    mov byte [rsp-8], 0x5A       ; touch strictly below rsp: legal growth
    call recurse
    add rsp, 4096                ; never reached
    ret

_start:
    lea rdi, [rel start_msg]
    mov rsi, start_len
    call print_str

    mov rax, SYS_FORK
    int 0x80
    test rax, rax
    jz .child

    ; ---- parent: wait for the doomed child, then report ----
    lea rdi, [rel parent_msg]
    mov rsi, parent_len
    call print_str
    mov rax, SYS_WAIT
    xor rdi, rdi
    xor rsi, rsi
    int 0x80
    lea rdi, [rel survived_msg]
    mov rsi, survived_len
    call print_str
    mov rax, SYS_EXIT
    xor rdi, rdi
    int 0x80

    ; ---- child: burn the stack ----
.child:
    lea rdi, [rel burning_msg]
    mov rsi, burning_len
    call print_str
    call recurse
    ; only reached if the growth floor failed to stop us
    lea rdi, [rel leaked_msg]
    mov rsi, leaked_len
    call print_str
    mov rax, SYS_EXIT
    xor rdi, rdi
    int 0x80

section .rodata
start_msg:     db "stackovf: fork+recursion (4 KiB frames, no handler)", 10
start_len      equ $ - start_msg
parent_msg:    db "stackovf: parent waiting for child", 10
parent_len     equ $ - parent_msg
burning_msg:   db "stackovf: child recursing - expect kill at the boundary", 10
burning_len    equ $ - burning_msg
survived_msg:  db "STACKOVF-TEST DONE (child killed, kernel alive)", 10
survived_len   equ $ - survived_msg
leaked_msg:    db "STACKOVF LEAKED (boundary failed!)", 10
leaked_len     equ $ - leaked_msg
