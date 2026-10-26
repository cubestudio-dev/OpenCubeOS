; SPDX-License-Identifier: Apache-2.0
; Copyright 2026 cubestudio-dev <cubestudio@qq.com>
; Open Cube OS - 3.0.0-WP-01-ocire-core
; File: ocire/ocire_mb2_header.S
; Purpose: Multiboot2 header so GRUB (BIOS or UEFI) recognizes the OCIRE
;          kernel.  OCIRE is a text console kernel (80x25 VGA text +
;          serial); the framebuffer tag requests 80x25 with depth 0,
;          which the multiboot2 spec defines as a text mode request.
;
; The header must be 8-byte aligned somewhere in the first 32768 bytes
; of the image; the linker places .rodata.mbthdr at the very start of
; the first loadable segment.

section .rodata.mbthdr
align 8
global __ocire_mb2_start
global __ocire_mb2_end

__ocire_mb2_start:
    ; --- Multiboot2 header (magic, arch, length, checksum) ---
    dd 0xE85250D6                  ; magic
    dd 0                           ; architecture: i386 (32-bit protected mode)
    dd __ocire_mb2_end - __ocire_mb2_start  ; header length
    dd 0x100000000 - (0xE85250D6 + 0 + (__ocire_mb2_end - __ocire_mb2_start))  ; checksum

    ; --- Framebuffer request tag (type 5): 80x25 text mode ---
    ; depth = 0 means "text mode" per the multiboot2 spec.
    align 8
    dw 5                           ; type = framebuffer
    dw 0                           ; flags = 0 (advisory)
    dd 20                          ; size of this tag (incl. 8-byte header)
    dd 80                          ; width  (text columns)
    dd 25                          ; height (text rows)
    dd 0                           ; depth  (0 = text mode)

    ; --- End tag ---
    align 8
    dw 0    ; type = end
    dw 0    ; flags
    dd 8    ; size
__ocire_mb2_end:
