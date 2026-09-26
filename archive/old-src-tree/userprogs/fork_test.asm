; WP-08a fork test: TRUE POSIX fork
; pid = fork(); if (pid == 0) { child } else { parent }
bits 64
global _start
section .text
print_str:
    mov rdx, rsi
    mov rsi, rdi
    mov rdi, 1
    mov rax, 1
    int 0x80
    ret
print_u64:
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
    ret
_start:
    lea rdi, [rel start_msg]
    mov rsi, 17
    call print_str
    mov rax, 10          ; sys_fork
    int 0x80
    test rax, rax
    jz .child
    ; Parent
    mov r13, rax
    lea rdi, [rel parent_pid_msg]
    mov rsi, 13
    call print_str
    mov rax, 14
    int 0x80
    mov rdi, rax
    call print_u64
    lea rdi, [rel ppid_msg]
    mov rsi, 6
    call print_str
    mov rax, 15
    int 0x80
    mov rdi, rax
    call print_u64
    lea rdi, [rel child_msg]
    mov rsi, 7
    call print_str
    mov rdi, r13
    call print_u64
    mov rax, 12
    xor rdi, rdi
    xor rsi, rsi
    int 0x80
    lea rdi, [rel pass_msg]
    mov rsi, 17
    call print_str
    mov rax, 16
    xor rdi, rdi
    int 0x80
.child:
    lea rdi, [rel child_pid_msg]
    mov rsi, 12
    call print_str
    mov rax, 14
    int 0x80
    mov rdi, rax
    call print_u64
    lea rdi, [rel ppid_msg]
    mov rsi, 6
    call print_str
    mov rax, 15
    int 0x80
    mov rdi, rax
    call print_u64
    mov rax, 16
    xor rdi, rdi
    int 0x80
section .rodata
start_msg:      db "fork_test: start", 10
parent_pid_msg: db "parent: pid=", 0
child_pid_msg:  db "child: pid=", 0
ppid_msg:       db " ppid=", 0
child_msg:      db " child=", 0
pass_msg:       db "fork_test: PASS", 10
