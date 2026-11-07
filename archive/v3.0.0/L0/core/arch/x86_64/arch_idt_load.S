; SPDX-License-Identifier: Apache-2.0
; Copyright 2026 cubestudio-dev <cubestudio@qq.com>
; Open Cube OS - WP-02
; File: kernel/arch_idt_load.S
; Purpose: Load GDT, IDT, and task register (TR). Also reload data segments
;          after the GDT load so they reference the new descriptors.
;
; SysV ABI: rdi = arch_gdt_ptr, rsi = arch_idt_ptr, rdx = tr_selector.

bits 64
default rel

global arch_load_gdt_idt_tr

section .text
align 16
arch_load_gdt_idt_tr:
    ; rdi = arch_gdt_ptr, rsi = arch_idt_ptr, rdx = tr_selector
    lgdt [rdi]
    lidt [rsi]

    ; Reload data segments with the new GDT's selectors.
    mov ax, 0x10           ; data selector (index 2 * 8)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    ; Far jump to reload CS.
    push 0x18              ; code64 selector (index 3 * 8)
    lea rax, [.reload_cs]
    push rax
    retfq
.reload_cs:
    ; Load TR with the TSS selector (0x20 = index 4 * 8, but it's a system
    ; descriptor so the selector is 0x20).
    mov ax, dx
    ltr ax
    ret
