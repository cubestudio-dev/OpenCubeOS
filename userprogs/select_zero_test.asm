; SPDX-License-Identifier: Apache-2.0
; Copyright 2026 cubestudio-dev <cubestudio@qq.com>
;
; BUG-0101 repro (P1, A2-8): sys_select semantics.
;
; Case A: select(nfds=1, set={fd 0}, timeout=0).
;   fd 0 is NOT open in this kernel (kind 0) - POSIX says EBADF (-1).
;   Pre-fix: fd 0 silently skipped; no fd ever ready; timeout_ms==0
;   treated as "no timeout" -> falls into the wait path and blocks
;   FOREVER (test freezes: "S1B" never prints).
;   Post-fix: returns -1 immediately (EBADF).
;
; Case B: select on an empty-but-valid pipe read end, timeout=0.
;   POSIX: poll once, nothing ready -> return 0.
;   Post-fix: returns 0 immediately. (Pre-fix this also blocked forever;
;   case A already proves the freeze, case B proves the poll semantics.)
;
; Pre-fix: prints "SZ1 S1" then hangs (no S1B).
; Post-fix: prints SZ1 S1 S1B S2B SZ-PASS and exits 0.

bits 64
global _start

%define SYS_EXIT      0
%define SYS_WRITE     1
%define SYS_PIPE      20
%define SYS_SELECT    80

section .data
setbuf:    times 128 db 0
msg_sz1:   db "SZ1", 10
msg_sz1_len equ $ - msg_sz1
msg_s1:    db "S1 select(fd0,timeout=0)...", 10
msg_s1_len equ $ - msg_s1
msg_s1b:   db "S1B returned (rax in r15)", 10
msg_s1b_len equ $ - msg_s1b
msg_s2b:   db "S2B select(pipe,timeout=0) returned 0", 10
msg_s2b_len equ $ - msg_s2b
msg_pass:  db "SZ-PASS", 10
msg_pass_len equ $ - msg_pass

section .text
print:
    ; rsi=buf, rdx=len
    mov rdi, 1
    mov rax, SYS_WRITE
    int 0x80
    ret

_start:
    lea rsi, [rel msg_sz1]
    mov rdx, msg_sz1_len
    call print

    ; ---- Case A: fd 0, timeout 0 ----
    lea rsi, [rel msg_s1]
    mov rdx, msg_s1_len
    call print

    lea rdi, [rel setbuf]
    mov rcx, 16
    xor rax, rax
.clr:
    mov [rdi], rax
    add rdi, 8
    loop .clr
    bts dword [rel setbuf], 0        ; fd 0 in set

    mov rdi, 1                        ; nfds = 1
    lea rsi, [rel setbuf]
    xor rdx, rdx                      ; timeout_ms = 0
    mov rax, SYS_SELECT
    int 0x80
    mov r15, rax                      ; remember result (-1 expected)

    lea rsi, [rel msg_s1b]
    mov rdx, msg_s1b_len
    call print

    ; ---- Case B: pipe read end (no writer data), timeout 0 ----
    sub rsp, 16
    lea rdi, [rsp]
    mov rax, SYS_PIPE
    int 0x80
    test rax, rax
    jnz .fail
    mov r12d, [rsp]                   ; read end
    add rsp, 16

    lea rdi, [rel setbuf]
    mov rcx, 16
    xor rax, rax
.clr2:
    mov [rdi], rax
    add rdi, 8
    loop .clr2
    ; set bit r12 (the pipe read fd; small enough for one qword)
    lea rdi, [rel setbuf]
    bts [rdi], r12

    mov rdi, 32                       ; nfds covers the pipe fd
    lea rsi, [rel setbuf]
    xor rdx, rdx                      ; timeout_ms = 0
    mov rax, SYS_SELECT
    int 0x80
    test rax, rax
    jnz .fail                         ; expect 0 (nothing ready)

    lea rsi, [rel msg_s2b]
    mov rdx, msg_s2b_len
    call print

    ; Case A must have returned -1 (EBADF), not blocked
    cmp r15, -1
    jne .fail

    lea rsi, [rel msg_pass]
    mov rdx, msg_pass_len
    call print
    mov rax, SYS_EXIT
    xor rdi, rdi
    int 0x80

.fail:
    mov rax, SYS_EXIT
    mov rdi, 1
    int 0x80
