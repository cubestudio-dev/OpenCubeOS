; SPDX-License-Identifier: Apache-2.0
; Copyright 2026 cubestudio-dev <cubestudio@qq.com>
; Open Cube OS - 3.0.0-WP05-ocbl
; File: ocbl/bios/tramp_data.asm
; Purpose: embed the assembled trampoline blob (tramp16.bin) into the
;          core image; ocbl_tramp_init() copies it to 0x40000 at boot.

                bits 64
                default rel

                global ocbl_tramp_blob
                global ocbl_tramp_blob_end

section .rodata
ocbl_tramp_blob:
        incbin "tramp16.bin"
ocbl_tramp_blob_end:
