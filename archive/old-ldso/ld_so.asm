; SPDX-License-Identifier: Apache-2.0
; Copyright 2026 cubestudio-dev <cubestudio@qq.com>

; Open Cube OS WP-08b Batch 2 - minimal ld.so test program.
;
; When the kernel encounters an ET_DYN ELF (a PIE executable) with
; PT_INTERP pointing at "/lib/ld.so", it looks up this program in its
; embedded interpreter table, maps its PT_LOAD segment into the user
; address space at vaddr 0x10000000, and jumps to its entry (_start).
;
; Batch 2 only: print a banner and exit. The real ld.so work — parsing
; .dynamic, loading DT_NEEDED libraries, resolving symbols, applying
; relocations, calling .init, and jumping to the main program — is
; deferred to later batches.
;
; Load address: 0x10000000 (256 MB mark).
;   - Well above USER_BRK_BASE (0x500000, where the main program's heap grows).
;   - Well below USER_STACK_TOP (0x40000000, 1 GB).
;   - Does not collide with the main program's PT_LOAD segments either
;     (the main PIE program is mapped by ld.so in later batches, not by
;     the kernel in Batch 2).
;
; Syscall ABI (per kernel/syscall.h):
;   trigger: int 0x80
;   rax = syscall number
;   rdi, rsi, rdx, r10 = arguments
; Used syscalls:
;   SYS_WRITE  = 1   (write(buf, len) to fd 1)
;   SYS_EXIT2  = 16  (exit(code))

bits 64
global _start

section .text
_start:
    ; write(1, msg, msg_end - msg)
    mov rax, 1               ; SYS_WRITE = 1
    mov rdi, 1               ; fd = 1 (stdout, kernel sends to console)
    lea rsi, [rel msg]       ; buf = address of msg
    mov rdx, msg_end - msg  ; len
    int 0x80

    ; exit2(0)
    mov rax, 16              ; SYS_EXIT2 = 16
    xor rdi, rdi             ; exit code = 0
    int 0x80

    ; exit2 should not return; loop forever as a safety net.
.hang:
    hlt
    jmp .hang

section .rodata
msg:     db "ld.so started", 10
msg_end:
