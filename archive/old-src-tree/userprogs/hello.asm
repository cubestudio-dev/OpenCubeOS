; Open Cube OS WP-08a - hello world
bits 64
global _start
section .text
_start:
    mov rax, 1           ; sys_write
    mov rdi, 1           ; fd = stdout
    lea rsi, [rel msg]   ; buf
    mov rdx, 20          ; len
    int 0x80
    mov rax, 16          ; sys_exit2
    xor rdi, rdi
    int 0x80
section .rodata
msg: db "hello from userspace", 10
