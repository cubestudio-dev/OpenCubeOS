; SPDX-License-Identifier: Apache-2.0
; Copyright 2026 Cube Studio <cubestudio@qq.com>

; BUG-010 test: multi-process mmap independence
; Parent mmaps "PAAA", forks, child mmaps "CBBB".
; Child verifies its mmap addr == 1006632960 (0x3C000000 = fresh per-process base).
; Parent verifies its page still has "PAAA" after child ran.
; If global mmap_base (bug): child addr = 1006632960+4096 (parent's advanced base).
; If per-process mmap_base (fix): child addr = 1006632960 (fresh start).
bits 64
global _start
section .text

; print_str(rdi=str_ptr, rsi=len)
print_str:
    push rdi
    push rsi
    mov rdx, rsi
    mov rsi, rdi
    mov rdi, 1
    mov rax, 1
    int 0x80
    pop rsi
    pop rdi
    ret

; print_u64(rdi=value) - print decimal
print_u64:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    sub rsp, 24
    lea rcx, [rsp+20]
    mov byte [rcx], 10
    inc rcx
    mov rsi, 10
    mov rax, rdi
    test rax, rax
    jnz .loop
    dec rcx
    mov byte [rcx], '0'
    jmp .print
.loop:
    test rax, rax
    jz .print
    xor rdx, rdx
    div rsi
    add dl, '0'
    dec rcx
    mov [rcx], dl
    jmp .loop
.print:
    lea rdx, [rsp+21]
    sub rdx, rcx
    mov rsi, rcx
    mov rdi, 1
    mov rax, 1
    int 0x80
    add rsp, 24
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    ret

_start:
    ; --- Parent: mmap 1 page ---
    mov rax, 30
    xor rdi, rdi
    mov rsi, 4096
    mov rdx, 3
    int 0x80
    test rax, rax
    jz .mmap_fail
    mov r12, rax

    ; Write "PAAA" to parent's mmap page
    mov dword [r12], 0x41414150

    ; Print parent mmap addr
    lea rdi, [rel p_mmap_msg]
    mov rsi, 14
    call print_str
    mov rdi, r12
    call print_u64

    ; --- Fork ---
    mov rax, 10
    int 0x80
    test rax, rax
    jz .child

    ; --- Parent: wait for child ---
    mov rax, 12
    xor rdi, rdi
    xor rsi, rsi
    int 0x80

    ; Parent: read back its mmap page
    mov eax, dword [r12]
    cmp eax, 0x41414150
    jne .p_fail

    ; Print parent readback
    lea rdi, [rel p_ok_msg]
    mov rsi, 21
    call print_str

    ; Print PASS
    lea rdi, [rel pass_msg]
    mov rsi, 19
    call print_str
    mov rax, 16
    xor rdi, rdi
    int 0x80

.p_fail:
    lea rdi, [rel p_fail_msg]
    mov rsi, 25
    call print_str
    mov rax, 16
    mov rdi, 1
    int 0x80

.mmap_fail:
    lea rdi, [rel mmap_fail_msg]
    mov rsi, 21
    call print_str
    mov rax, 16
    mov rdi, 1
    int 0x80

.child:
    ; --- Child: mmap 1 page ---
    mov rax, 30
    xor rdi, rdi
    mov rsi, 4096
    mov rdx, 3
    int 0x80
    mov r13, rax

    ; Write "CBBB" to child's mmap page
    mov dword [r13], 0x42424243

    ; Print child mmap addr
    lea rdi, [rel c_mmap_msg]
    mov rsi, 13
    call print_str
    mov rdi, r13
    call print_u64

    ; Check: child mmap addr should be 1006632960 (0x3C000000)
    mov rax, r13
    mov rbx, 1006632960
    cmp rax, rbx
    jne .c_addr_wrong

    ; Read back child's mmap page
    mov eax, dword [r13]
    cmp eax, 0x42424243
    jne .c_fail

    ; Print child readback
    lea rdi, [rel c_ok_msg]
    mov rsi, 20
    call print_str
    mov rax, 16
    xor rdi, rdi
    int 0x80

.c_addr_wrong:
    lea rdi, [rel c_addr_msg]
    mov rsi, 23
    call print_str
    mov rax, 16
    mov rdi, 1
    int 0x80

.c_fail:
    lea rdi, [rel c_fail_msg]
    mov rsi, 24
    call print_str
    mov rax, 16
    mov rdi, 1
    int 0x80

section .rodata
p_mmap_msg:    db "parent: mmap=", 0
c_mmap_msg:    db "child: mmap=", 0
p_ok_msg:      db "parent: readback=PAAA", 10
c_ok_msg:      db "child: readback=CBBB", 10
pass_msg:      db "mmap_multi: PASS", 10
p_fail_msg:    db "mmap_multi: FAIL parent", 10
c_fail_msg:    db "mmap_multi: FAIL child", 10
c_addr_msg:    db "mmap_multi: BAD child addr", 10
mmap_fail_msg: db "mmap_multi: FAIL mmap", 10
