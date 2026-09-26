bits 64
global _start
section .text
_start:
    mov rax, 1
    mov rdi, 1
    lea rsi, [rel msg]
    mov rdx, 14
    int 0x80
.loop:
    jmp .loop
section .rodata
msg: db "loop: running", 10
