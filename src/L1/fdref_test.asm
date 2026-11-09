; SPDX-License-Identifier: Apache-2.0
; Copyright 2026 cubestudio-dev <cubestudio@qq.com>
;
; BUG-0098 repro (P1, A2-5): VFS fd (kind 1) reference counting across
; dup2 + close.
;
; Scenario (the ush external-command redirection pattern):
;   fd = open("/fdref.txt", O_WRONLY|O_CREAT|O_TRUNC)
;   write(fd, "HELLO")
;   dup2(fd, 7)
;   close(fd)          <- pre-fix: destroyed the shared VFS open-file slot
;   write(7, "-SECOND")<- pre-fix: slot gone, fs_vfs_write returns -1
;                        (write silently fails / data lost)
;   close(7)
;   re-open + read back and compare: expect "HELLO-SECOND"
;
; Pre-fix: prints "FDREF WFAIL" (write(7) == -1) and exits 1.
; Post-fix: prints "FDREF PASS" and exits 0.

bits 64
global _start

%define SYS_EXIT      0
%define SYS_WRITE     1
%define SYS_OPEN      3
%define SYS_CLOSE     4
%define SYS_READ      70
%define SYS_DUP2      51

%define O_WRONLY 0x0002
%define O_CREAT  0x0004
%define O_TRUNC  0x0010
%define O_RDONLY 0x0001

section .data
path:      db "/fdref.txt", 0
path_r:    db "/fdref.txt", 0
w1:        db "HELLO"
w1_len     equ $ - w1
w2:        db "-SECOND"
w2_len     equ $ - w2
rbuf:      times 64 db 0
expect:    db "HELLO-SECOND"
expect_len equ $ - expect

msg_wfail: db "FDREF WFAIL (write after close failed: slot destroyed)", 10
msg_wfail_len equ $ - msg_wfail
msg_rfail: db "FDREF RFAIL (readback mismatch)", 10
msg_rfail_len equ $ - msg_rfail
msg_pass:  db "FDREF PASS", 10
msg_pass_len equ $ - msg_pass

section .text
print:
    ; rsi=buf, rdx=len
    mov rdi, 1
    mov rax, SYS_WRITE
    int 0x80
    ret

_start:
    ; fd = open("/fdref.txt", O_WRONLY|O_CREAT|O_TRUNC)
    lea rdi, [rel path]
    mov rsi, O_WRONLY | O_CREAT | O_TRUNC
    mov rax, SYS_OPEN
    int 0x80
    test rax, rax
    js .wfail
    mov r12, rax              ; fd

    ; write(fd, "HELLO")
    mov rdi, r12
    lea rsi, [rel w1]
    mov rdx, w1_len
    mov rax, SYS_WRITE
    int 0x80

    ; dup2(fd, 7)
    mov rdi, r12
    mov rsi, 7
    mov rax, SYS_DUP2
    int 0x80

    ; close(fd) - the OTHER holder keeps the slot open post-fix
    mov rdi, r12
    mov rax, SYS_CLOSE
    int 0x80

    ; write(7, "-SECOND") - must succeed post-fix
    mov rdi, 7
    lea rsi, [rel w2]
    mov rdx, w2_len
    mov rax, SYS_WRITE
    int 0x80
    test rax, rax
    js .wfail

    ; close(7)
    mov rdi, 7
    mov rax, SYS_CLOSE
    int 0x80

    ; reopen and read back
    lea rdi, [rel path_r]
    mov rsi, O_RDONLY
    mov rax, SYS_OPEN
    int 0x80
    test rax, rax
    js .rfail
    mov r12, rax
    mov rdi, r12
    lea rsi, [rel rbuf]
    mov rdx, 64
    mov rax, SYS_READ
    int 0x80
    mov r13, rax              ; bytes read
    mov rdi, r12
    mov rax, SYS_CLOSE
    int 0x80

    ; compare with "HELLO-SECOND"
    cmp r13, expect_len
    jne .rfail
    xor ecx, ecx
.cmp_loop:
    lea rsi, [rel rbuf]
    add rsi, rcx
    mov al, [rsi]
    lea rsi, [rel expect]
    add rsi, rcx
    cmp al, [rsi]
    jne .rfail
    inc ecx
    cmp ecx, expect_len
    jl .cmp_loop

    lea rsi, [rel msg_pass]
    mov rdx, msg_pass_len
    call print
    mov rax, SYS_EXIT
    xor rdi, rdi
    int 0x80

.wfail:
    lea rsi, [rel msg_wfail]
    mov rdx, msg_wfail_len
    call print
    mov rax, SYS_EXIT
    mov rdi, 1
    int 0x80

.rfail:
    lea rsi, [rel msg_rfail]
    mov rdx, msg_rfail_len
    call print
    mov rax, SYS_EXIT
    mov rdi, 1
    int 0x80
