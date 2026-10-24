; SPDX-License-Identifier: Apache-2.0
; Copyright 2026 cubestudio-dev <cubestudio@qq.com>
;
; BUG-0044 repro (P1, A1-3): page-fault handler semantics.
;
; Stage A (child 1): mprotect the already-mapped stack-bottom pages to
;   read-only, then write them. A write fault there is a PROTECTION
;   violation (P=1) whose address lands inside the stack-growth window.
;   Pre-fix: the handler discarded error_code, took the stack-growth
;   path, swapped in a fresh zero page (old page data dropped, old frame
;   leaked) and resumed - so all 16 writes "succeed" and the program
;   prints "PF-A LEAKED". Post-fix: P=1 is rejected, the process is
;   killed by the dispatcher.
;
; Stage B (child 2): move RSP 9 MiB down (past the 8 MiB growth floor)
;   and touch memory. Pre-fix: unbounded growth - "PF-B GROWN" prints.
;   Post-fix: the floor rejects it, the process is killed.

bits 64
global _start

%define SYS_EXIT      0
%define SYS_WRITE     1
%define SYS_FORK      10
%define SYS_MPROTECT  32
%define SYS_MEMINFO   76

USER_STACK_TOP equ 0x40000000
STACK_BOTTOM   equ (USER_STACK_TOP - 0x10000)

section .data
msg_a_leak:  db "PF-A LEAKED (P=1 fault grew stack: data lost + frames leaked)", 10
msg_a_len    equ $ - msg_a_leak
msg_b_grown: db "PF-B GROWN-BEYOND-FLOOR (unbounded stack growth)", 10
msg_b_len    equ $ - msg_b_grown
msg_done:    db "PF-T DONE", 10
msg_done_len equ $ - msg_done

section .text
print:
    ; rsi=buf, rdx=len
    mov rdi, 1
    mov rax, SYS_WRITE
    int 0x80
    ret

_start:
    mov rax, SYS_FORK
    int 0x80
    test rax, rax
    jz .child_a

    mov rax, SYS_FORK
    int 0x80
    test rax, rax
    jz .child_b

    ; ---- parent: let the children run, then report ----
    mov r12, 40000000
.spin:
    dec r12
    jnz .spin
    lea rsi, [rel msg_done]
    mov rdx, msg_done_len
    call print
    mov rax, SYS_EXIT
    xor rdi, rdi
    int 0x80

    ; ---- child A: write to mprotect(RO) stack-bottom pages ----
.child_a:
    mov rdi, STACK_BOTTOM
    mov rsi, 0x10000
    mov rdx, 1                  ; PROT_READ only
    mov rax, SYS_MPROTECT
    int 0x80

    mov rdi, STACK_BOTTOM       ; first write faults: P=1, in stack window
    mov r13, 16
.wl:
    mov qword [rdi], 0x41424344
    add rdi, 0x1000
    dec r13
    jnz .wl

    ; Reached only when the kernel handed out fresh pages for a P=1 fault.
    lea rsi, [rel msg_a_leak]
    mov rdx, msg_a_len
    call print
    mov rax, SYS_EXIT
    xor rdi, rdi
    int 0x80

    ; ---- child B: walk RSP down past the 8 MiB floor ----
.child_b:
    mov r13, 0x940              ; 0x940 pages = 9.4 MB (> 8 MiB floor)
.bl:
    sub rsp, 0x1000             ; move one page down
    mov [rsp], r13              ; touch: vaddr < previous rsp -> growth
    dec r13
    jnz .bl
    ; Reached only when the kernel grows the stack beyond any limit.
    lea rsi, [rel msg_b_grown]
    mov rdx, msg_b_len
    call print
    mov rax, SYS_EXIT
    xor rdi, rdi
    int 0x80
