; SPDX-License-Identifier: Apache-2.0
; Copyright 2026 cubestudio-dev <cubestudio@qq.com>
; Open Cube OS - 3.0.0-WP02-ocire_recovery
; File: ocire/ocire_mb2_header.S
; Purpose: Multiboot2 header so GRUB (BIOS or UEFI) recognizes the OCIRE
;          kernel.  WP-02 requests a LINEAR FRAMEBUFFER (800x600x32 RGB)
;          instead of the WP-01 80x25 text mode:
;            - OCOS hard-fails without an RGB framebuffer tag, and the
;              OCIRE boot supervisor (WP-02) hands the machine over to
;              OCOS via kexec - GRUB's video mode must be one OCOS can
;              keep using, and the fb tag is re-emitted in the kexec
;              multiboot2 info block verbatim;
;            - OCIRE renders its own 8x16 text on the LFB
;              (ocire_console); when no fb tag arrives (bare metal
;              text mode), it falls back to the VGA text buffer.
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

    ; --- Framebuffer request tag (type 5): 800x600x32 RGB LFB ---
    align 8
    dw 5                           ; type = framebuffer
    dw 0                           ; flags = 0 (advisory)
    dd 20                          ; size of this tag (incl. 8-byte header)
    dd 800                         ; width  (pixels)
    dd 600                         ; height (pixels)
    dd 32                          ; depth  (bits, RGB)

    ; --- End tag ---
    align 8
    dw 0    ; type = end
    dw 0    ; flags
    dd 8    ; size
__ocire_mb2_end:
