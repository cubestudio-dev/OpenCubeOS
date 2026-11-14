; SPDX-License-Identifier: Apache-2.0
; Copyright 2026 cubestudio-dev <cubestudio@qq.com>

; P0-3 test: call mprotect on kernel address (0x100000) — should return -1
; Also call mprotect on valid user address (0x500000) — should return 0
bits 64
global _start
section .text
_start:
    ; Test 1: mprotect(0x100000, 0x200000, PROT_READ|PROT_WRITE) — kernel addr, should FAIL
    mov rax, 32          ; sys_mprotect
    mov rdi, 0x100000    ; addr = kernel identity-mapped region
    mov rsi, 0x200000    ; len = 2 MB
    mov rdx, 3           ; prot = PROT_READ|PROT_WRITE
    int 0x80
    ; rax = return value (-1 = fail, 0 = success)
    cmp rax, 0
    je .fail            ; if success, bug! (should be -1)
    
    ; Test 1 PASSED — print "MPROTECT_KERNEL_REJECTED"
    mov rax, 1           ; sys_write
    mov rdi, 1           ; fd = stdout
    lea rsi, [rel msg1]
    mov rdx, 26
    int 0x80
    
    ; Test 2: mprotect(0x500000, 0x1000, PROT_READ|PROT_WRITE) — user brk, should SUCCEED
    mov rax, 32          ; sys_mprotect
    mov rdi, 0x500000    ; addr = USER_BRK_BASE
    mov rsi, 0x1000      ; len = 1 page
    mov rdx, 3           ; prot = PROT_READ|PROT_WRITE
    int 0x80
    cmp rax, 0
    jne .fail2           ; if fail, bug! (should be 0)
    
    ; Test 2 PASSED — print "MPROTECT_USER_OK"
    mov rax, 1
    mov rdi, 1
    lea rsi, [rel msg2]
    mov rdx, 16
    int 0x80
    
    ; Print PASS
    mov rax, 1
    mov rdi, 1
    lea rsi, [rel msg_pass]
    mov rdx, 5
    int 0x80
    jmp .exit

.fail:
    ; Test 1 FAILED — kernel addr was accepted (bug!)
    mov rax, 1
    mov rdi, 1
    lea rsi, [rel msg_bug1]
    mov rdx, 23
    int 0x80
    jmp .exit

.fail2:
    ; Test 2 FAILED — user addr was rejected (bug!)
    mov rax, 1
    mov rdi, 1
    lea rsi, [rel msg_bug2]
    mov rdx, 22
    int 0x80
    jmp .exit

.exit:
    mov rax, 16          ; sys_exit2
    xor rdi, rdi
    int 0x80

section .rodata
msg1:     db "MPROTECT_KERNEL_REJECTED", 10      ; 26 bytes
msg2:     db "MPROTECT_USER_OK", 10               ; 16 bytes  
msg_pass: db "PASS", 10                           ; 5 bytes
msg_bug1: db "BUG: kernel addr accepted!", 10     ; 23 bytes (was 28)
msg_bug2: db "BUG: user addr rejected!", 10       ; 22 bytes (was 27)
