; SPDX-License-Identifier: Apache-2.0
; Copyright 2026 cubestudio-dev <cubestudio@qq.com>
; Open Cube OS — WP-01
; File: boot/long_mode_init.S
; Purpose: 64-bit entry point. Reload data segments with 64-bit selectors,
;          set up a proper 16 KiB stack, restore the multiboot2 magic and
;          info pointer, and call kmain(u64 magic, u64 mbi_phys).

bits 64

global long_mode_start
extern kmain
extern __boot_stack_top

section .text
align 16
long_mode_start:
    ; Reload data segments with the 64-bit data selector. In long mode
    ; the segment base/limit are mostly ignored for DS/ES/FS/GS/SS, but
    ; the selector must still point at a valid GDT entry.
    mov     ax, 0x10
    mov     ds, ax
    mov     es, ax
    mov     fs, ax
    mov     gs, ax
    mov     ss, ax

    ; Set up the boot stack.
    mov     rsp, __boot_stack_top
    mov     rbp, rsp

    ; boot.S placed the multiboot2 magic into edi and the info pointer
    ; into esi before the far jump. The far jump preserves GPRs, and the
    ; 32-bit -> 64-bit transition zero-extends the high 32 bits. So:
    ;   rdi = multiboot2 magic, rsi = multiboot2 info pointer.
    ; This already matches the SysV ABI for kmain(u64 magic, u64 mbi).

    ; Align stack for SysV ABI (16-byte aligned at call site).
    and     rsp, 0xfffffffffffffff0

    call    kmain

    ; If kmain ever returns, halt forever.
.halt:
    cli
    hlt
    jmp .halt
