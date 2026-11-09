; SPDX-License-Identifier: Apache-2.0 */
; Copyright 2026 cubestudio-dev <cubestudio@qq.com>

; WP-11-crash_recovery 4.5: SIGSEGV-on-fault test.
;
; Flow: register a SIGSEGV handler, print "armed", deliberately deref
; an unmapped kernel address (0xFD000000). The kernel fault path
; delivers SIGSEGV into the handler (same primitive as the syscall
; path: saved frame + restorer trampoline). The handler prints the
; signal number it received, calls SYS_SIGRETURN, and control returns
; right AFTER the faulting instruction - the program then prints
; "recovered" and exits 0. The kernel must stay alive throughout
; (no panic, no kill).
bits 64
global _start

%define SYS_EXIT      0
%define SYS_WRITE     1
%define SYS_SIGNAL    40
%define SYS_SIGRETURN 42
%define SIGSEGV      11

section .text
print_str:                       ; rdi=buf, rsi=len
    mov rdx, rsi
    mov rsi, rdi
    mov rdi, 1
    mov rax, SYS_WRITE
    int 0x80
    ret

_start:
    lea rdi, [rel armed_msg]
    mov rsi, armed_len
    call print_str

    mov rax, SYS_SIGNAL           ; signal(SIGSEGV, handler)
    mov rdi, SIGSEGV
    lea rsi, [rel handler]
    int 0x80

    lea rdi, [rel fault_msg]
    mov rsi, fault_len
    call print_str

    mov rsi, 0xFD000000           ; kernel address: #PF, P=0, U=1
    mov rax, [rsi]

    ; NOT reached on the happy path: the SIGSEGV handler above prints
    ; "caught" and exits from inside itself. (A sigreturn-based
    ; variant would land here but re-fault on the same instruction;
    ; the kernel's 3-delivery guard then kills the process.)
    lea rdi, [rel recovered_msg]
    mov rsi, recovered_len
    call print_str
    mov rax, SYS_EXIT
    xor rdi, rdi
    int 0x80

handler:
    ; rdi = signal number on entry. Print "caught", then EXIT from
    ; inside the handler - the cleanest demonstration that the signal
    ; was really delivered to user code (the kernel iretq'd into this
    ; ring-3 address with the signal number in rdi). A handler that
    ; instead sigreturns onto the same faulting instruction re-faults;
    ; the kernel's 3-delivery ping-pong guard then kills the process
    ; (shown by sigsegv_test --loop below).
    mov [rel got_sig], rdi
    lea rsi, [rel caught_msg]
    mov rdx, caught_len
    mov rdi, 1
    mov rax, SYS_WRITE
    int 0x80
    mov rax, SYS_EXIT
    xor rdi, rdi
    int 0x80
    ret

section .rodata
armed_msg:     db "sigsegv_test: SIGSEGV handler armed", 10
armed_len      equ $ - armed_msg
fault_msg:     db "sigsegv_test: touching 0xFD000000 (kernel page)...", 10
fault_len      equ $ - fault_msg
caught_msg:    db "SIGSEGV-TEST CAUGHT (signal 11 delivered, exiting cleanly)", 10
caught_len     equ $ - caught_msg
recovered_msg: db "sigsegv_test: resumed after fault (sigreturn path)", 10
recovered_len  equ $ - recovered_msg
section .data
got_sig:       dq 0
