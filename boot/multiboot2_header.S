; SPDX-License-Identifier: Apache-2.0
; Copyright 2026 Cube Studio <cubestudio@qq.com>
; Open Cube OS — WP-01
; File: boot/multiboot2_header.S
; Purpose: Multiboot2 header so GRUB (BIOS or UEFI) recognizes our kernel
;          and hands over a framebuffer.
;
; The Multiboot2 spec requires a header somewhere in the first 32768 bytes
; of the kernel image, 8-byte aligned. We put it at the very start of
; .rodata.mbthdr so the linker can place it immediately after the ELF header.

section .rodata.mbthdr
align 8
global __mb2_header_start
global __mb2_header_end

__mb2_header_start:
    ; --- Multiboot2 header (magic, arch, length, checksum) ---
    dd 0xE85250D6                  ; magic
    dd 0                           ; architecture: i386 (32-bit protected mode)
    dd __mb2_header_end - __mb2_header_start  ; header length
    dd 0x100000000 - (0xE85250D6 + 0 + (__mb2_header_end - __mb2_header_start))  ; checksum

    ; --- Framebuffer request tag (type 5) ---
    ; Asks the bootloader to set 800x600x32 graphics mode.
    align 8
    dw 5                           ; type = framebuffer
    dw 0                           ; flags = 0 (advisory)
    dd 20                          ; size of this tag (incl. 8-byte header)
    dd 800                         ; width
    dd 600                         ; height
    dd 32                          ; bpp

    ; --- End tag ---
    align 8
    dw 0    ; type = end
    dw 0    ; flags
    dd 8    ; size
__mb2_header_end:
